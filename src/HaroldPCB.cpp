#include "HaroldPCB.h"
#include <math.h>

HPCB_AudioCB_Mono HaroldPCB::s_user_mono_ = nullptr;

bool HaroldPCB::Init(uint32_t sample_rate_hz, uint16_t block_size) {
  sr_        = sample_rate_hz ? sample_rate_hz : 48000;
  blocksize_ = block_size ? block_size : 48;

  // IO config
  for (uint8_t i = 0; i < kNumPots; ++i)       { pinMode(pot_pins_[i], INPUT); }
  for (uint8_t i = 0; i < kNumToggles; ++i)    { pinMode(toggle_pins_[i], INPUT_PULLUP); }
  for (uint8_t i = 0; i < kNumFS; ++i)         { pinMode(fs_pins_[i], INPUT_PULLUP); }
  for (uint8_t i = 0; i < kNumLEDs; ++i)       { pinMode(led_pins_[i], OUTPUT); digitalWrite(led_pins_[i], HIGH); } // off (active-low)

  // Daisy hardware/audio
  DAISY.init(DAISY_SEED, (sr_ == 96000 ? AUDIO_SR_96K : AUDIO_SR_48K));
  DAISY.SetAudioBlockSize(blocksize_);

  return true;
}

bool HaroldPCB::StartAudio(HPCB_AudioCB_Mono cb_mono) {
  s_user_mono_ = cb_mono;
  if (!s_user_mono_) return false;
  DAISY.begin(_MonoThunk);
  return true;
}

void HaroldPCB::StopAudio() {
  DAISY.end();
}

const char* HaroldPCB::Version() {
  return HPCB_VERSION_STR;
}

// HaroldPCB.cpp — _MonoThunk() with R muted in mono mode
void HaroldPCB::_MonoThunk(float **in, float **out, size_t size) {
  HPCB_AudioCB_Mono cb = s_user_mono_;
  if (!cb) {
    // No user callback: pass L through, keep R silent
    for (size_t i = 0; i < size; ++i) {
      out[0][i] = in[0][i];
      out[1][i] = 0.0f;      // <-- force Right silent
    }
    return;
  }

  for (size_t i = 0; i < size; ++i) {
    float o = 0.0f;
    cb(in[0][i], o);
    out[0][i] = o;
    out[1][i] = 0.0f;        // <-- force Right silent
  }
}


float HaroldPCB::ReadPot(uint8_t index) {
  if (index >= kNumPots) return 0.0f;
  return analogRead(pot_pins_[index]) / 1023.0f; // DaisyDuino ADC default 10-bit
}

float HaroldPCB::ReadPotMapped(uint8_t index, float min, float max, HPCB_Curve curve) {
  float v = ReadPot(index);
  switch (curve) {
    case HPCB_Curve::Log10: v = log10f(1.0f + 9.0f * v); break;           // more resolution near 0
    case HPCB_Curve::Exp10: v = (powf(10.0f, v) - 1.0f) / 9.0f; break;    // more near 1
    default: break;
  }
  return min + (max - min) * v;
}

float HaroldPCB::ReadPotSmoothed(uint8_t index, float smooth_ms) {
  if (index >= kNumPots) return 0.0f;
  float val   = ReadPot(index);
  float dt_ms = (1000.0f * blocksize_) / float(sr_);
  float alpha = (smooth_ms <= 0.0f) ? 1.0f : dt_ms / (smooth_ms + dt_ms);
  pot_smooth_[index] += alpha * (val - pot_smooth_[index]);
  return pot_smooth_[index];
}

bool HaroldPCB::ReadToggle(uint8_t index) const {
  if (index >= kNumToggles) return false;
  return digitalRead(toggle_pins_[index]) == LOW;
}

bool HaroldPCB::FootswitchIsPressed(uint8_t index) const {
  if (index >= kNumFS) return false;
  return digitalRead(fs_pins_[index]) == LOW;
}

bool HaroldPCB::FootswitchIsReleased(uint8_t index) const {
  if (index >= kNumFS) return true;
  return digitalRead(fs_pins_[index]) == HIGH;
}

void HaroldPCB::SetLED(uint8_t index, bool on) {
  if (index >= kNumLEDs) return;
  digitalWrite(led_pins_[index], on ? HIGH : LOW);
}

void HaroldPCB::Idle() {
  // Hook for future debouncing/timing FSM
}
