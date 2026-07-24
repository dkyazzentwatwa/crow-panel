#ifndef CYPHER_TUNE_TUNE_UI_H
#define CYPHER_TUNE_TUNE_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SampleBank.h"
#include "Sequencer.h"
#include "TouchTracker.h"
#include "VisualVoices.h"

// The MPC performance screen: 4x4 pad grid, TR-style step lane for the
// selected pad, transport bar, and an always-visible pad-edit panel.
// Rendering is dirty-region + manual flush (project 21's pattern); pads and
// step cells redraw as solid cells in final state, so no offscreen canvas is
// needed and internal SRAM stays free for the audio engine.
//
// The class exists in every build; all rendering/touch is compiled only for
// USE_DISPLAY on the P4, so the baseline serial build stays green with a
// no-op tick(). Engine access goes through the callbacks wired in begin() -
// the UI never includes audio headers.
class TuneUi {
 public:
  // Fired when a pad is played from the touch surface. The sketch routes
  // this into the audio/record path exactly like a serial `pad` command.
  typedef void (*TriggerFn)(void *ctx, uint8_t padIdx, uint8_t velocity);
  // Transport actions route through the sketch too (in audio builds they
  // become engine commands; BPM/swing/pattern/metronome are direct
  // byte-atomic sequencer writes and need no indirection).
  enum TransportOp : uint8_t { kOpPlay, kOpStop, kOpRecordToggle };
  typedef void (*TransportFn)(void *ctx, uint8_t op);
  // Short engine status for the footer strip ("i2s 22k v3" / "mock").
  typedef String (*AudioStatusFn)(void *ctx);
  // Kit prev/next arrows (dir -1/+1); null until SD kits are wired.
  typedef void (*KitStepFn)(void *ctx, int8_t dir);

  void begin(SampleBank *bank, Sequencer *seq, VisualVoices *voices,
             TriggerFn trigger, TransportFn transport, AudioStatusFn audioStatus,
             KitStepFn kitStep, void *ctx);
  void tick();  // touch -> actions -> state diff -> dirty-region render

  // Light a pad from the sequencer/audio side (step fires, serial pads).
  void notePadFlash(uint8_t padIdx);
  void selectPad(uint8_t padIdx);
  uint8_t selectedPad() const { return selectedPad_; }
  bool displayReady() const { return displayReady_; }

  String perfLine() const;   // serial `perf`
  String touchLine() const;  // serial `touch`

 private:
  SampleBank *bank_ = nullptr;
  Sequencer *seq_ = nullptr;
  VisualVoices *voices_ = nullptr;
  TriggerFn trigger_ = nullptr;
  TransportFn transport_ = nullptr;
  AudioStatusFn audioStatus_ = nullptr;
  KitStepFn kitStep_ = nullptr;
  void *ctx_ = nullptr;

  bool displayReady_ = false;
  uint8_t selectedPad_ = 0;
  uint32_t padFlashUntil_[SampleBank::kPadCount] = {0};

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void handleTouch_(uint32_t now);
  void pressControl_(TouchTracker::Contact &c, uint32_t now);
  void holdControl_(TouchTracker::Contact &c, uint32_t now);
  void bumpBpm_(int8_t dir);
  void bumpSwing_(int8_t dir);
  void syncState_(uint32_t now);
  void render_(uint32_t now);

  void drawAll_();
  void drawTransport_();
  void drawSeqHeader_();
  void drawPad_(uint8_t padIdx, uint32_t now);
  void drawStepCell_(uint8_t step);
  void drawEditPanel_();
  void drawVoices_();
  void drawStatus_();

  TouchTracker touch_;

  // Dirty flags + per-cell masks (bit i = pad/step i).
  bool dirtyAll_ = true;
  bool dirtyTransport_ = false;
  bool dirtyHeader_ = false;
  bool dirtyEdit_ = false;
  bool dirtyVoices_ = false;
  bool dirtyStatus_ = false;
  uint16_t dirtyPads_ = 0;
  uint16_t dirtySteps_ = 0;

  // Mirrors for change detection (serial edits reach the screen for free).
  uint16_t mirrorBpm_ = 0;
  uint8_t mirrorSwing_ = 0;
  uint8_t mirrorPattern_ = 0;
  bool mirrorPlaying_ = false;
  bool mirrorRecording_ = false;
  bool mirrorMetronome_ = false;
  uint8_t mirrorPlayStep_ = 0xFF;
  uint16_t mirrorStepMask_ = 0;
  uint16_t mirrorAccentMask_ = 0;
  uint8_t mirrorGain_ = 0;
  int8_t mirrorPitch_ = 0;
  uint8_t mirrorChoke_ = 0;
  String mirrorKit_;
  uint8_t mirrorVoiceLevels_[4] = {0};

  uint32_t lastVoiceDrawMs_ = 0;
  uint32_t lastStatusDrawMs_ = 0;
  uint32_t lastSliderDrawMs_ = 0;

  // Rolling render cost (µs) for the `perf` command.
  uint32_t lastRenderUs_ = 0;
  uint32_t maxRenderUs_ = 0;
  uint32_t renderCount_ = 0;
#endif
};

#endif
