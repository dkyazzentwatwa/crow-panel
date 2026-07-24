#include "TuneUi.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
#include "UiLayout.h"

namespace {

using namespace UiLayout;

constexpr uint32_t kFlashMs = 90;
constexpr uint16_t kAccentDim = Widgets::rgb(0x0C, 0x6E, 0x72);  // "on" step
constexpr uint16_t kPadFlash = Widgets::kAccent;

inline uint16_t bit16(uint8_t i) { return (uint16_t)1 << i; }

const char *chokeLabel(uint8_t group) {
  static const char *kLabels[5] = {"OFF", "1", "2", "3", "4"};
  return kLabels[group <= 4 ? group : 0];
}

}  // namespace

void TuneUi::begin(SampleBank *bank, Sequencer *seq, VisualVoices *voices,
                   TriggerFn trigger, TransportFn transport,
                   AudioStatusFn audioStatus, KitStepFn kitStep, void *ctx) {
  bank_ = bank;
  seq_ = seq;
  voices_ = voices;
  trigger_ = trigger;
  transport_ = transport;
  audioStatus_ = audioStatus;
  kitStep_ = kitStep;
  ctx_ = ctx;
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHER TUNE",
                                     /*manualFlush=*/true);
  dirtyAll_ = true;
}

void TuneUi::tick() {
  if (!displayReady_ || seq_ == nullptr || bank_ == nullptr) {
    return;
  }
  uint32_t now = millis();
  CrowDisplay::tick();
  handleTouch_(now);
  syncState_(now);
  render_(now);
}

void TuneUi::notePadFlash(uint8_t padIdx) {
  if (padIdx >= SampleBank::kPadCount) {
    return;
  }
  padFlashUntil_[padIdx] = millis() + kFlashMs;
  dirtyPads_ |= bit16(padIdx);
}

void TuneUi::selectPad(uint8_t padIdx) {
  if (padIdx >= SampleBank::kPadCount || padIdx == selectedPad_) {
    return;
  }
  dirtyPads_ |= bit16(selectedPad_) | bit16(padIdx);
  selectedPad_ = padIdx;
  dirtyHeader_ = true;
  dirtySteps_ = 0xFFFF;
  dirtyEdit_ = true;
}

String TuneUi::perfLine() const {
  return String("render last=") + String(lastRenderUs_) + "us max=" +
         String(maxRenderUs_) + "us frames=" + String(renderCount_) +
         " display=" + (displayReady_ ? "ready" : "off");
}

String TuneUi::touchLine() const {
  return touch_.diagnostics();
}

// --- Touch ---

void TuneUi::handleTouch_(uint32_t now) {
  touch_.tick();
  for (uint8_t i = 0; i < TouchTracker::kMaxContacts; i++) {
    TouchTracker::Contact &c = touch_.contact(i);
    if (c.pressedEdge) {
      c.owner = hitTest(c.downX, c.downY);
      pressControl_(c, now);
    } else if (c.active && c.owner != kControlNone) {
      holdControl_(c, now);
    }
  }
}

void TuneUi::pressControl_(TouchTracker::Contact &c, uint32_t now) {
  int16_t owner = c.owner;
  if (owner >= kControlPadBase && owner < kControlPadBase + 16) {
    uint8_t pad = owner - kControlPadBase;
    // Velocity from vertical position in the cell: top soft, bottom hard.
    int16_t rel = c.downY - padY(padRow(pad));
    if (rel < 0) rel = 0;
    if (rel > kPadCellH - 1) rel = kPadCellH - 1;
    uint8_t velocity = 40 + (uint8_t)((int32_t)rel * 87 / (kPadCellH - 1));
    if (trigger_ != nullptr) {
      trigger_(ctx_, pad, velocity);
    }
    notePadFlash(pad);
    selectPad(pad);
    return;
  }
  if (owner >= kControlStepBase && owner < kControlStepBase + 16) {
    uint8_t step = owner - kControlStepBase;
    seq_->toggleStep(step, selectedPad_, /*cycleAccent=*/true);
    dirtySteps_ |= bit16(step);
    return;
  }
  switch (owner) {
    case kControlPlay:
      if (transport_ != nullptr) transport_(ctx_, kOpPlay);
      dirtyTransport_ = true;
      break;
    case kControlStop:
      if (transport_ != nullptr) transport_(ctx_, kOpStop);
      dirtyTransport_ = true;
      break;
    case kControlRec:
      if (transport_ != nullptr) transport_(ctx_, kOpRecordToggle);
      dirtyTransport_ = true;
      break;
    case kControlBpmMinus:
    case kControlBpmPlus:
      bumpBpm_(owner == kControlBpmPlus ? 1 : -1);
      c.nextRepeatMs = now + 400;
      break;
    case kControlSwingMinus:
    case kControlSwingPlus:
      bumpSwing_(owner == kControlSwingPlus ? 1 : -1);
      c.nextRepeatMs = now + 400;
      break;
    case kControlMetro:
      seq_->setMetronome(!seq_->metronome());
      dirtyTransport_ = true;
      break;
    case kControlPattern0:
    case kControlPattern1:
    case kControlPattern2:
    case kControlPattern3:
      seq_->setPattern(owner - kControlPattern0);
      break;  // syncState_ picks up the change (also covers serial `pat`)
    case kControlVolSlider:
      bank_->setGain(selectedPad_, (uint8_t)sliderValue(c.downX, 255));
      dirtyEdit_ = true;
      break;
    case kControlPitchSlider:
      bank_->setPitch(selectedPad_, (int8_t)(sliderValue(c.downX, 24) - 12));
      dirtyEdit_ = true;
      break;
    case kControlChoke0:
    case kControlChoke1:
    case kControlChoke2:
    case kControlChoke3:
    case kControlChoke4:
      bank_->setChoke(selectedPad_, owner - kControlChoke0);
      dirtyEdit_ = true;
      dirtyPads_ |= bit16(selectedPad_);
      break;
    case kControlKitPrev:
    case kControlKitNext:
      if (kitStep_ != nullptr) {
        kitStep_(ctx_, owner == kControlKitNext ? 1 : -1);
      }
      break;
    default:
      break;
  }
}

void TuneUi::holdControl_(TouchTracker::Contact &c, uint32_t now) {
  switch (c.owner) {
    case kControlBpmMinus:
    case kControlBpmPlus:
    case kControlSwingMinus:
    case kControlSwingPlus: {
      // Hold-repeat only while the finger stays on the button.
      if (hitTest(c.x, c.y) != c.owner || c.nextRepeatMs == 0 ||
          (int32_t)(now - c.nextRepeatMs) < 0) {
        return;
      }
      bool bpm = (c.owner == kControlBpmMinus || c.owner == kControlBpmPlus);
      int8_t dir = (c.owner == kControlBpmPlus || c.owner == kControlSwingPlus) ? 1 : -1;
      if (bpm) {
        bumpBpm_(dir);
        c.nextRepeatMs = now + ((now - c.downMs > 2000) ? 40 : 80);
      } else {
        bumpSwing_(dir);
        c.nextRepeatMs = now + 80;
      }
      break;
    }
    case kControlVolSlider:
      bank_->setGain(selectedPad_, (uint8_t)sliderValue(c.x, 255));
      if (now - lastSliderDrawMs_ >= 33) {
        lastSliderDrawMs_ = now;
        dirtyEdit_ = true;
      }
      break;
    case kControlPitchSlider:
      bank_->setPitch(selectedPad_, (int8_t)(sliderValue(c.x, 24) - 12));
      if (now - lastSliderDrawMs_ >= 33) {
        lastSliderDrawMs_ = now;
        dirtyEdit_ = true;
      }
      break;
    default:
      break;
  }
}

void TuneUi::bumpBpm_(int8_t dir) {
  int next = (int)seq_->bpm() + dir;
  if (next < 40) next = 40;
  if (next > 240) next = 240;
  seq_->setBpm((uint16_t)next);
}

void TuneUi::bumpSwing_(int8_t dir) {
  int next = (int)seq_->swing() + dir;
  if (next < Sequencer::kSwingMin) next = Sequencer::kSwingMin;
  if (next > Sequencer::kSwingMax) next = Sequencer::kSwingMax;
  seq_->setSwing((uint8_t)next);
}

// --- State diff (serial edits reach the screen through the same path) ---

void TuneUi::syncState_(uint32_t now) {
  // Flash decay.
  for (uint8_t i = 0; i < SampleBank::kPadCount; i++) {
    if (padFlashUntil_[i] != 0 && (int32_t)(now - padFlashUntil_[i]) >= 0) {
      padFlashUntil_[i] = 0;
      dirtyPads_ |= bit16(i);
    }
  }

  if (seq_->bpm() != mirrorBpm_ || seq_->swing() != mirrorSwing_ ||
      seq_->playing() != mirrorPlaying_ || seq_->recording() != mirrorRecording_ ||
      seq_->metronome() != mirrorMetronome_) {
    dirtyTransport_ = true;
  }
  if (seq_->pattern() != mirrorPattern_) {
    dirtyTransport_ = true;
    dirtyHeader_ = true;
    dirtySteps_ = 0xFFFF;
  }
  if (seq_->playing() != mirrorPlaying_ && !seq_->playing()) {
    // Stopped: clear the playhead highlight.
    if (mirrorPlayStep_ < Sequencer::kSteps) {
      dirtySteps_ |= bit16(mirrorPlayStep_);
    }
  }
  uint8_t playStep = seq_->playStep();
  if (seq_->playing() && playStep != mirrorPlayStep_) {
    if (mirrorPlayStep_ < Sequencer::kSteps) {
      dirtySteps_ |= bit16(mirrorPlayStep_);
    }
    dirtySteps_ |= bit16(playStep);
  }

  // Selected pad's lane and edit params (covers serial step/vel/gain/...).
  uint16_t stepMask = seq_->stepMaskForPad(selectedPad_);
  uint16_t accentMask = seq_->accentMaskForPad(selectedPad_);
  dirtySteps_ |= (stepMask ^ mirrorStepMask_) | (accentMask ^ mirrorAccentMask_);
  const PadSound &sound = bank_->pad(selectedPad_);
  if (sound.gain != mirrorGain_ || sound.pitchSemis != mirrorPitch_) {
    dirtyEdit_ = true;
  }
  if (sound.chokeGroup != mirrorChoke_) {
    dirtyEdit_ = true;
    dirtyPads_ |= bit16(selectedPad_);
  }
  if (mirrorKit_ != bank_->kitName()) {
    dirtyAll_ = true;  // kit swap re-labels everything
  }

  if (voices_ != nullptr && now - lastVoiceDrawMs_ >= 66) {
    for (uint8_t i = 0; i < VisualVoices::kVoiceCount; i++) {
      if (voices_->voice(i).level != mirrorVoiceLevels_[i]) {
        dirtyVoices_ = true;
      }
      mirrorVoiceLevels_[i] = voices_->voice(i).level;
    }
    lastVoiceDrawMs_ = now;
  }
  if (now - lastStatusDrawMs_ >= 500) {
    dirtyStatus_ = true;
    lastStatusDrawMs_ = now;
  }

  mirrorBpm_ = seq_->bpm();
  mirrorSwing_ = seq_->swing();
  mirrorPattern_ = seq_->pattern();
  mirrorPlaying_ = seq_->playing();
  mirrorRecording_ = seq_->recording();
  mirrorMetronome_ = seq_->metronome();
  mirrorPlayStep_ = playStep;
  mirrorStepMask_ = stepMask;
  mirrorAccentMask_ = accentMask;
  mirrorGain_ = sound.gain;
  mirrorPitch_ = sound.pitchSemis;
  mirrorChoke_ = sound.chokeGroup;
  mirrorKit_ = bank_->kitName();
}

// --- Rendering ---

void TuneUi::render_(uint32_t now) {
  bool any = dirtyAll_ || dirtyTransport_ || dirtyHeader_ || dirtyEdit_ ||
             dirtyVoices_ || dirtyStatus_ || dirtyPads_ != 0 || dirtySteps_ != 0;
  if (!any) {
    return;
  }
  uint32_t t0 = micros();

  if (dirtyAll_) {
    drawAll_();
    CrowDisplay::flush();
  } else {
    if (dirtyTransport_) {
      drawTransport_();
      CrowDisplay::flush(0, 0, kScreenW, kTransportH);
    }
    if (dirtyHeader_) {
      drawSeqHeader_();
      CrowDisplay::flush(kRightX, kSeqHeaderY, kRightW, kSeqHeaderH);
    }
    if (dirtyPads_ != 0) {
      for (uint8_t row = 0; row < 4; row++) {
        uint16_t rowMask = 0;
        for (uint8_t col = 0; col < 4; col++) {
          rowMask |= bit16(padIndexAt(row, col));
        }
        if ((dirtyPads_ & rowMask) == 0) {
          continue;
        }
        for (uint8_t col = 0; col < 4; col++) {
          uint8_t pad = padIndexAt(row, col);
          if (dirtyPads_ & bit16(pad)) {
            drawPad_(pad, now);
          }
        }
        CrowDisplay::flush(kPadX0, padY(row), kPadGridRight - kPadX0, kPadCellH);
      }
    }
    if (dirtySteps_ != 0) {
      for (uint8_t row = 0; row < 2; row++) {
        uint16_t rowMask = (row == 0) ? 0x00FF : 0xFF00;
        if ((dirtySteps_ & rowMask) == 0) {
          continue;
        }
        for (uint8_t step = row * 8; step < row * 8 + 8; step++) {
          if (dirtySteps_ & bit16(step)) {
            drawStepCell_(step);
          }
        }
        CrowDisplay::flush(kStepX0, stepY(row * 8), kRightW, kStepCell);
      }
    }
    if (dirtyEdit_) {
      drawEditPanel_();
      CrowDisplay::flush(kRightX, kEditY, kRightW, kEditH);
    }
    if (dirtyVoices_) {
      drawVoices_();
      CrowDisplay::flush(kRightX, kVoicesY, kRightW, kVoicesH);
    }
    if (dirtyStatus_) {
      drawStatus_();
      CrowDisplay::flush(0, kStatusY, kScreenW, kStatusH);
    }
  }

  dirtyAll_ = false;
  dirtyTransport_ = false;
  dirtyHeader_ = false;
  dirtyEdit_ = false;
  dirtyVoices_ = false;
  dirtyStatus_ = false;
  dirtyPads_ = 0;
  dirtySteps_ = 0;

  lastRenderUs_ = micros() - t0;
  if (lastRenderUs_ > maxRenderUs_) {
    maxRenderUs_ = lastRenderUs_;
  }
  renderCount_++;
}

void TuneUi::drawAll_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }
  g->fillScreen(Widgets::kBg);
  drawTransport_();
  drawSeqHeader_();
  uint32_t now = millis();
  for (uint8_t pad = 0; pad < SampleBank::kPadCount; pad++) {
    drawPad_(pad, now);
  }
  for (uint8_t step = 0; step < Sequencer::kSteps; step++) {
    drawStepCell_(step);
  }
  drawEditPanel_();
  drawVoices_();
  drawStatus_();
}

void TuneUi::drawTransport_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, 0, kScreenW, kTransportH, Widgets::kBg);

  bool playing = seq_->playing();
  bool recording = seq_->recording();
  Widgets::panel(g, kPlayX, kBtnY, kPlayW, kBtnH, 8,
                 playing ? Widgets::kGreen : Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kPlayX + kPlayW / 2, kBtnY + 15, "PLAY", Widgets::fontS(),
                playing ? Widgets::kBg : Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kStopX, kBtnY, kStopW, kBtnH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kStopX + kStopW / 2, kBtnY + 15, "STOP", Widgets::fontS(),
                Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kRecX, kBtnY, kRecW, kBtnH, 8,
                 recording ? Widgets::kRed : Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kRecX + kRecW / 2, kBtnY + 15, "REC", Widgets::fontS(),
                recording ? Widgets::kBg : Widgets::kTextHi, Widgets::kCenter);

  Widgets::panel(g, kBpmMinusX, kBtnY, kBpmMinusW, kBtnH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kBpmMinusX + kBpmMinusW / 2, kBtnY + 12, "-", Widgets::fontL(),
                Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kBpmValX, kBtnY, kBpmValW, kBtnH, 8, Widgets::kSurfaceHi, 1, Widgets::kLine);
  Widgets::text(g, kBpmValX + kBpmValW / 2, kBtnY + 12,
                String(seq_->bpm()).c_str(), Widgets::fontL(), Widgets::kAccent,
                Widgets::kCenter);
  Widgets::panel(g, kBpmPlusX, kBtnY, kBpmPlusW, kBtnH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kBpmPlusX + kBpmPlusW / 2, kBtnY + 12, "+", Widgets::fontL(),
                Widgets::kTextHi, Widgets::kCenter);

  Widgets::panel(g, kSwingMinusX, kBtnY, kSwingMinusW, kBtnH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kSwingMinusX + kSwingMinusW / 2, kBtnY + 12, "-", Widgets::fontL(),
                Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kSwingValX, kBtnY, kSwingValW, kBtnH, 8, Widgets::kSurfaceHi, 1, Widgets::kLine);
  Widgets::text(g, kSwingValX + kSwingValW / 2, kBtnY + 15,
                (String("SW ") + String(seq_->swing()) + "%").c_str(),
                Widgets::fontS(), Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kSwingPlusX, kBtnY, kSwingPlusW, kBtnH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kSwingPlusX + kSwingPlusW / 2, kBtnY + 12, "+", Widgets::fontL(),
                Widgets::kTextHi, Widgets::kCenter);

  bool metro = seq_->metronome();
  Widgets::panel(g, kMetroX, kBtnY, kMetroW, kBtnH, 8,
                 metro ? Widgets::kAccent : Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kMetroX + kMetroW / 2, kBtnY + 15, "MET", Widgets::fontS(),
                metro ? Widgets::kBg : Widgets::kTextMut, Widgets::kCenter);

  for (uint8_t i = 0; i < 4; i++) {
    bool selected = seq_->pattern() == i;
    int16_t x = kPatternX + i * kPatternPitch;
    Widgets::panel(g, x, kBtnY, kPatternW, kBtnH, 8,
                   selected ? Widgets::kAccent : Widgets::kSurface, 1, Widgets::kLine);
    char label[2] = {(char)('A' + i), '\0'};
    Widgets::text(g, x + kPatternW / 2, kBtnY + 15, label, Widgets::fontS(),
                  selected ? Widgets::kBg : Widgets::kTextMut, Widgets::kCenter);
  }
}

void TuneUi::drawSeqHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kRightX, kSeqHeaderY, kRightW, kSeqHeaderH, Widgets::kBg);
  const PadSound &sound = bank_->pad(selectedPad_);
  String title = String("STEPS - P") + (selectedPad_ + 1 < 10 ? "0" : "") +
                 String(selectedPad_ + 1) + " " + sound.label;
  Widgets::text(g, kRightX + 4, kSeqHeaderY + 10, title.c_str(), Widgets::fontS(),
                Widgets::kTextHi);
  Widgets::text(g, kRightX + kRightW - 4, kSeqHeaderY + 10,
                (String("KIT ") + bank_->kitName()).c_str(), Widgets::fontS(),
                Widgets::kTextMut, Widgets::kRight);
}

void TuneUi::drawPad_(uint8_t padIdx, uint32_t now) {
  Arduino_GFX *g = CrowDisplay::canvas();
  int16_t x = padX(padCol(padIdx));
  int16_t y = padY(padRow(padIdx));
  bool flashing = padFlashUntil_[padIdx] != 0 && (int32_t)(padFlashUntil_[padIdx] - now) > 0;
  bool selected = padIdx == selectedPad_;

  uint16_t fill = flashing ? kPadFlash : (selected ? Widgets::kSurfaceHi : Widgets::kSurface);
  uint16_t border = selected ? Widgets::kAccent : Widgets::kLine;
  g->fillRoundRect(x, y, kPadCellW, kPadCellH, 10, fill);
  g->drawRoundRect(x, y, kPadCellW, kPadCellH, 10, border);
  if (selected && !flashing) {
    g->drawRoundRect(x + 1, y + 1, kPadCellW - 2, kPadCellH - 2, 9, Widgets::kAccent);
  }

  uint16_t textMain = flashing ? Widgets::kBg : Widgets::kTextHi;
  uint16_t textMut = flashing ? Widgets::kBg : Widgets::kTextMut;
  Widgets::text(g, x + 10, y + 8, String(padIdx + 1).c_str(), Widgets::fontS(), textMut);
  const PadSound &sound = bank_->pad(padIdx);
  Widgets::text(g, x + kPadCellW / 2, y + kPadCellH - 26, sound.label,
                Widgets::fontS(), textMain, Widgets::kCenter);
  if (sound.chokeGroup != 0) {
    Widgets::text(g, x + kPadCellW - 8, y + 8,
                  (String("C") + String(sound.chokeGroup)).c_str(),
                  Widgets::fontS(), textMut, Widgets::kRight);
  }
}

void TuneUi::drawStepCell_(uint8_t step) {
  Arduino_GFX *g = CrowDisplay::canvas();
  int16_t x = stepX(step);
  int16_t y = stepY(step);
  uint8_t velocity = seq_->vel(seq_->pattern(), step, selectedPad_);
  bool playhead = seq_->playing() && seq_->playStep() == step;

  uint16_t fill;
  if (velocity >= Sequencer::kAccentThreshold) {
    fill = Widgets::kAccent;
  } else if (velocity > 0) {
    fill = kAccentDim;
  } else {
    fill = Widgets::kSurface;
  }
  g->fillRoundRect(x, y, kStepCell, kStepCell, 6, fill);
  g->drawRoundRect(x, y, kStepCell, kStepCell, 6,
                   playhead ? Widgets::kTextHi : Widgets::kLine);
  if (playhead) {
    g->drawRoundRect(x + 1, y + 1, kStepCell - 2, kStepCell - 2, 5, Widgets::kTextHi);
  }
  // Beat notch on 1/5/9/13.
  if (step % 4 == 0) {
    g->fillRect(x + 4, y + 3, 10, 3, Widgets::kAmber);
  }
  if (velocity == 0) {
    Widgets::text(g, x + kStepCell / 2, y + 20, String(step + 1).c_str(),
                  Widgets::fontS(), Widgets::kTextMut, Widgets::kCenter);
  }
}

void TuneUi::drawEditPanel_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kRightX, kEditY, kRightW, kEditH, Widgets::kBg);
  const PadSound &sound = bank_->pad(selectedPad_);

  String title = String("P") + (selectedPad_ + 1 < 10 ? "0" : "") +
                 String(selectedPad_ + 1) + " " + sound.label;
  Widgets::text(g, kRightX + 8, kEditY + 6, title.c_str(), Widgets::fontL(),
                Widgets::kTextHi);
  Widgets::text(g, kRightX + kRightW - 8, kEditY + 10, sound.sampleRef,
                Widgets::fontS(), Widgets::kTextMut, Widgets::kRight);

  // VOL slider.
  Widgets::text(g, kEditLabelX, kVolSliderY + 6, "VOL", Widgets::fontS(),
                Widgets::kTextMut);
  Widgets::hBar(g, kSliderX, kVolSliderY, kSliderW, kSliderH,
                sound.gain / 255.0f, Widgets::kAccent, Widgets::kSurface);
  Widgets::text(g, kSliderX + kSliderW - 6, kVolSliderY + 7,
                String(sound.gain).c_str(), Widgets::fontS(), Widgets::kTextHi,
                Widgets::kRight);

  // PITCH slider (bipolar, center notch).
  Widgets::text(g, kEditLabelX, kPitchSliderY + 6, "PIT", Widgets::fontS(),
                Widgets::kTextMut);
  g->fillRoundRect(kSliderX, kPitchSliderY, kSliderW, kSliderH, 6, Widgets::kSurface);
  int16_t center = kSliderX + kSliderW / 2;
  int16_t pos = center + (int16_t)((int32_t)sound.pitchSemis * (kSliderW / 2) / 12);
  if (sound.pitchSemis >= 0) {
    g->fillRect(center, kPitchSliderY + 4, pos - center, kSliderH - 8, Widgets::kAccent);
  } else {
    g->fillRect(pos, kPitchSliderY + 4, center - pos, kSliderH - 8, Widgets::kAmber);
  }
  g->fillRect(center - 1, kPitchSliderY + 2, 2, kSliderH - 4, Widgets::kTextHi);
  String pitchText = (sound.pitchSemis > 0 ? "+" : "") + String(sound.pitchSemis);
  Widgets::text(g, kSliderX + kSliderW - 6, kPitchSliderY + 7, pitchText.c_str(),
                Widgets::fontS(), Widgets::kTextHi, Widgets::kRight);

  // Choke group chips.
  Widgets::text(g, kEditLabelX, kChokeY + 12, "CHK", Widgets::fontS(),
                Widgets::kTextMut);
  for (uint8_t i = 0; i < 5; i++) {
    bool selected = sound.chokeGroup == i;
    int16_t x = kChokeX0 + i * kChokePitch;
    Widgets::panel(g, x, kChokeY, kChokeW, kChokeH, 8,
                   selected ? Widgets::kAccent : Widgets::kSurface, 1, Widgets::kLine);
    Widgets::text(g, x + kChokeW / 2, kChokeY + 12, chokeLabel(i), Widgets::fontS(),
                  selected ? Widgets::kBg : Widgets::kTextMut, Widgets::kCenter);
  }

  // Kit selector.
  Widgets::text(g, kEditLabelX, kKitY + 12, "KIT", Widgets::fontS(), Widgets::kTextMut);
  Widgets::panel(g, kKitPrevX, kKitY, kKitArrowW, kKitH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kKitPrevX + kKitArrowW / 2, kKitY + 9, "<", Widgets::fontL(),
                kitStep_ ? Widgets::kTextHi : Widgets::kLine, Widgets::kCenter);
  Widgets::text(g, (kKitPrevX + kKitArrowW + kKitNextX) / 2, kKitY + 10,
                bank_->kitName(), Widgets::fontM(), Widgets::kTextHi, Widgets::kCenter);
  Widgets::panel(g, kKitNextX, kKitY, kKitArrowW, kKitH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kKitNextX + kKitArrowW / 2, kKitY + 9, ">", Widgets::fontL(),
                kitStep_ ? Widgets::kTextHi : Widgets::kLine, Widgets::kCenter);
}

void TuneUi::drawVoices_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kRightX, kVoicesY, kRightW, kVoicesH, Widgets::kBg);
  Widgets::text(g, kRightX + 8, kVoicesY + 6, "VOICES", Widgets::fontS(),
                Widgets::kTextMut);
  if (voices_ == nullptr) {
    return;
  }
  int16_t barBottom = kVoicesY + kVoicesH - 14;
  int16_t barMaxH = 70;
  for (uint8_t i = 0; i < VisualVoices::kVoiceCount; i++) {
    int16_t x = kRightX + 130 + i * 74;
    g->fillRect(x, barBottom - barMaxH, 40, barMaxH, Widgets::kSurface);
    int16_t h = (int16_t)((int32_t)voices_->voice(i).level * barMaxH / 127);
    if (h > 0) {
      g->fillRect(x, barBottom - h, 40, h, Widgets::kAccent);
    }
  }
  const char *label = voices_->primaryLabel();
  Widgets::text(g, kRightX + 8, kVoicesY + kVoicesH - 26, label,
                Widgets::fontS(), Widgets::kTextHi);
}

void TuneUi::drawStatus_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, kStatusY, kScreenW, kStatusH, Widgets::kBg);
  g->drawFastHLine(0, kStatusY, kScreenW, Widgets::kLine);
  String engine = audioStatus_ != nullptr ? audioStatus_(ctx_) : String("audio n/a");
  Widgets::text(g, 8, kStatusY + 12, engine.c_str(), Widgets::fontS(), Widgets::kTextMut);
  String mem = String("heap ") + String(ESP.getFreeHeap() / 1024) + "K psram " +
               String(ESP.getFreePsram() / 1024 / 1024) + "M";
  Widgets::text(g, kScreenW / 2, kStatusY + 12, mem.c_str(), Widgets::fontS(),
                Widgets::kTextMut, Widgets::kCenter);
  String perf = String("ui ") + String(lastRenderUs_ / 1000.0f, 1) + "ms";
  Widgets::text(g, kScreenW - 8, kStatusY + 12, perf.c_str(), Widgets::fontS(),
                Widgets::kTextMut, Widgets::kRight);
}

#else  // headless / non-P4: keep the class alive with no-op behavior

void TuneUi::begin(SampleBank *bank, Sequencer *seq, VisualVoices *voices,
                   TriggerFn trigger, TransportFn transport,
                   AudioStatusFn audioStatus, KitStepFn kitStep, void *ctx) {
  bank_ = bank;
  seq_ = seq;
  voices_ = voices;
  trigger_ = trigger;
  transport_ = transport;
  audioStatus_ = audioStatus;
  kitStep_ = kitStep;
  ctx_ = ctx;
  displayReady_ = false;
}

void TuneUi::tick() {}

void TuneUi::notePadFlash(uint8_t padIdx) {
  if (padIdx < SampleBank::kPadCount) {
    padFlashUntil_[padIdx] = millis();
  }
}

void TuneUi::selectPad(uint8_t padIdx) {
  if (padIdx < SampleBank::kPadCount) {
    selectedPad_ = padIdx;
  }
}

String TuneUi::perfLine() const { return String("display off (USE_DISPLAY=0)"); }
String TuneUi::touchLine() const { return String("touch off (USE_DISPLAY=0)"); }

#endif
