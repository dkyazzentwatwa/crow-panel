#include "TuneUi.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
#include "UiLayout.h"

namespace {

using namespace UiLayout;

constexpr uint32_t kFlashMs = 140;   // pad hit envelope (velocity-scaled)
constexpr uint8_t kTrailLen = 3;     // step cells lit behind the playhead
constexpr uint16_t kScopeSamples = 256;

inline uint16_t bit16(uint8_t i) { return (uint16_t)1 << i; }

// Mix two RGB565 colors: t=0 gives a, t=255 gives b. Channels are blended in
// their native widths (5/6/5), which is exact enough for fades and far cheaper
// than converting to 8-bit RGB and back.
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  int16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int16_t r = ar + (((br - ar) * t) >> 8);
  int16_t g = ag + (((bg - ag) * t) >> 8);
  int16_t bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

const char *chokeLabel(uint8_t group) {
  static const char *kLabels[5] = {"OFF", "1", "2", "3", "4"};
  return kLabels[group <= 4 ? group : 0];
}

}  // namespace

void TuneUi::begin(SampleBank *bank, Sequencer *seq, VisualVoices *voices,
                   const Callbacks &callbacks) {
  bank_ = bank;
  seq_ = seq;
  voices_ = voices;
  cb_ = callbacks;
  loadSettings_();
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHER TUNE",
                                     /*manualFlush=*/true);
  applyBrightness_();
  dirtyAll_ = true;
}

void TuneUi::tick() {
  if (!displayReady_ || seq_ == nullptr || bank_ == nullptr) {
    return;
  }
  uint32_t now = millis();
  CrowDisplay::tick();
  handleTouch_(now);
  tickIdle_(now);
  if (view_ == kViewMain) {
    syncState_(now);
  }
  render_(now);
}

void TuneUi::notePadFlash(uint8_t padIdx, uint8_t velocity) {
  if (padIdx >= SampleBank::kPadCount) {
    return;
  }
  padFlashUntil_[padIdx] = millis() + kFlashMs;
  padFlashVel_[padIdx] = velocity > 127 ? 127 : (velocity < 8 ? 8 : velocity);
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
      noteActivity();
      // Dispatch by view: the two screens share no controls, so routing here
      // means a stale pad rect can never fire while settings is up.
      if (view_ == kViewSettings) {
        c.owner = hitTestSettings(c.downX, c.downY);
        pressSettingsControl_(c, now);
      } else if (view_ == kViewLoops) {
        c.owner = hitTestLoops(c.downX, c.downY);
        pressLoopsControl_(c, now);
      } else {
        c.owner = hitTest(c.downX, c.downY);
        pressControl_(c, now);
      }
    } else if (c.active && c.owner != kControlNone) {
      if (view_ != kViewMain) {
        // Only the steppers repeat, and they are cheap enough to re-press.
        continue;
      }
      holdControl_(c, now);
    }
  }
}

void TuneUi::pressSettingsControl_(TouchTracker::Contact &c, uint32_t now) {
  (void)now;
  switch (c.owner) {
    case kControlBack:
      setView(kViewMain);
      break;
    case kControlBrightMinus:
      bumpBrightness(-(int16_t)kBrightnessStep);
      dirtySettingsRows_ |= bit16(kSetRowBrightness);
      break;
    case kControlBrightPlus:
      bumpBrightness(kBrightnessStep);
      dirtySettingsRows_ |= bit16(kSetRowBrightness);
      break;
    case kControlVolumeMinus:
    case kControlVolumePlus:
      if (cb_.volumeGet != nullptr && cb_.volumeSet != nullptr) {
        int16_t v = (int16_t)cb_.volumeGet(cb_.ctx) +
                    (c.owner == kControlVolumePlus ? 16 : -16);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        cb_.volumeSet(cb_.ctx, (uint8_t)v);
      }
      dirtySettingsRows_ |= bit16(kSetRowVolume);
      break;
    case kControlLoopVolMinus:
    case kControlLoopVolPlus:
      if (cb_.loopVolGet != nullptr && cb_.loopVolSet != nullptr) {
        int16_t v = (int16_t)cb_.loopVolGet(cb_.ctx) +
                    (c.owner == kControlLoopVolPlus ? 16 : -16);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        cb_.loopVolSet(cb_.ctx, (uint8_t)v);
      }
      dirtySettingsRows_ |= bit16(kSetRowLoopVol);
      break;
    case kControlThemePrev:
    case kControlThemeNext: {
      uint8_t count = tuneThemeCount();
      uint8_t next = (uint8_t)((themeIndex_ + count +
                                (c.owner == kControlThemeNext ? 1 : -1)) % count);
      setThemeByName(tuneTheme(next).name);  // persists + forces a full repaint
      break;
    }
    case kControlKitPrev:
    case kControlKitNext:
      if (cb_.kitStep != nullptr) {
        cb_.kitStep(cb_.ctx, c.owner == kControlKitNext ? 1 : -1);
      }
      dirtySettingsRows_ |= bit16(kSetRowKit);
      break;
    case kControlIdleDim:
      setIdleDim(!idleDimEnabled_);
      dirtySettingsRows_ |= bit16(kSetRowIdleDim);
      break;
    default:
      break;
  }
}

void TuneUi::tickIdle_(uint32_t now) {
  if (!idleDimEnabled_ || dimmed_) {
    return;
  }
  // Never dim while the transport runs: that is exactly when you are listening
  // to a loop rather than touching the panel.
  if (seq_ != nullptr && seq_->playing()) {
    lastActivityMs_ = now;
    return;
  }
  if ((uint32_t)(now - lastActivityMs_) < CYPHER_TUNE_IDLE_DIM_MS) {
    return;
  }
  dimmed_ = true;
  applyBrightness_();
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
    if (cb_.trigger != nullptr) {
      cb_.trigger(cb_.ctx, pad, velocity);
    }
    notePadFlash(pad, velocity);
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
      if (cb_.transport != nullptr) cb_.transport(cb_.ctx, kOpPlay);
      dirtyTransport_ = true;
      break;
    case kControlStop:
      if (cb_.transport != nullptr) cb_.transport(cb_.ctx, kOpStop);
      dirtyTransport_ = true;
      break;
    case kControlRec:
      if (cb_.transport != nullptr) cb_.transport(cb_.ctx, kOpRecordToggle);
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
      c.ownerPad = (int8_t)selectedPad_;  // latch: a later pad tap must not steal this drag
      bank_->setGain(selectedPad_, (uint8_t)sliderValue(c.downX, 255));
      dirtyEdit_ = true;
      break;
    case kControlPitchSlider:
      c.ownerPad = (int8_t)selectedPad_;
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
      if (cb_.kitStep != nullptr) {
        cb_.kitStep(cb_.ctx, owner == kControlKitNext ? 1 : -1);
      }
      break;
    case kControlLoopPrev:
    case kControlLoopNext: {
      uint8_t total = LoopLibrary::count();
      if (total == 0 || cb_.loopSelect == nullptr || cb_.loopCurrent == nullptr) {
        break;
      }
      // Walk the whole catalog, with "none" sitting just off the front.
      int16_t next = cb_.loopCurrent(cb_.ctx) +
                     (owner == kControlLoopNext ? 1 : -1);
      if (next < -1) next = (int16_t)total - 1;
      if (next >= (int16_t)total) next = -1;
      cb_.loopSelect(cb_.ctx, (int8_t)next);
      // Loading a loop relocks the tempo, so the transport readout changes too.
      dirtyEdit_ = true;
      dirtyTransport_ = true;
      break;
    }
    case kControlOpenLoops:
      setView(kViewLoops);
      break;
    case kControlOpenSettings:
      setView(kViewSettings);
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
    case kControlVolSlider: {
      // Write the pad latched at press, not selectedPad_ - see Contact::ownerPad.
      uint8_t target = (c.ownerPad >= 0) ? (uint8_t)c.ownerPad : selectedPad_;
      bank_->setGain(target, (uint8_t)sliderValue(c.x, 255));
      if (now - lastSliderDrawMs_ >= 33) {
        lastSliderDrawMs_ = now;
        // Only repaint the edit panel when it is showing the pad being edited;
        // a drag on a pad that is no longer selected still takes effect, it
        // just has nothing on screen to update.
        if (target == selectedPad_) dirtyEdit_ = true;
      }
      break;
    }
    case kControlPitchSlider: {
      uint8_t target = (c.ownerPad >= 0) ? (uint8_t)c.ownerPad : selectedPad_;
      bank_->setPitch(target, (int8_t)(sliderValue(c.x, 24) - 12));
      if (now - lastSliderDrawMs_ >= 33) {
        lastSliderDrawMs_ = now;
        if (target == selectedPad_) dirtyEdit_ = true;
      }
      break;
    }
    default:
      break;
  }
}

void TuneUi::bumpBpm_(int8_t dir) {
  if (seq_ != nullptr && seq_->locked()) {
    return;  // the loop owns the clock; nudging BPM would only break sync
  }
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
  // Flash envelopes: repaint every frame a pad is still decaying (that is what
  // makes the fade visible), and once more when it expires to settle at idle.
  for (uint8_t i = 0; i < SampleBank::kPadCount; i++) {
    if (padFlashUntil_[i] == 0) {
      continue;
    }
    if ((int32_t)(now - padFlashUntil_[i]) >= 0) {
      padFlashUntil_[i] = 0;
      padFlashVel_[i] = 0;
    }
    dirtyPads_ |= bit16(i);
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
    // Repaint the new head plus the whole tail behind it (and one past the
    // tail, which has just gone dark) so the trail fades cleanly.
    for (uint8_t back = 0; back <= kTrailLen + 1; back++) {
      dirtySteps_ |= bit16((playStep + Sequencer::kSteps - back) % Sequencer::kSteps);
      dirtySteps_ |= bit16((mirrorPlayStep_ + Sequencer::kSteps - back) % Sequencer::kSteps);
    }
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

  // Scope: a live trace has to repaint on a clock, not on a change signal.
  // ~25 fps costs one 108-row band flush and keeps the waveform fluid. Silent
  // builds keep the old change-driven meter refresh instead.
  if (cb_.scope != nullptr) {
    if (now - lastScopeDrawMs_ >= 40) {
      dirtyScope_ = true;
      lastScopeDrawMs_ = now;
    }
  } else if (voices_ != nullptr && now - lastScopeDrawMs_ >= 66) {
    for (uint8_t i = 0; i < VisualVoices::kVoiceCount; i++) {
      if (voices_->voice(i).level != mirrorVoiceLevels_[i]) {
        dirtyScope_ = true;
      }
      mirrorVoiceLevels_[i] = voices_->voice(i).level;
    }
    lastScopeDrawMs_ = now;
  }
  if (now - lastStatusDrawMs_ >= 500) {
    dirtyStatus_ = true;
    lastStatusDrawMs_ = now;
  }
  // Contact count changes need to show immediately, not on the 500 ms tick,
  // or the multi-touch readout would miss every drum hit.
  uint8_t contacts = touch_.activeCount();
  if (contacts != mirrorContacts_) {
    mirrorContacts_ = contacts;
    dirtyStatus_ = true;
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
  if (view_ == kViewLoops) {
    // The browser is static between taps, so it only ever needs a full paint.
    if (!dirtyAll_) {
      return;
    }
    uint32_t tL = micros();
    drawLoops_();
    CrowDisplay::flush();
    dirtyAll_ = false;
    lastRenderUs_ = micros() - tL;
    renderCount_++;
    return;
  }
  if (view_ == kViewSettings) {
    if (!dirtyAll_ && dirtySettingsRows_ == 0) {
      return;
    }
    uint32_t tS = micros();
    if (dirtyAll_) {
      drawSettings_();
      CrowDisplay::flush();
    } else {
      Arduino_GFX *g = CrowDisplay::canvas();
      const TuneTheme &t = theme();
      for (uint8_t row = 0; row < kSetRowCount; row++) {
        if (dirtySettingsRows_ & bit16(row)) {
          drawSettingsRow_(g, t, row);
          CrowDisplay::flush(0, setRowY(row), kScreenW, kSetRowH);
        }
      }
    }
    dirtyAll_ = false;
    dirtySettingsRows_ = 0;
    lastRenderUs_ = micros() - tS;
    renderCount_++;
    return;
  }

  bool any = dirtyAll_ || dirtyTransport_ || dirtyHeader_ || dirtyEdit_ ||
             dirtyScope_ || dirtyStatus_ || dirtyPads_ != 0 || dirtySteps_ != 0;
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
    if (dirtyScope_) {
      drawScope_();
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
  dirtyScope_ = false;
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
  const TuneTheme &t = theme();
  if (g == nullptr) {
    return;
  }
  g->fillScreen(t.bg);
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
  drawScope_();
  drawStatus_();
}

void TuneUi::drawSettingsRow_(Arduino_GFX *g, const TuneTheme &t, uint8_t row) {
  int16_t y = setRowY(row);
  g->fillRect(0, y, kScreenW, kSetRowH, t.bg);

  const char *label = "";
  switch (row) {
    case kSetRowBrightness: label = "BRIGHTNESS"; break;
    case kSetRowVolume:     label = "MASTER VOL"; break;
    case kSetRowLoopVol:    label = "LOOP VOL"; break;
    case kSetRowTheme:      label = "THEME"; break;
    case kSetRowKit:        label = "KIT"; break;
    case kSetRowIdleDim:    label = "IDLE DIM"; break;
    default: break;
  }
  Widgets::text(g, kSetLabelX, y + 18, label, Widgets::fontM(), t.ink);

  bool stepper = row != kSetRowIdleDim;
  bool arrows = row == kSetRowTheme || row == kSetRowKit;
  if (stepper) {
    Widgets::panel(g, kSetMinusX, y, kSetStepW, kSetRowH, 8, t.surface, 1, t.line);
    Widgets::text(g, kSetMinusX + kSetStepW / 2, y + 12, arrows ? "<" : "-",
                  Widgets::fontL(), t.ink, Widgets::kCenter);
    Widgets::panel(g, kSetPlusX, y, kSetStepW, kSetRowH, 8, t.surface, 1, t.line);
    Widgets::text(g, kSetPlusX + kSetStepW / 2, y + 12, arrows ? ">" : "+",
                  Widgets::fontL(), t.ink, Widgets::kCenter);
  }

  switch (row) {
    case kSetRowBrightness: {
      // The bar spans the usable range (kMinBrightness..255), not 0..255, so a
      // full-left bar still matches the dimmest the panel actually goes.
      float frac = (float)(brightness_ - kMinBrightness) / (255 - kMinBrightness);
      Widgets::hBar(g, kSetBarX, y + 12, kSetBarW, 28, frac, t.accent, t.surface);
      Widgets::text(g, kSetValueX, y + 18, String(brightness_).c_str(),
                    Widgets::fontM(), t.ink, Widgets::kRight);
      break;
    }
    case kSetRowVolume: {
      uint8_t vol = cb_.volumeGet != nullptr ? cb_.volumeGet(cb_.ctx) : 0;
      Widgets::hBar(g, kSetBarX, y + 12, kSetBarW, 28, vol / 255.0f, t.accent,
                    t.surface);
      Widgets::text(g, kSetValueX, y + 18,
                    cb_.volumeGet != nullptr ? String(vol).c_str() : "n/a",
                    Widgets::fontM(), t.ink, Widgets::kRight);
      break;
    }
    case kSetRowLoopVol: {
      uint8_t vol = cb_.loopVolGet != nullptr ? cb_.loopVolGet(cb_.ctx) : 0;
      Widgets::hBar(g, kSetBarX, y + 12, kSetBarW, 28, vol / 255.0f, t.good,
                    t.surface);
      Widgets::text(g, kSetValueX, y + 18,
                    cb_.loopVolGet != nullptr ? String(vol).c_str() : "n/a",
                    Widgets::fontM(), t.ink, Widgets::kRight);
      break;
    }
    case kSetRowTheme:
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8, t.surface, 1, t.line);
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16, theme().name,
                    Widgets::fontM(), t.accent, Widgets::kCenter);
      Widgets::text(g, kSetValueX, y + 18,
                    (String(themeIndex_ + 1) + "/" + String(tuneThemeCount())).c_str(),
                    Widgets::fontS(), t.muted, Widgets::kRight);
      break;
    case kSetRowKit:
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8, t.surface, 1, t.line);
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16,
                    bank_ != nullptr ? bank_->kitName() : "-", Widgets::fontM(),
                    t.accent, Widgets::kCenter);
      break;
    case kSetRowIdleDim: {
      uint16_t fill = idleDimEnabled_ ? t.good : t.surface;
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8, fill, 1, t.line);
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16,
                    idleDimEnabled_ ? "ON - dims when stopped" : "OFF",
                    Widgets::fontM(), idleDimEnabled_ ? t.onAccent : t.muted,
                    Widgets::kCenter);
      break;
    }
    default:
      break;
  }
}

void TuneUi::drawSettings_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }
  const TuneTheme &t = theme();
  g->fillScreen(t.bg);

  Widgets::panel(g, kSetBackX, kSetBackY, kSetBackW, kSetBackH, 8, t.surface, 1,
                 t.line);
  Widgets::text(g, kSetBackX + kSetBackW / 2, kSetBackY + 11, "< BACK",
                Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::text(g, kScreenW / 2, kSetBackY + 6, "SETTINGS", Widgets::fontL(),
                t.ink, Widgets::kCenter);
  g->drawFastHLine(0, kSetHeaderH, kScreenW, t.line);

  for (uint8_t row = 0; row < kSetRowCount; row++) {
    drawSettingsRow_(g, t, row);
  }

  // Read-only engine/system block: the numbers worth seeing when something
  // sounds wrong, in one place instead of squeezed into the status strip.
  g->drawFastHLine(0, kSetInfoY - 16, kScreenW, t.line);
  Widgets::text(g, kSetLabelX, kSetInfoY, "ENGINE", Widgets::fontS(), t.muted);
  String engine = cb_.audioStatus != nullptr ? cb_.audioStatus(cb_.ctx)
                                             : String("audio n/a");
  Widgets::text(g, kSetLabelX, kSetInfoY + 26, engine.c_str(), Widgets::fontM(),
                t.ink);
  String mem = String("heap ") + String(ESP.getFreeHeap() / 1024) + "K   psram " +
               String(ESP.getFreePsram() / 1024 / 1024) + "M   ui " +
               String(lastRenderUs_ / 1000.0f, 1) + "ms";
  Widgets::text(g, kSetLabelX, kSetInfoY + 58, mem.c_str(), Widgets::fontS(),
                t.muted);
  Widgets::text(g, kScreenW - kSetLabelX, kSetInfoY + 58,
                "pads / steps / transport are on the main screen",
                Widgets::fontS(), t.muted, Widgets::kRight);
}

void TuneUi::drawLoops_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }
  const TuneTheme &t = theme();
  g->fillScreen(t.bg);

  Widgets::panel(g, kSetBackX, kSetBackY, kSetBackW, kSetBackH, 8, t.surface, 1,
                 t.line);
  Widgets::text(g, kSetBackX + kSetBackW / 2, kSetBackY + 11, "< BACK",
                Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::text(g, kScreenW / 2, kSetBackY + 6, "LOOPS", Widgets::fontL(), t.ink,
                Widgets::kCenter);
  g->drawFastHLine(0, kSetHeaderH, kScreenW, t.line);

  uint8_t packs = LoopLibrary::packCount();
  int8_t current = cb_.loopCurrent != nullptr ? cb_.loopCurrent(cb_.ctx) : -1;
  if (browsePack_ >= packs && packs > 0) {
    browsePack_ = 0;
  }

  if (packs == 0) {
    Widgets::text(g, kScreenW / 2, 250, "No loop packs found on SD",
                  Widgets::fontL(), t.muted, Widgets::kCenter);
    Widgets::text(g, kScreenW / 2, 290,
                  "expected /mpc/loops/<pack>/loops.txt", Widgets::fontS(),
                  t.muted, Widgets::kCenter);
    return;
  }

  // Pack column.
  for (uint8_t i = 0; i < packs && i < kLoopPackRows; i++) {
    const LoopLibrary::PackInfo &pk = LoopLibrary::pack(i);
    bool sel = i == browsePack_;
    bool holdsCurrent = current >= 0 &&
                        LoopLibrary::info((uint8_t)current).pack == i;
    Widgets::panel(g, kLoopPackX, loopPackY(i), kLoopPackW, kLoopPackH, 8,
                   sel ? t.surfaceHi : t.surface, sel ? 2 : 1,
                   sel ? t.accent : t.line);
    Widgets::text(g, kLoopPackX + 12, loopPackY(i) + 16, pk.title,
                  Widgets::fontM(), sel ? t.ink : t.muted);
    // A dot marks the pack the playing loop came from, so you can always find
    // your way back to it.
    Widgets::text(g, kLoopPackX + kLoopPackW - 12, loopPackY(i) + 18,
                  (String(holdsCurrent ? "* " : "") + String(pk.count)).c_str(),
                  Widgets::fontS(), holdsCurrent ? t.good : t.muted,
                  Widgets::kRight);
  }

  // Loop cells for the selected pack.
  const LoopLibrary::PackInfo &pk = LoopLibrary::pack(browsePack_);
  for (uint8_t s = 0; s < kLoopCellsPerPage; s++) {
    int16_t x = loopCellX(s);
    int16_t y = loopCellY(s);
    int8_t idx = LoopLibrary::loopInPack(browsePack_, s);
    if (idx < 0) {
      continue;  // pack has fewer loops than the grid has cells
    }
    const LoopLibrary::LoopInfo &l = LoopLibrary::info((uint8_t)idx);
    bool active = idx == current;
    Widgets::panel(g, x, y, kLoopCellW, kLoopCellH, 10,
                   active ? t.accentDim : t.surface, active ? 2 : 1,
                   active ? t.accent : t.line);
    Widgets::text(g, x + 12, y + 12, l.title, Widgets::fontM(),
                  active ? t.ink : t.ink);
    String sub = String(l.bpmTenths / 10.0f, 1) + " BPM   " + String(l.bars) +
                 " bars   " + String(l.frames / (float)l.srcRate, 0) + "s";
    Widgets::text(g, x + 12, y + 44, sub.c_str(), Widgets::fontS(), t.muted);
    if (active) {
      Widgets::text(g, x + kLoopCellW - 12, y + 44, "PLAYING", Widgets::fontS(),
                    t.good, Widgets::kRight);
    }
  }
  (void)pk;

  Widgets::panel(g, kLoopNoneX, kLoopNoneY, kLoopNoneW, kLoopNoneH, 8,
                 current < 0 ? t.accentDim : t.surface, 1, t.line);
  Widgets::text(g, kLoopNoneX + kLoopNoneW / 2, kLoopNoneY + 14, "NO LOOP",
                Widgets::fontM(), current < 0 ? t.ink : t.muted, Widgets::kCenter);
  String status = current >= 0
                      ? String("playing: ") + LoopLibrary::info((uint8_t)current).title
                      : String("tempo unlocked - BPM buttons active");
  Widgets::text(g, kLoopNoneX + kLoopNoneW + 24, kLoopNoneY + 14, status.c_str(),
                Widgets::fontS(), t.muted);
}

void TuneUi::pressLoopsControl_(TouchTracker::Contact &c, uint32_t now) {
  (void)now;
  int16_t owner = c.owner;
  if (owner == kControlBack) {
    setView(kViewMain);
    return;
  }
  if (owner == kControlLoopNone) {
    if (cb_.loopSelect != nullptr) {
      cb_.loopSelect(cb_.ctx, -1);
    }
    dirtyAll_ = true;
    return;
  }
  if (owner >= kLoopPackBase && owner < kLoopPackBase + kLoopPackRows) {
    uint8_t pack = (uint8_t)(owner - kLoopPackBase);
    if (pack < LoopLibrary::packCount() && pack != browsePack_) {
      browsePack_ = pack;
      dirtyAll_ = true;  // the whole right-hand grid changes
    }
    return;
  }
  if (owner >= kLoopCellBase && owner < kLoopCellBase + kLoopCellsPerPage) {
    int8_t idx = LoopLibrary::loopInPack(browsePack_,
                                         (uint8_t)(owner - kLoopCellBase));
    if (idx >= 0 && cb_.loopSelect != nullptr) {
      cb_.loopSelect(cb_.ctx, idx);
      dirtyAll_ = true;
    }
  }
}

void TuneUi::drawTransport_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  g->fillRect(0, 0, kScreenW, kTransportH, t.bg);

  bool playing = seq_->playing();
  bool recording = seq_->recording();
  Widgets::panel(g, kPlayX, kBtnY, kPlayW, kBtnH, 8,
                 playing ? t.good : t.surface, 1, t.line);
  Widgets::text(g, kPlayX + kPlayW / 2, kBtnY + 15, "PLAY", Widgets::fontS(),
                playing ? t.onAccent : t.ink, Widgets::kCenter);
  Widgets::panel(g, kStopX, kBtnY, kStopW, kBtnH, 8, t.surface, 1, t.line);
  Widgets::text(g, kStopX + kStopW / 2, kBtnY + 15, "STOP", Widgets::fontS(),
                t.ink, Widgets::kCenter);
  Widgets::panel(g, kRecX, kBtnY, kRecW, kBtnH, 8,
                 recording ? t.danger : t.surface, 1, t.line);
  Widgets::text(g, kRecX + kRecW / 2, kBtnY + 15, "REC", Widgets::fontS(),
                recording ? t.onAccent : t.ink, Widgets::kCenter);

  Widgets::panel(g, kBpmMinusX, kBtnY, kBpmMinusW, kBtnH, 8, t.surface, 1, t.line);
  Widgets::text(g, kBpmMinusX + kBpmMinusW / 2, kBtnY + 12, "-", Widgets::fontL(),
                t.ink, Widgets::kCenter);
  Widgets::panel(g, kBpmValX, kBtnY, kBpmValW, kBtnH, 8, t.surfaceHi, 1, t.line);
  // While a loop owns the clock the real tempo is fractional, so show one
  // decimal and mark it locked - the +/- buttons cannot move it.
  if (seq_->locked()) {
    uint16_t tenths = seq_->effectiveBpmTenths(CYPHER_TUNE_ENGINE_RATE);
    Widgets::text(g, kBpmValX + kBpmValW / 2, kBtnY + 8,
                  String(tenths / 10.0f, 1).c_str(), Widgets::fontM(), t.good,
                  Widgets::kCenter);
    Widgets::text(g, kBpmValX + kBpmValW / 2, kBtnY + 32, "LOCK",
                  Widgets::fontS(), t.muted, Widgets::kCenter);
  } else {
    Widgets::text(g, kBpmValX + kBpmValW / 2, kBtnY + 12,
                  String(seq_->bpm()).c_str(), Widgets::fontL(), t.accent,
                  Widgets::kCenter);
  }
  Widgets::panel(g, kBpmPlusX, kBtnY, kBpmPlusW, kBtnH, 8, t.surface, 1, t.line);
  Widgets::text(g, kBpmPlusX + kBpmPlusW / 2, kBtnY + 12, "+", Widgets::fontL(),
                t.ink, Widgets::kCenter);

  Widgets::panel(g, kSwingMinusX, kBtnY, kSwingMinusW, kBtnH, 8, t.surface, 1, t.line);
  Widgets::text(g, kSwingMinusX + kSwingMinusW / 2, kBtnY + 12, "-", Widgets::fontL(),
                t.ink, Widgets::kCenter);
  Widgets::panel(g, kSwingValX, kBtnY, kSwingValW, kBtnH, 8, t.surfaceHi, 1, t.line);
  Widgets::text(g, kSwingValX + kSwingValW / 2, kBtnY + 15,
                (String("SW ") + String(seq_->swing()) + "%").c_str(),
                Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::panel(g, kSwingPlusX, kBtnY, kSwingPlusW, kBtnH, 8, t.surface, 1, t.line);
  Widgets::text(g, kSwingPlusX + kSwingPlusW / 2, kBtnY + 12, "+", Widgets::fontL(),
                t.ink, Widgets::kCenter);

  bool metro = seq_->metronome();
  Widgets::panel(g, kMetroX, kBtnY, kMetroW, kBtnH, 8,
                 metro ? t.accent : t.surface, 1, t.line);
  Widgets::text(g, kMetroX + kMetroW / 2, kBtnY + 15, "MET", Widgets::fontS(),
                metro ? t.onAccent : t.muted, Widgets::kCenter);

  for (uint8_t i = 0; i < 4; i++) {
    bool selected = seq_->pattern() == i;
    int16_t x = kPatternX + i * kPatternPitch;
    Widgets::panel(g, x, kBtnY, kPatternW, kBtnH, 8,
                   selected ? t.accent : t.surface, 1, t.line);
    char label[2] = {(char)('A' + i), '\0'};
    Widgets::text(g, x + kPatternW / 2, kBtnY + 15, label, Widgets::fontS(),
                  selected ? t.onAccent : t.muted, Widgets::kCenter);
  }
}

void TuneUi::drawSeqHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  g->fillRect(kRightX, kSeqHeaderY, kRightW, kSeqHeaderH, t.bg);
  const PadSound &sound = bank_->pad(selectedPad_);
  String title = String("STEPS - P") + (selectedPad_ + 1 < 10 ? "0" : "") +
                 String(selectedPad_ + 1) + " " + sound.label;
  Widgets::text(g, kRightX + 4, kSeqHeaderY + 10, title.c_str(), Widgets::fontS(),
                t.ink);
  // Kit is read-only here now (it is changed on the settings screen), so the
  // header just reports it and hands the corner to the SET button.
  Widgets::text(g, kSetBtnX - 12, kSeqHeaderY + 10,
                (String("KIT ") + bank_->kitName()).c_str(), Widgets::fontS(),
                t.muted, Widgets::kRight);
  Widgets::panel(g, kSetBtnX, kSetBtnY, kSetBtnW, kSetBtnH, 6, t.surface, 1, t.line);
  Widgets::text(g, kSetBtnX + kSetBtnW / 2, kSetBtnY + 9, "SET", Widgets::fontS(),
                t.ink, Widgets::kCenter);
}

void TuneUi::drawPad_(uint8_t padIdx, uint32_t now) {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  int16_t x = padX(padCol(padIdx));
  int16_t y = padY(padRow(padIdx));
  int32_t remain = padFlashUntil_[padIdx] == 0
                       ? 0
                       : (int32_t)(padFlashUntil_[padIdx] - now);
  bool flashing = remain > 0;
  bool selected = padIdx == selectedPad_;

  // Idle pads alternate per row so the 4x4 grid reads as a grid even in the
  // flatter themes.
  uint16_t idle = (padRow(padIdx) % 2 == 0) ? t.padFill : t.padFillAlt;
  uint16_t base = selected ? t.padSel : idle;
  // A hit blends toward the flash color by velocity x how much of the envelope
  // is left, so a ghost note glows faintly and an accent slams - and both decay
  // instead of snapping off.
  uint8_t intensity = 0;
  if (flashing) {
    uint32_t env = (uint32_t)remain * 255 / kFlashMs;  // 255 -> 0 over the tail
    intensity = (uint8_t)((env * padFlashVel_[padIdx]) / 127);
  }
  uint16_t fill = intensity > 0 ? blend565(base, t.padFlash, intensity) : base;
  uint16_t border = selected ? t.accent : t.line;
  g->fillRoundRect(x, y, kPadCellW, kPadCellH, 10, fill);
  g->drawRoundRect(x, y, kPadCellW, kPadCellH, 10, border);
  if (selected && !flashing) {
    g->drawRoundRect(x + 1, y + 1, kPadCellW - 2, kPadCellH - 2, 9, t.accent);
  }

  // Swap to on-accent text only once the fill is bright enough to need it.
  bool hot = intensity >= 128;
  uint16_t textMain = hot ? t.onAccent : t.ink;
  uint16_t textMut = hot ? t.onAccent : t.muted;
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
  const TuneTheme &t = theme();
  int16_t x = stepX(step);
  int16_t y = stepY(step);
  uint8_t velocity = seq_->vel(seq_->pattern(), step, selectedPad_);
  bool playhead = seq_->playing() && seq_->playStep() == step;
  // Distance behind the playhead, wrapping the bar: 0 is the playhead itself,
  // 1..kTrailLen are the fading tail.
  uint8_t behind = 0xFF;
  if (seq_->playing()) {
    behind = (uint8_t)((seq_->playStep() + Sequencer::kSteps - step) %
                       Sequencer::kSteps);
  }

  uint16_t fill;
  if (velocity >= Sequencer::kAccentThreshold) {
    fill = t.accent;
  } else if (velocity > 0) {
    fill = t.accentDim;
  } else {
    fill = t.surface;
  }
  // Tail glow: each cell behind the playhead keeps a fraction of the highlight,
  // so the bar reads as motion instead of a single jumping box.
  if (!playhead && behind >= 1 && behind <= kTrailLen) {
    uint8_t glow = (uint8_t)(110 / behind);
    fill = blend565(fill, t.playhead, glow);
  }
  g->fillRoundRect(x, y, kStepCell, kStepCell, 6, fill);
  g->drawRoundRect(x, y, kStepCell, kStepCell, 6,
                   playhead ? t.playhead : t.line);
  if (playhead) {
    g->drawRoundRect(x + 1, y + 1, kStepCell - 2, kStepCell - 2, 5, t.playhead);
  }
  // Beat notch on 1/5/9/13.
  if (step % 4 == 0) {
    g->fillRect(x + 4, y + 3, 10, 3, t.warn);
  }
  if (velocity == 0) {
    Widgets::text(g, x + kStepCell / 2, y + 20, String(step + 1).c_str(),
                  Widgets::fontS(), t.muted, Widgets::kCenter);
  }
}

void TuneUi::drawEditPanel_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  g->fillRect(kRightX, kEditY, kRightW, kEditH, t.bg);
  const PadSound &sound = bank_->pad(selectedPad_);

  String title = String("P") + (selectedPad_ + 1 < 10 ? "0" : "") +
                 String(selectedPad_ + 1) + " " + sound.label;
  Widgets::text(g, kRightX + 8, kEditY + 6, title.c_str(), Widgets::fontL(),
                t.ink);
  Widgets::text(g, kRightX + kRightW - 8, kEditY + 10, sound.sampleRef,
                Widgets::fontS(), t.muted, Widgets::kRight);

  // VOL slider.
  Widgets::text(g, kEditLabelX, kVolSliderY + 6, "VOL", Widgets::fontS(),
                t.muted);
  Widgets::hBar(g, kSliderX, kVolSliderY, kSliderW, kSliderH,
                sound.gain / 255.0f, t.accent, t.surface);
  Widgets::text(g, kSliderX + kSliderW - 6, kVolSliderY + 7,
                String(sound.gain).c_str(), Widgets::fontS(), t.ink,
                Widgets::kRight);

  // PITCH slider (bipolar, center notch).
  Widgets::text(g, kEditLabelX, kPitchSliderY + 6, "PIT", Widgets::fontS(),
                t.muted);
  g->fillRoundRect(kSliderX, kPitchSliderY, kSliderW, kSliderH, 6, t.surface);
  int16_t center = kSliderX + kSliderW / 2;
  int16_t pos = center + (int16_t)((int32_t)sound.pitchSemis * (kSliderW / 2) / 12);
  if (sound.pitchSemis >= 0) {
    g->fillRect(center, kPitchSliderY + 4, pos - center, kSliderH - 8, t.accent);
  } else {
    g->fillRect(pos, kPitchSliderY + 4, center - pos, kSliderH - 8, t.warn);
  }
  g->fillRect(center - 1, kPitchSliderY + 2, 2, kSliderH - 4, t.ink);
  String pitchText = (sound.pitchSemis > 0 ? "+" : "") + String(sound.pitchSemis);
  Widgets::text(g, kSliderX + kSliderW - 6, kPitchSliderY + 7, pitchText.c_str(),
                Widgets::fontS(), t.ink, Widgets::kRight);

  // Choke group chips.
  Widgets::text(g, kEditLabelX, kChokeY + 12, "CHK", Widgets::fontS(),
                t.muted);
  for (uint8_t i = 0; i < 5; i++) {
    bool selected = sound.chokeGroup == i;
    int16_t x = kChokeX0 + i * kChokePitch;
    Widgets::panel(g, x, kChokeY, kChokeW, kChokeH, 8,
                   selected ? t.accent : t.surface, 1, t.line);
    Widgets::text(g, x + kChokeW / 2, kChokeY + 12, chokeLabel(i), Widgets::fontS(),
                  selected ? t.onAccent : t.muted, Widgets::kCenter);
  }

  // Backing loop: a performance control, so it stays on this screen rather
  // than moving to settings with the kit selector.
  Widgets::text(g, kEditLabelX, kLoopY + 12, "LOOP", Widgets::fontS(), t.muted);
  bool haveLoops = LoopLibrary::count() > 0 && cb_.loopSelect != nullptr;
  int8_t current = cb_.loopCurrent != nullptr ? cb_.loopCurrent(cb_.ctx) : -1;
  // Arrows step within the catalog for quick A/B of neighbours; the name is a
  // button into the browser, because stepping through 41 loops is not a way to
  // find one.
  Widgets::panel(g, kLoopPrevX, kLoopY, kLoopArrowW, kLoopH, 8, t.surface, 1, t.line);
  Widgets::text(g, kLoopPrevX + kLoopArrowW / 2, kLoopY + 9, "<", Widgets::fontL(),
                haveLoops ? t.ink : t.line, Widgets::kCenter);
  int16_t nameX = kLoopPrevX + kLoopArrowW + 4;
  int16_t nameW = kLoopNextX - nameX - 4;
  Widgets::panel(g, nameX, kLoopY, nameW, kLoopH, 8,
                 current >= 0 ? t.accentDim : t.surface, 1, t.line);
  const char *label = current >= 0
                          ? LoopLibrary::info((uint8_t)current).title
                          : (haveLoops ? "-- none --" : "no loops on SD");
  Widgets::text(g, nameX + nameW / 2, kLoopY + 10, label, Widgets::fontM(),
                current >= 0 ? t.ink : t.muted, Widgets::kCenter);
  Widgets::panel(g, kLoopNextX, kLoopY, kLoopArrowW, kLoopH, 8, t.surface, 1, t.line);
  Widgets::text(g, kLoopNextX + kLoopArrowW / 2, kLoopY + 9, ">", Widgets::fontL(),
                haveLoops ? t.ink : t.line, Widgets::kCenter);
}

void TuneUi::drawScope_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  g->fillRect(kRightX, kVoicesY, kRightW, kVoicesH, t.bg);

  static int16_t samples[kScopeSamples];
  uint16_t count = cb_.scope != nullptr ? cb_.scope(cb_.ctx, samples, kScopeSamples) : 0;

  if (count == 0) {
    // Silent build: no mix to show, so fall back to the simulated voice meters
    // rather than drawing a flat line that would imply real (dead) audio.
    Widgets::text(g, kRightX + 8, kVoicesY + 6, "VOICES", Widgets::fontS(), t.muted);
    if (voices_ != nullptr) {
      int16_t barBottom = kVoicesY + kVoicesH - 14;
      int16_t barMaxH = 70;
      for (uint8_t i = 0; i < VisualVoices::kVoiceCount; i++) {
        int16_t x = kRightX + 130 + i * 74;
        g->fillRect(x, barBottom - barMaxH, 40, barMaxH, t.surface);
        int16_t h = (int16_t)((int32_t)voices_->voice(i).level * barMaxH / 127);
        if (h > 0) {
          g->fillRect(x, barBottom - h, 40, h, t.accent);
        }
      }
      Widgets::text(g, kRightX + 8, kVoicesY + kVoicesH - 26,
                    voices_->primaryLabel(), Widgets::fontS(), t.ink);
    }
    return;
  }

  Widgets::text(g, kScopeX, kVoicesY + 6, "OUT", Widgets::fontS(), t.muted);
  Widgets::text(g, kScopeX + kScopeW, kVoicesY + 6,
                voices_ != nullptr ? voices_->primaryLabel() : "",
                Widgets::fontS(), t.ink, Widgets::kRight);

  // Scope trace: one vertical segment per column, spanning that column's
  // sample range, so transients read as solid bars instead of dotted points.
  int16_t mid = kScopeTraceY + kScopeTraceH / 2;
  g->fillRect(kScopeX, kScopeTraceY, kScopeW, kScopeTraceH, t.surface);
  g->drawFastHLine(kScopeX, mid, kScopeW, t.line);
  uint16_t perCol = count / kScopeW > 0 ? count / kScopeW : 1;
  for (int16_t col = 0; col < kScopeW; col++) {
    uint16_t start = (uint16_t)((uint32_t)col * count / kScopeW);
    int16_t lo = 0, hi = 0;
    for (uint16_t k = 0; k < perCol && start + k < count; k++) {
      int16_t s = samples[start + k];
      if (s < lo) lo = s;
      if (s > hi) hi = s;
    }
    int16_t yTop = mid - (int16_t)((int32_t)hi * (kScopeTraceH / 2) / 32768);
    int16_t yBot = mid - (int16_t)((int32_t)lo * (kScopeTraceH / 2) / 32768);
    if (yBot < yTop) {
      int16_t tmp = yTop; yTop = yBot; yBot = tmp;
    }
    g->drawFastVLine(kScopeX + col, yTop, (yBot - yTop) + 1, t.accent);
  }

  // Peak VU with a slow-falling hold marker.
  uint8_t peak = cb_.peak != nullptr ? cb_.peak(cb_.ctx) : 0;
  if (peak >= vuHold_) {
    vuHold_ = peak;
  } else if (vuHold_ > 3) {
    vuHold_ -= 3;
  } else {
    vuHold_ = 0;
  }
  g->fillRect(kScopeX, kVuY, kScopeW, kVuH, t.surface);
  int16_t w = (int16_t)((int32_t)peak * kScopeW / 255);
  if (w > 0) {
    // Green through the body, warn near the top, danger at the ceiling.
    uint16_t color = peak > 240 ? t.danger : (peak > 200 ? t.warn : t.good);
    g->fillRect(kScopeX, kVuY, w, kVuH, color);
  }
  int16_t holdX = kScopeX + (int16_t)((int32_t)vuHold_ * kScopeW / 255);
  if (vuHold_ > 0) {
    g->drawFastVLine(holdX, kVuY, kVuH, t.ink);
  }
}

void TuneUi::drawStatus_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const TuneTheme &t = theme();
  g->fillRect(0, kStatusY, kScreenW, kStatusH, t.bg);
  g->drawFastHLine(0, kStatusY, kScreenW, t.line);
  String engine = cb_.audioStatus != nullptr ? cb_.audioStatus(cb_.ctx) : String("audio n/a");
  Widgets::text(g, 8, kStatusY + 12, engine.c_str(), Widgets::fontS(), t.muted);
  String mem = String("heap ") + String(ESP.getFreeHeap() / 1024) + "K psram " +
               String(ESP.getFreePsram() / 1024 / 1024) + "M";
  Widgets::text(g, kScreenW / 2, kStatusY + 12, mem.c_str(), Widgets::fontS(),
                t.muted, Widgets::kCenter);
  // Live contact count: the only way to confirm multi-touch on this board,
  // since USB-CDC serial drops once the sketch runs. Lights up when more than
  // one finger is tracked.
  uint8_t contacts = touch_.activeCount();
  String perf = String("ui ") + String(lastRenderUs_ / 1000.0f, 1) + "ms  T" +
                String(contacts);
  Widgets::text(g, kScreenW - 8, kStatusY + 12, perf.c_str(), Widgets::fontS(),
                contacts > 1 ? t.accent : t.muted, Widgets::kRight);
}

#else  // headless / non-P4: keep the class alive with no-op behavior

void TuneUi::begin(SampleBank *bank, Sequencer *seq, VisualVoices *voices,
                   const Callbacks &callbacks) {
  bank_ = bank;
  seq_ = seq;
  voices_ = voices;
  cb_ = callbacks;
  loadSettings_();
  displayReady_ = false;
}

void TuneUi::tick() {}

void TuneUi::notePadFlash(uint8_t padIdx, uint8_t velocity) {
  if (padIdx < SampleBank::kPadCount) {
    padFlashUntil_[padIdx] = millis();
    padFlashVel_[padIdx] = velocity;
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

// --- Theming: compiled in every build -------------------------------------
// The palette only affects drawing, but the selection, its persistence, and the
// `theme` serial command are useful headless too (and keep the .ino free of
// display #ifdefs), so these live outside the guard above.

#if USE_DISPLAY
#include <CrowPanelShared.h>  // CrowDisplay::setBacklight
#endif
#include <Preferences.h>

void TuneUi::loadSettings_() {
  Preferences prefs;
  if (prefs.begin(CYPHER_TUNE_NVS_NAMESPACE, true)) {
    uint32_t storedTheme = prefs.getUInt("theme", 0);
    uint32_t storedBright = prefs.getUInt("bright", 255);
    idleDimEnabled_ = prefs.getBool("idledim", true);
    prefs.end();
    if (storedTheme < tuneThemeCount()) {
      themeIndex_ = (uint8_t)storedTheme;
    }
    brightness_ = storedBright < kMinBrightness
                      ? kMinBrightness
                      : (storedBright > 255 ? 255 : (uint8_t)storedBright);
  }
  lastActivityMs_ = millis();
}

void TuneUi::persistSettings_() const {
  Preferences prefs;
  if (prefs.begin(CYPHER_TUNE_NVS_NAMESPACE, false)) {
    prefs.putUInt("theme", themeIndex_);
    prefs.putUInt("bright", brightness_);
    prefs.putBool("idledim", idleDimEnabled_);
    prefs.end();
  }
}

void TuneUi::applyBrightness_() {
  // CrowDisplay only exists in USE_DISPLAY builds by design, so the call is
  // guarded here rather than relying on a stub (matches project 22's
  // applyBacklight). The level itself is still tracked headless so `bright`
  // and the NVS value stay meaningful.
#if USE_DISPLAY
  CrowDisplay::setBacklight(dimmed_ ? kMinBrightness : brightness_);
#endif
}

void TuneUi::setBrightness(uint8_t level) {
  brightness_ = level < kMinBrightness ? kMinBrightness : level;
  dimmed_ = false;
  applyBrightness_();
  persistSettings_();
}

void TuneUi::bumpBrightness(int16_t delta) {
  int16_t v = (int16_t)brightness_ + delta;
  if (v > 255) v = 255;
  if (v < (int16_t)kMinBrightness) v = kMinBrightness;
  setBrightness((uint8_t)v);
}

void TuneUi::setIdleDim(bool on) {
  idleDimEnabled_ = on;
  if (!on && dimmed_) {
    dimmed_ = false;
    applyBrightness_();
  }
  persistSettings_();
}

void TuneUi::noteActivity() {
  lastActivityMs_ = millis();
  if (dimmed_) {
    dimmed_ = false;
    applyBrightness_();
  }
}

void TuneUi::setView(View v) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (view_ != v) {
    dirtyAll_ = true;  // different screen entirely
  }
#endif
  view_ = v;
}

void TuneUi::cycleTheme() {
  themeIndex_ = (themeIndex_ + 1) % tuneThemeCount();
  persistSettings_();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirtyAll_ = true;  // every color changed; nothing partial would be coherent
#endif
}

bool TuneUi::setThemeByName(const String &name) {
  int idx = tuneThemeIndexFromName(name);
  if (idx < 0) {
    return false;
  }
  themeIndex_ = (uint8_t)idx;
  persistSettings_();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirtyAll_ = true;
#endif
  return true;
}

String TuneUi::settingsLine() const {
  return String("brightness ") + String(brightness_) + "/255 (min " +
         String(kMinBrightness) + ")  idle-dim " +
         (idleDimEnabled_ ? "on" : "off") + "  theme " + theme().name +
         "  view " + (view_ == kViewSettings ? "settings" : "main");
}

String TuneUi::themeLine() const {
  String out = String("theme: ") + theme().name + " (" + String(themeIndex_ + 1) +
               "/" + String(tuneThemeCount()) + ")  available:";
  for (uint8_t i = 0; i < tuneThemeCount(); i++) {
    out += String(i == themeIndex_ ? " *" : " ") + tuneTheme(i).name;
  }
  return out;
}
