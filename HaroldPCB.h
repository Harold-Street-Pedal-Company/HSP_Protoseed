#pragma once
#include <Arduino.h>
#include "ThirdPartyDaisy.h"  // vendored DaisyDuino ONLY

// Mono per-sample user callback
typedef void (*HPCB_AudioCB_Mono)(float in, float &out);

struct HPCB_FootswitchTiming {
  uint16_t debounce_ms       = 12;
  uint16_t longpress_ms      = 500;
  uint16_t multiclick_gap_ms = 300;
};

enum class HPCB_Curve : uint8_t { Linear, Log10, Exp10 };

class HaroldPCB {
public:
  // ---- Core ----
  bool Init(uint32_t sample_rate_hz = 48000, uint16_t block_size = 48);
  bool StartAudio(HPCB_AudioCB_Mono cb_mono);
  void StopAudio();

  uint32_t SampleRate() const { return sr_; }

  // ---- Controls ----
  float ReadPot(uint8_t index);
  float ReadPotMapped(uint8_t index, float min, float max, HPCB_Curve curve = HPCB_Curve::Linear);
  float ReadPotSmoothed(uint8_t index, float smooth_ms);
  bool  ReadToggle(uint8_t index) const;
  bool  FootswitchIsPressed(uint8_t index) const;
  bool  FootswitchIsReleased(uint8_t index) const;
  bool  FootswitchIsLongPressed(uint8_t) const { return false; }
  bool  FootswitchIsDoublePressed(uint8_t) const { return false; }
  bool  FootswitchIsDoubleLongPressed(uint8_t) const { return false; }

  // ---- LEDs ----
  void SetLED(uint8_t index, bool on);

  // ---- Idle tick ----
  void Idle();

private:
  // HaroldPCB v1.2 pin map
  static constexpr uint8_t kNumPots     = 6;
  static constexpr uint8_t kNumToggles  = 4;
  static constexpr uint8_t kNumFS       = 2;
  static constexpr uint8_t kNumLEDs     = 2;

  const int pot_pins_[kNumPots]       = {A6, A5, A4, A3, A2, A1};
  const int toggle_pins_[kNumToggles] = {10, 9, 8, 7};
  const int fs_pins_[kNumFS]          = {26, 25};
  const int led_pins_[kNumLEDs]       = {22, 23}; // active-low

  float pot_smooth_[kNumPots] = {0};

  uint32_t sr_        = 48000;
  uint16_t blocksize_ = 48;

  static HPCB_AudioCB_Mono s_user_mono_;
  static void _MonoThunk(float **in, float **out, size_t size);
};
