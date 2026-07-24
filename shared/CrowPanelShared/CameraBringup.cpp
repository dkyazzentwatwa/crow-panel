// MIPI-CSI camera bring-up.
//
// HARDWARE-VERIFIED (V1.2 panel, 2026-07-24): live video on the panel at
// 20-30 fps with correct colour. That exercises the whole chain - sensor mode
// table, CSI receiver, ISP demosaic/gamma/CCM, and the PSRAM buffer rotation.
//
// Settled by that run: byte_swap_en = false is CORRECT for this panel. The
// colour came out right first try, which was a genuine coin-flip beforehand
// (the RGB565 byte order is not documented in any header).
//
// Pipeline: SC2336 -> CSI receiver -> ISP -> RGB565 frames in PSRAM.
//
// Every driver used here ships inside Arduino core 3.3.8's own P4 libraries.
// There is no third-party dependency and no ESP-IDF component to vendor:
//   esp_driver_cam/csi/include/esp_cam_ctlr_csi.h  -> libesp_driver_cam.a
//   esp_driver_isp/include/driver/isp_core.h       -> libesp_driver_isp.a
//   esp_hw_support/ldo/include/esp_ldo_regulator.h -> libesp_hw_support.a
//
// The core ships two P4 library trees, packages/esp32/tools/esp32p4-libs/ and
// .../esp32p4_es-libs/; both carry the camera archives and the suite's FQBN
// links the `_es` one. Verified in the link map, not just by a green build:
//   libesp_driver_cam.a(esp_cam_ctlr_csi.c.obj) and
//   libesp_driver_isp.a(isp_core.c.obj) are pulled in, and both vanish from the
// map when USE_CAMERA_DRIVER=0. That is the concrete refutation of this repo's
// former claim that a P4 camera was "a verified impossibility" under Arduino.
//
// Do NOT wrap these includes in __has_include. They are core-bundled and always
// present, and a feature-flagged __has_include around a library include is how
// this repo previously produced a green build with a silently dead feature
// (see docs/troubleshooting.md).

#include "CameraBringup.h"

#if USE_CAMERA_DRIVER && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <esp_cam_ctlr.h>
#include <esp_cam_ctlr_csi.h>
#include <esp_heap_caps.h>
#include <esp_ldo_regulator.h>
#include <driver/isp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Logger.h"

namespace {

// --- Fixed properties of the SC2336 1024x600 RAW8 mode ---------------------
//
// From Espressif's esp-video-components sc2336.c: format entry
// "MIPI_2lane_24Minput_RAW8_1024x600_30fps" and its isp_info[11] block.
//   pclk 84 MHz, vts 1000, hts 2400, tline 33333 ns, bayer BGGR,
//   mipi_clk 288 MHz, 2 lanes.
constexpr color_raw_element_order_t kBayerOrder = COLOR_RAW_ELEMENT_ORDER_BGGR;

// The ISP header asks for a clock "twice higher than cam sensor speed"; the
// sensor's pixel clock is 84 MHz, so 80 MHz is the practical ceiling from the
// default source and comfortably above the CSI line rate this mode produces.
constexpr uint32_t kIspClockHz = 80000000;

// MIPI D-PHY supply. LDO_VO3 feeds VDD_MIPI_DPHY, shared by the DSI panel and
// the CSI receiver. Arduino_GFX acquires the same channel at the same voltage
// during display bring-up; the LDO driver refcounts identical non-adjustable
// acquisitions, so taking it again here is correct and also makes a
// display-less camera build work.
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyLdoMillivolts = 2500;

// Three buffers, not two. Two is the intuitive choice - fill one, read the
// other - but it deadlocks the receiver: while the app holds one frame and the
// receiver is filling the second, a completed frame has nowhere to go, and
// because bk_buffer_dis is set there is no driver-owned fallback buffer either.
// Three gives every state its own slot (app-held, filling, queued) so
// on_get_new_trans can always hand back something valid. At 1.2 MB each on a
// 32 MB PSRAM part the third buffer costs nothing that matters.
constexpr uint8_t kBufferCount = 3;

struct Slot {
  uint16_t *data = nullptr;
  size_t bytes = 0;
};

esp_ldo_channel_handle_t gPhyLdo = nullptr;
isp_proc_handle_t gIsp = nullptr;
esp_cam_ctlr_handle_t gCam = nullptr;
isp_ae_ctlr_t gAe = nullptr;
isp_awb_ctlr_t gAwb = nullptr;
Slot gSlots[kBufferCount];

// Current CCM diagonal. The ISP has no read-back for it, so the last written
// value is tracked here rather than re-derived.
float gGainR = 1.0f;
float gGainG = 1.0f;
float gGainB = 1.0f;

// Slot indices, not pointers: an 8-bit index is cheap to move through an
// ISR-safe queue and cannot dangle.
QueueHandle_t gFreeQueue = nullptr;
QueueHandle_t gReadyQueue = nullptr;

Sc2336Sensor gSensor;
const HardwareProfile *gProfile = nullptr;

volatile uint32_t gFrameCount = 0;
volatile uint32_t gDropCount = 0;
uint32_t gAcquiredSequence = 0;
bool gReady = false;
bool gStreaming = false;
const char *gLastError = "not started";

// Maps a driver-owned buffer pointer back to its slot index. A handful of
// entries, so a linear scan is the right tool. IRAM_ATTR because the transfer
// callbacks below run in interrupt context.
int IRAM_ATTR slotForBuffer(const void *buffer) {
  for (uint8_t i = 0; i < kBufferCount; i++) {
    if (gSlots[i].data == buffer) return i;
  }
  return -1;
}

// ISR: the receiver wants somewhere to put the next frame.
//
// This callback must ALWAYS supply a valid buffer. bk_buffer_dis is set, so the
// driver has no fallback of its own and leaving trans->buffer untouched would
// hand the DMA engine a stale or null pointer. When nothing is free we
// therefore recycle the oldest frame the app has not collected: dropping the
// stalest frame is the correct trade for a live viewfinder, where latency
// matters more than completeness. Only the counter distinguishes it from a
// clean run, and that counter is surfaced rather than hidden.
bool IRAM_ATTR onGetNewTrans(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *) {
  BaseType_t higherPriorityWoken = pdFALSE;
  uint8_t index = 0;

  if (xQueueReceiveFromISR(gFreeQueue, &index, &higherPriorityWoken) != pdTRUE) {
    // Nothing free: steal the oldest queued-but-uncollected frame.
    if (xQueueReceiveFromISR(gReadyQueue, &index, &higherPriorityWoken) != pdTRUE) {
      // Every slot is held by the app. That breaks the acquire/release
      // contract, and there is no safe buffer to offer - refuse rather than
      // scribble over a frame the app is reading.
      gDropCount++;
      return higherPriorityWoken == pdTRUE;
    }
    gDropCount++;
  }

  trans->buffer = gSlots[index].data;
  trans->buflen = gSlots[index].bytes;
  return higherPriorityWoken == pdTRUE;
}

// ISR: a frame landed. Publish its slot for acquire() to pick up.
bool IRAM_ATTR onTransFinished(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *) {
  BaseType_t higherPriorityWoken = pdFALSE;
  const int index = slotForBuffer(trans->buffer);
  if (index < 0) return false;  // not ours; nothing sane to do from an ISR
  gFrameCount++;
  const uint8_t slot = (uint8_t)index;
  if (xQueueSendFromISR(gReadyQueue, &slot, &higherPriorityWoken) != pdTRUE) {
    // Ready queue full: recycle immediately so the receiver keeps running.
    gDropCount++;
    xQueueSendFromISR(gFreeQueue, &slot, &higherPriorityWoken);
  }
  return higherPriorityWoken == pdTRUE;
}

void freeBuffers() {
  for (uint8_t i = 0; i < kBufferCount; i++) {
    if (gSlots[i].data != nullptr) {
      heap_caps_free(gSlots[i].data);
      gSlots[i].data = nullptr;
      gSlots[i].bytes = 0;
    }
  }
}

bool allocateBuffers(uint16_t width, uint16_t height) {
  const size_t bytes = (size_t)width * height * sizeof(uint16_t);
  for (uint8_t i = 0; i < kBufferCount; i++) {
    // MALLOC_CAP_CACHE_ALIGNED matters: the receiver DMAs straight into these
    // and the driver cache-syncs whole lines. An unaligned buffer produces
    // corrupt bands rather than a clean failure.
    gSlots[i].data = (uint16_t *)heap_caps_calloc(
        1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);
    if (gSlots[i].data == nullptr) {
      gLastError = "out of PSRAM for frame buffers";
      Logger::warn("camera", "could not allocate " + String((uint32_t)bytes) +
                                 " bytes for buffer " + String(i));
      freeBuffers();
      return false;
    }
    gSlots[i].bytes = bytes;
  }
  Logger::info("camera", "allocated " + String(kBufferCount) + " x " +
                             String((uint32_t)bytes) + " byte RGB565 buffers in PSRAM");
  return true;
}

// sRGB-ish 1/2.2 gamma, as a plain LUT operator for the driver to sample.
// Signature is fixed by esp_isp_gamma_fill_curve_points: input and output are
// both 0..255.
uint32_t gammaCurve(uint32_t x) {
  if (x == 0) return 0;
  if (x >= 255) return 255;
  const float normalized = (float)x / 255.0f;
  const float corrected = powf(normalized, 1.0f / 2.2f);
  const int32_t out = (int32_t)(corrected * 255.0f + 0.5f);
  return (uint32_t)(out > 255 ? 255 : (out < 0 ? 0 : out));
}

// Writes the CCM as a diagonal matrix carrying the current per-channel gains.
// Off-diagonal terms stay zero: this is white balance, not colour-space
// correction. A real CCM tuned for the SC2336's colour response would fill
// them in, but that needs a colour chart and a measurement session, and an
// invented matrix would look worse than none.
bool applyColorMatrix() {
  if (gIsp == nullptr) return false;
  esp_isp_ccm_config_t ccm = {};
  ccm.matrix[0][0] = gGainR;
  ccm.matrix[1][1] = gGainG;
  ccm.matrix[2][2] = gGainB;
  // Clamp rather than error when a gain lands outside the hardware's range;
  // a slightly-wrong tint beats a failed call that leaves the last gains stuck.
  ccm.saturation = true;
  return esp_isp_ccm_configure(gIsp, &ccm) == ESP_OK;
}

// Static image-pipeline tuning, applied once at begin(). None of this is
// scene-dependent - the parts that are (exposure, white balance) are driven by
// the application's control loops through the statistics calls below.
//
// Every stage here is best-effort: a failure degrades image quality but must
// not take the camera down, because a slightly noisy picture is far better than
// no picture. Failures are logged, not propagated.
void configureImagePipeline(uint16_t width, uint16_t height) {
  // Black level correction, FIRST in the pipeline and first here for a reason.
  //
  // A CMOS sensor does not read zero for black - it sits on a pedestal offset,
  // so without BLC the darkest pixel lands somewhere around 16/255 instead of
  // 0. Every downstream stage then works on a raised floor, and the result is
  // exactly what the first hardware capture showed: hazy, washed-out frames
  // with grey instead of black and visibly collapsed contrast.
  //
  // Omitting this was a real gap in the original pipeline. The offsets below
  // are the conventional 8-bit pedestal for this class of sensor, not a
  // measured value for this part - if blacks still look lifted (or, worse, get
  // crushed), this is the number to trim.
  esp_isp_blc_config_t blc = {};
  blc.window.top_left.x = 0;
  blc.window.top_left.y = 0;
  blc.window.btm_right.x = width;
  blc.window.btm_right.y = height;
  blc.filter_enable = false;
  // Bayer order here is BGGR, so the quad reads B / G / G / R.
  constexpr uint32_t kPedestal = 16;
  esp_isp_blc_offset_t blcOffset = {};
  blcOffset.top_left_chan_offset = kPedestal;      // B
  blcOffset.top_right_chan_offset = kPedestal;     // G
  blcOffset.bottom_left_chan_offset = kPedestal;   // G
  blcOffset.bottom_right_chan_offset = kPedestal;  // R
  if (esp_isp_blc_configure(gIsp, &blc) == ESP_OK &&
      esp_isp_blc_set_correction_offset(gIsp, &blcOffset) == ESP_OK &&
      esp_isp_blc_enable(gIsp) == ESP_OK) {
    Logger::info("camera", "black level correction on (pedestal " + String(kPedestal) + ")");
  } else {
    Logger::warn("camera", "BLC setup failed - expect washed-out, low-contrast frames");
  }

  // Demosaic: RAW Bayer -> RGB. Without this there is no colour image at all,
  // so it is the one stage whose failure is worth shouting about.
  esp_isp_demosaic_config_t demosaic = {};
  demosaic.grad_ratio.integer = 2;
  demosaic.grad_ratio.decimal = 0;
  demosaic.padding_mode = ISP_DEMOSAIC_EDGE_PADDING_MODE_SRND_DATA;
  demosaic.padding_data = 0;
  if (esp_isp_demosaic_configure(gIsp, &demosaic) != ESP_OK ||
      esp_isp_demosaic_enable(gIsp) != ESP_OK) {
    Logger::warn("camera", "demosaic setup failed - expect a Bayer-patterned image");
  }

  // Bilateral filter: denoise. A 2 MP sensor at indoor light is noisy, and the
  // JPEG encoder downstream spends bits on noise if it is not removed first.
  // Level 4 of 2..20 is deliberately gentle - heavier settings smear detail.
  esp_isp_bf_config_t bf = {};
  bf.padding_mode = ISP_BF_EDGE_PADDING_MODE_SRND_DATA;
  bf.padding_data = 0;
  bf.denoising_level = 4;
  for (int i = 0; i < ISP_BF_TEMPLATE_X_NUMS; i++) {
    for (int j = 0; j < ISP_BF_TEMPLATE_Y_NUMS; j++) {
      // Gaussian-ish 3x3: centre weighted, edges least.
      bf.bf_template[i][j] = (i == 1 && j == 1) ? 4 : ((i == 1 || j == 1) ? 2 : 1);
    }
  }
  if (esp_isp_bf_configure(gIsp, &bf) != ESP_OK || esp_isp_bf_enable(gIsp) != ESP_OK) {
    Logger::warn("camera", "bilateral filter unavailable; image will be noisier");
  }

  // Gamma: linear sensor data looks flat and dark on a display. Standard sRGB
  // -ish 1/2.2 curve, applied per channel.
  isp_gamma_curve_points_t points = {};
  if (esp_isp_gamma_fill_curve_points(gammaCurve, &points) == ESP_OK) {
    const color_component_t components[] = {COLOR_COMPONENT_R, COLOR_COMPONENT_G,
                                            COLOR_COMPONENT_B};
    for (color_component_t c : components) {
      if (esp_isp_gamma_configure(gIsp, c, &points) != ESP_OK) {
        Logger::warn("camera", "gamma curve rejected; image will look flat");
        break;
      }
    }
    esp_isp_gamma_enable(gIsp);
  }

  // Colour correction starts at unity gain. The AWB loop moves it.
  applyColorMatrix();
  esp_isp_ccm_enable(gIsp);

  // --- Statistics engines -------------------------------------------------
  //
  // These only measure. Nothing in the Arduino core acts on them, which is
  // exactly why the application has to.

  // AE samples after demosaic so it sees luminance before gamma bends it -
  // a linear signal is what an exposure loop wants.
  esp_isp_ae_config_t ae = {};
  ae.sample_point = ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC;
  ae.window.top_left.x = 0;
  ae.window.top_left.y = 0;
  ae.window.btm_right.x = width;
  ae.window.btm_right.y = height;
  ae.intr_priority = 0;
  if (esp_isp_new_ae_controller(gIsp, &ae, &gAe) == ESP_OK) {
    esp_isp_ae_controller_enable(gAe);
  } else {
    gAe = nullptr;
    Logger::warn("camera", "AE statistics unavailable; exposure stays manual");
  }

  // AWB samples AFTER the CCM, so the loop measures its own correction and
  // converges. The window is inset by 1/8 on each side because image edges are
  // the first thing to overexpose and overexposed pixels are worthless as
  // white references (the header says as much).
  esp_isp_awb_config_t awb = {};
  awb.sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM;
  awb.window.top_left.x = width / 8;
  awb.window.top_left.y = height / 8;
  awb.window.btm_right.x = width - width / 8;
  awb.window.btm_right.y = height - height / 8;
  awb.subwindow = awb.window;
  // Luminance range excludes the very darkest and the blown-out top end.
  awb.white_patch.luminance.min = 24;
  awb.white_patch.luminance.max = 230 * 3;

  // The R/G and B/G bounds are DELIBERATELY WIDE, and the first hardware
  // capture is why. They were originally 0.6-2.0, which sounds reasonable and
  // is a chicken-and-egg trap: raw Bayer output is green-heavy (two green
  // photosites per quad), so an uncorrected frame has R/G and B/G well below
  // 0.6. No pixel qualified as a white patch, the loop got zero samples, it
  // returned "no estimate", the gains stayed at unity - and the green cast it
  // existed to remove was precisely what stopped it from ever seeing anything.
  //
  // Espressif's own header says as much: "The ratio could be as wider as
  // possible, so that all the distorted pixels will be counted for the
  // reference of white balance." Taking that literally is correct - the whole
  // point is to measure a distorted image and undo the distortion.
  awb.white_patch.red_green_ratio.min = 0.1f;
  awb.white_patch.red_green_ratio.max = 3.9f;
  awb.white_patch.blue_green_ratio.min = 0.1f;
  awb.white_patch.blue_green_ratio.max = 3.9f;
  awb.intr_priority = 0;
  if (esp_isp_new_awb_controller(gIsp, &awb, &gAwb) == ESP_OK) {
    esp_isp_awb_controller_enable(gAwb);
  } else {
    gAwb = nullptr;
    Logger::warn("camera", "AWB statistics unavailable; colour stays uncorrected");
  }
}

}  // namespace

namespace CrowCamera {

bool begin(const HardwareProfile &profile) {
  if (gReady) return true;
  gProfile = &profile;
  gFrameCount = 0;
  gDropCount = 0;
  gAcquiredSequence = 0;

  const uint16_t width = profile.camera.width;
  const uint16_t height = profile.camera.height;

  // 1. Sensor first. If nothing answers on the SCCB bus there is no point
  //    powering the D-PHY or claiming the ISP, and the error is far clearer
  //    here than as a CSI timeout three steps later.
  if (!gSensor.begin(profile.camera)) {
    gLastError = gSensor.lastError();
    return false;
  }

  // 2. MIPI D-PHY power. Non-adjustable so it composes with the display's
  //    identical acquisition.
  esp_ldo_channel_config_t ldoConfig = {};
  ldoConfig.chan_id = kMipiPhyLdoChannel;
  ldoConfig.voltage_mv = kMipiPhyLdoMillivolts;
  if (esp_ldo_acquire_channel(&ldoConfig, &gPhyLdo) != ESP_OK) {
    gLastError = "could not power the MIPI D-PHY rail";
    Logger::warn("camera", gLastError);
    return false;
  }

  // 3. Buffers before any hardware that might immediately want one.
  if (!allocateBuffers(width, height)) {
    end();
    return false;
  }
  gFreeQueue = xQueueCreate(kBufferCount, sizeof(uint8_t));
  gReadyQueue = xQueueCreate(kBufferCount, sizeof(uint8_t));
  if (gFreeQueue == nullptr || gReadyQueue == nullptr) {
    gLastError = "could not create frame queues";
    end();
    return false;
  }

  // 4. ISP. It sits inline between the CSI receiver and memory, turning the
  //    sensor's RAW8 Bayer into the RGB565 the panel and the JPEG encoder both
  //    want. Without it the app would have to demosaic in software, which at
  //    1024x600x30 is not happening on any CPU.
  esp_isp_processor_cfg_t ispConfig = {};
  ispConfig.clk_src = ISP_CLK_SRC_DEFAULT;
  ispConfig.clk_hz = kIspClockHz;
  ispConfig.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  ispConfig.input_data_color_type = ISP_COLOR_RAW8;
  ispConfig.output_data_color_type = ISP_COLOR_RGB565;
  ispConfig.has_line_start_packet = false;
  ispConfig.has_line_end_packet = false;
  ispConfig.h_res = width;
  ispConfig.v_res = height;
  ispConfig.bayer_order = kBayerOrder;
  if (esp_isp_new_processor(&ispConfig, &gIsp) != ESP_OK) {
    gLastError = "could not create the ISP processor";
    Logger::warn("camera", gLastError);
    end();
    return false;
  }
  if (esp_isp_enable(gIsp) != ESP_OK) {
    gLastError = "could not enable the ISP";
    end();
    return false;
  }

  // Demosaic, denoise, gamma, CCM, and the AE/AWB statistics engines. Every
  // stage is best-effort and logs its own failure - a degraded image must not
  // fail the whole bring-up.
  configureImagePipeline(width, height);

  // 5. CSI receiver.
  esp_cam_ctlr_csi_config_t csiConfig = {};
  csiConfig.ctlr_id = 0;
  csiConfig.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csiConfig.h_res = width;
  csiConfig.v_res = height;
  csiConfig.data_lane_num = profile.camera.csiLanes;
  csiConfig.lane_bit_rate_mbps = profile.camera.laneBitRateMbps;
  csiConfig.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csiConfig.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csiConfig.queue_items = kBufferCount;
  // byte_swap_en: HARDWARE-VERIFIED false on the V1.2 panel (2026-07-24) -
  // colour renders correctly with no swap. This was the one bring-up unknown
  // that could not be resolved from headers; do not change it speculatively.
  csiConfig.byte_swap_en = false;
  csiConfig.bk_buffer_dis = true;  // we supply our own buffers; skip the driver's
  if (esp_cam_new_csi_ctlr(&csiConfig, &gCam) != ESP_OK) {
    gLastError = "could not create the CSI controller";
    Logger::warn("camera", gLastError);
    end();
    return false;
  }

  esp_cam_ctlr_evt_cbs_t callbacks = {};
  callbacks.on_get_new_trans = onGetNewTrans;
  callbacks.on_trans_finished = onTransFinished;
  if (esp_cam_ctlr_register_event_callbacks(gCam, &callbacks, nullptr) != ESP_OK) {
    gLastError = "could not register CSI callbacks";
    end();
    return false;
  }
  if (esp_cam_ctlr_enable(gCam) != ESP_OK) {
    gLastError = "could not enable the CSI controller";
    end();
    return false;
  }

  // 6. Sensor mode table last, so the sensor is not streaming into hardware
  //    that does not exist yet. configure() leaves it in standby regardless.
  if (!gSensor.configure()) {
    gLastError = gSensor.lastError();
    end();
    return false;
  }

  gReady = true;
  gLastError = "";
  Logger::info("camera", "pipeline ready: SC2336 -> CSI(" +
                             String(profile.camera.csiLanes) + " lane @" +
                             String(profile.camera.laneBitRateMbps) + "Mbps) -> ISP -> RGB565 " +
                             String(width) + "x" + String(height));
  return true;
}

bool start() {
  if (!gReady) {
    gLastError = "start() before a successful begin()";
    return false;
  }
  if (gStreaming) return true;

  // Every buffer starts free. Drain first so a restart cannot double-queue a
  // slot that acquire() still believes it owns.
  xQueueReset(gFreeQueue);
  xQueueReset(gReadyQueue);
  for (uint8_t i = 0; i < kBufferCount; i++) {
    xQueueSend(gFreeQueue, &i, 0);
  }

  // Receiver before sensor: armed hardware waiting for data beats data
  // arriving at hardware that is not listening.
  if (esp_cam_ctlr_start(gCam) != ESP_OK) {
    gLastError = "could not start the CSI controller";
    return false;
  }
  if (!gSensor.setStreaming(true)) {
    esp_cam_ctlr_stop(gCam);
    gLastError = gSensor.lastError();
    return false;
  }
  gStreaming = true;
  Logger::info("camera", "streaming");
  return true;
}

bool stop() {
  if (!gReady || !gStreaming) return true;
  gSensor.setStreaming(false);
  esp_cam_ctlr_stop(gCam);
  gStreaming = false;
  Logger::info("camera", "stopped");
  return true;
}

void end() {
  stop();
  // Statistics controllers before the processor that owns them.
  if (gAe != nullptr) {
    esp_isp_ae_controller_disable(gAe);
    esp_isp_del_ae_controller(gAe);
    gAe = nullptr;
  }
  if (gAwb != nullptr) {
    esp_isp_awb_controller_disable(gAwb);
    esp_isp_del_awb_controller(gAwb);
    gAwb = nullptr;
  }
  if (gCam != nullptr) {
    esp_cam_ctlr_disable(gCam);
    esp_cam_ctlr_del(gCam);
    gCam = nullptr;
  }
  if (gIsp != nullptr) {
    esp_isp_disable(gIsp);
    esp_isp_del_processor(gIsp);
    gIsp = nullptr;
  }
  if (gFreeQueue != nullptr) {
    vQueueDelete(gFreeQueue);
    gFreeQueue = nullptr;
  }
  if (gReadyQueue != nullptr) {
    vQueueDelete(gReadyQueue);
    gReadyQueue = nullptr;
  }
  freeBuffers();
  if (gPhyLdo != nullptr) {
    esp_ldo_release_channel(gPhyLdo);
    gPhyLdo = nullptr;
  }
  gReady = false;
}

bool acquire(Frame &out, uint32_t timeoutMs) {
  if (!gReady || !gStreaming) return false;
  uint8_t index = 0;
  if (xQueueReceive(gReadyQueue, &index, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    return false;
  }
  out.data = gSlots[index].data;
  out.bytes = gSlots[index].bytes;
  out.width = gProfile->camera.width;
  out.height = gProfile->camera.height;
  out.sequence = ++gAcquiredSequence;
  out.captureMs = millis();
  return true;
}

void release(const Frame &frame) {
  if (!gReady) return;
  const int index = slotForBuffer(frame.data);
  if (index < 0) return;
  const uint8_t slot = (uint8_t)index;
  xQueueSend(gFreeQueue, &slot, 0);
}

bool meanLuminance(uint32_t &mean, uint32_t timeoutMs, int *blocks, size_t blockCount) {
  if (!gReady || gAe == nullptr) return false;
  isp_ae_result_t result = {};
  // One-shot rather than continuous: the control loop runs at a few Hz, so
  // there is no reason to keep an interrupt firing every frame.
  //
  // The timeout is deliberately short. This blocks the calling task until the
  // ISP raises its statistics interrupt, so a long timeout here stalls the
  // render loop and - because a tap needs two touch samples to register - makes
  // the touchscreen feel dead. Missing a statistics window is harmless; the
  // loop simply tries again on the next update.
  if (esp_isp_ae_controller_get_oneshot_statistics(gAe, (int)timeoutMs, &result) != ESP_OK) {
    return false;
  }
  uint64_t sum = 0;
  size_t copied = 0;
  for (int x = 0; x < ISP_AE_BLOCK_X_NUM; x++) {
    for (int y = 0; y < ISP_AE_BLOCK_Y_NUM; y++) {
      sum += (uint64_t)result.luminance[x][y];
      if (blocks != nullptr && copied < blockCount) blocks[copied++] = result.luminance[x][y];
    }
  }
  mean = (uint32_t)(sum / (ISP_AE_BLOCK_X_NUM * ISP_AE_BLOCK_Y_NUM));
  return true;
}

bool whiteBalanceEstimate(float &redGain, float &blueGain, uint32_t &patchCount,
                          uint32_t timeoutMs) {
  redGain = 1.0f;
  blueGain = 1.0f;
  patchCount = 0;
  if (!gReady || gAwb == nullptr) return false;

  isp_awb_stat_result_t result = {};
  // Short timeout, same reasoning as meanLuminance: this blocks the caller.
  if (esp_isp_awb_controller_get_oneshot_statistics(gAwb, (int)timeoutMs, &result) != ESP_OK) {
    return false;
  }
  patchCount = result.white_patch_num;

  // Too few white patches means the scene has no neutral reference. Gray-world
  // on a handful of pixels produces a wild, confident, wrong answer - report
  // failure and let the caller keep its previous gains.
  //
  // The threshold is low on purpose. Paired with the wide ratio bounds above,
  // the goal is for the loop to get SOME signal from an uncorrected frame; a
  // high bar here would recreate the same deadlock from the other direction.
  if (result.white_patch_num < 16) return false;
  if (result.sum_r == 0 || result.sum_b == 0 || result.sum_g == 0) return false;

  const float avgR = (float)result.sum_r / (float)result.white_patch_num;
  const float avgG = (float)result.sum_g / (float)result.white_patch_num;
  const float avgB = (float)result.sum_b / (float)result.white_patch_num;

  // Gray world: whatever the sensor calls white should have equal channels.
  redGain = avgG / avgR;
  blueGain = avgG / avgB;
  return true;
}

bool setColorGains(float red, float green, float blue) {
  if (!gReady) return false;
  // The CCM saturates above 4.0; keeping well inside that avoids a correction
  // so aggressive it clips a channel rather than balancing it.
  auto clamp = [](float v) { return v < 0.25f ? 0.25f : (v > 3.5f ? 3.5f : v); };
  gGainR = clamp(red);
  gGainG = clamp(green);
  gGainB = clamp(blue);
  return applyColorMatrix();
}

void colorGains(float &red, float &green, float &blue) {
  red = gGainR;
  green = gGainG;
  blue = gGainB;
}

bool ready() { return gReady; }
bool streaming() { return gStreaming; }
const char *lastError() { return gLastError; }
uint32_t frameCount() { return gFrameCount; }
uint32_t dropCount() { return gDropCount; }
Sc2336Sensor *sensor() { return gReady ? &gSensor : nullptr; }

void printStatus(Print &out) {
  out.print(F("[camera] "));
  if (!gReady) {
    out.print(F("down ("));
    out.print(gLastError);
    out.println(')');
    gSensor.printStatus(out);
    return;
  }
  out.print(gStreaming ? F("streaming") : F("idle"));
  out.print(F(" frames="));
  out.print(gFrameCount);
  out.print(F(" dropped="));
  out.print(gDropCount);
  out.print(F(" buffers="));
  out.print(kBufferCount);
  out.print(F("x"));
  out.print((uint32_t)gSlots[0].bytes);
  out.println(F("B PSRAM"));
  gSensor.printStatus(out);
}

}  // namespace CrowCamera

#endif  // USE_CAMERA_DRIVER && CONFIG_IDF_TARGET_ESP32P4
