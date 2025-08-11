#pragma once
#include <Arduino.h>
#include <math.h>
#include "ThirdPartyDaisy.h"  // vendored DaisyDuino ONLY

// ---- Library version (available at compile-time) ----
#define HPCB_VERSION_MAJOR 1
#define HPCB_VERSION_MINOR 1
#define HPCB_VERSION_PATCH 1
#define HPCB_VERSION_STR "1.1.1"

// Mono per-sample user callback
typedef void (*HPCB_AudioCB_Mono)(float in, float &out);

/** @brief Footswitch timing configuration (reserved for future debouncing/gesture FSM). */
struct HPCB_FootswitchTiming {
  uint16_t debounce_ms       = 12;   ///< Per-edge debounce time in ms
  uint16_t longpress_ms      = 500;  ///< Long press threshold in ms
  uint16_t multiclick_gap_ms = 300;  ///< Double-click window in ms
};

/** @brief Curve mapping options for pot range conversion. */
enum class HPCB_Curve : uint8_t { Linear, Log10, Exp10 };

/**
 * @brief Main hardware/helper interface for Harold Street Pedals Daisy boards.
 *
 * Simple, beginner-friendly API that abstracts GPIO, audio start/stop,
 * and common DSP helpers (LFO, smoothing, mixing, etc.).
 *
 * Always-on 48kHz sample rate; 96k accepted in Init() but clamped to supported modes.
 */
class HaroldPCB {
public:
  // ================= Core =================

  /**
   * @brief Initialize hardware and audio configuration.
   * @param sample_rate_hz Desired sample rate (48000 or 96000 accepted; defaults to 48000).
   * @param block_size Audio block size in samples (defaults to 48).
   * @return true if initialized.
   */
  bool Init(uint32_t sample_rate_hz = 48000, uint16_t block_size = 48);

  /**
   * @brief Start audio using a beginner-friendly mono per-sample callback.
   * @param cb_mono Function pointer: (float in, float &out)
   * @return true if audio started.
   */
  bool StartAudio(HPCB_AudioCB_Mono cb_mono);

  /** @brief Stop audio processing. */
  void StopAudio();

  /** @brief Current sample rate in Hz. */
  uint32_t SampleRate() const { return sr_; }

  /** @brief Library semantic version string, e.g., "1.1.0". */
  const char* Version();

  // ================ Controls ================

  /**
   * @brief Read a raw pot value as 0.0–1.0.
   * @param index Pot index [0..5] for RV1..RV6.
   */
  float ReadPot(uint8_t index);

  /**
   * @brief Read a pot mapped into [min,max] with optional curve shaping.
   * @param index Pot index [0..5].
   * @param min Minimum mapped value.
   * @param max Maximum mapped value.
   * @param curve Mapping curve (Linear/Log10/Exp10).
   */
  float ReadPotMapped(uint8_t index, float min, float max,
                      HPCB_Curve curve = HPCB_Curve::Linear);

  /**
   * @brief Read a pot with one-pole smoothing (control-rate).
   * @param index Pot index [0..5].
   * @param smooth_ms Time constant in milliseconds (set <=0 for no smoothing).
   * @return Smoothed 0.0–1.0 value.
   */
  float ReadPotSmoothed(uint8_t index, float smooth_ms);

  /**
   * @brief Read a toggle switch (active-low hardware).
   * @param index Toggle index [0..3] for TS1..TS4.
   * @return true if ON (switch pulled low).
   */
  bool  ReadToggle(uint8_t index) const;

  /**
   * @brief Footswitch raw pressed (active-low).
   * @param index Footswitch index [0..1] for FS1..FS2.
   */
  bool  FootswitchIsPressed(uint8_t index) const;

  /**
   * @brief Footswitch raw released.
   * @param index Footswitch index [0..1].
   */
  bool  FootswitchIsReleased(uint8_t index) const;

  // Reserved for future debounced gestures (placeholders for API stability)
  bool  FootswitchIsLongPressed(uint8_t) const { return false; }
  bool  FootswitchIsDoublePressed(uint8_t) const { return false; }
  bool  FootswitchIsDoubleLongPressed(uint8_t) const { return false; }

  // ================= LEDs =================

  /**
   * @brief Control an LED (active-low hardware).
   * @param index LED index [0..1].
   * @param on true to turn ON, false to turn OFF.
   */
  void SetLED(uint8_t index, bool on);

  // ============== Idle tick ===============

  /**
   * @brief Call periodically in loop() for housekeeping.
   * Reserved for future debouncing and timing state machines.
   */
  void Idle();

  // =========== Inline DSP Helpers (kept inside HaroldPCB for one-include UX) ===========

  /** @brief Simple per-sample LFO with triangle/square/sine outputs. */
  struct LFO {
    void SetRateHz(float hz, float sr) { inc_ = hz / sr; }
    void Sync() { ph_ = 0.f; }
    float NextTri() {
      step();
      return 2.f * fabsf(2.f*(ph_ - floorf(ph_ + 0.5f))) - 1.f;
    }
    float NextSquare() { step(); return ph_ < 0.5f ? 1.f : -1.f; }
    float NextSine()   { step(); return sinf(2.f * 3.14159265359f * ph_); }
  private:
    void step(){ ph_ += inc_; if (ph_ >= 1.f) ph_ -= 1.f; }
    float ph_ = 0.f, inc_ = 0.f;
  };

  /** @brief One-pole low-pass for control or audio smoothing. */
  struct OnePole {
    void SetTauMs(float ms, float sr) {
      float dt = 1.f / sr;
      a_ = dt / (ms/1000.f + dt);
    }
    float Process(float x) { z_ += a_ * (x - z_); return z_; }
  private:
    float a_ = 1.f, z_ = 0.f;
  };

  /** @brief Blink an LED with a bipolar control (e.g., LFO). */
  struct LEDLFO {
    void Attach(uint8_t ledIndex){ led_ = ledIndex; }
    void UpdateFrom(float lfo, HaroldPCB& h){ h.SetLED(led_, lfo > 0.f); }
  private:
    uint8_t led_ = 0;
  };

  /** @brief Run a task every N calls (e.g., once per audio block). */
  struct EveryN {
    size_t n = 48, c = 0;
    bool Tick(){ if(++c >= n){ c = 0; return true; } return false; }
  };

  /** @brief Linear dry/wet mix (0=dry, 1=wet). */
  static inline float Mix(float dry, float wet, float mix01) {
    return dry*(1.f - mix01) + wet*mix01;
  }

  /** @brief Convert LFO (-1..+1) and depth (0..1) to a safe gain (never negative). */
  static inline float ModToGain(float lfo, float depth) {
    return 1.0f - 0.5f*depth + 0.5f*depth*lfo;
  }

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
