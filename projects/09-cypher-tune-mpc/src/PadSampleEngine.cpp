#include "PadSampleEngine.h"

namespace {

const PadSample DEFAULT_SAMPLES[PadSampleEngine::kPadCount] = {
  {1, "Kick", "factory:kick-tight", 115},
  {2, "Snare", "factory:snare-crack", 110},
  {3, "Hat", "factory:hat-closed", 92},
  {4, "OpenHat", "factory:hat-open", 88},
  {5, "Clap", "factory:clap", 108},
  {6, "Rim", "factory:rim", 94},
  {7, "PercA", "factory:perc-low", 96},
  {8, "PercB", "factory:perc-high", 96},
  {9, "BassA", "factory:bass-a", 112},
  {10, "BassB", "factory:bass-b", 112},
  {11, "ChordA", "factory:chord-a", 86},
  {12, "ChordB", "factory:chord-b", 86},
  {13, "VoxA", "factory:vox-a", 90},
  {14, "VoxB", "factory:vox-b", 90},
  {15, "FxUp", "factory:fx-up", 84},
  {16, "FxDn", "factory:fx-down", 84}
};

const PadSample &safeSample(uint8_t pad) {
  if (pad < 1 || pad > PadSampleEngine::kPadCount) {
    pad = 1;
  }
  return DEFAULT_SAMPLES[pad - 1];
}

}  // namespace

void PadSampleEngine::begin(AudioOutput &audio, VisualVoices &voices) {
  audio_ = &audio;
  voices_ = &voices;
  lastPad_ = 0;
}

bool PadSampleEngine::trigger(uint8_t pad, String &message) {
  const PadSample &slot = safeSample(pad);
  lastPad_ = slot.pad;
  if (voices_ != nullptr) {
    voices_->trigger(slot.pad, slot.label, slot.velocity);
  }
  bool audioAccepted = false;
  if (audio_ != nullptr) {
    audioAccepted = audio_->trigger(slot.pad, slot.velocity);
  }
  message = String("Pad ") + String(slot.pad) + " " + slot.label +
            " sample=" + slot.sampleRef +
            " audio=" + (audioAccepted ? "accepted" : "not-ready");
  return audioAccepted;
}

const PadSample &PadSampleEngine::sample(uint8_t pad) const {
  return safeSample(pad);
}

const char *PadSampleEngine::lastLabel() const {
  if (lastPad_ == 0) {
    return "none";
  }
  return safeSample(lastPad_).label;
}

String PadSampleEngine::sampleMap() const {
  String out;
  for (uint8_t i = 1; i <= kPadCount; i++) {
    const PadSample &slot = safeSample(i);
    if (i > 1) {
      out += "|";
    }
    out += String(i) + ":" + slot.label;
  }
  return out;
}
