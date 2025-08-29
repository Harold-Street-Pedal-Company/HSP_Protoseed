// HSP_Cavernous.ino — v1.0.5 (Harold Street Pedal Company, 2025)
// 48 kHz / block 8 — HaroldPCB v1.3.1; mono; right output is muted by the library
//
// Short: “Cavernous” = PT2399-style dark delay (≤ ~650 ms) feeding a 3-mode reverb
// (Spring / Mod / Shimmer-ish). Delay modulation hits the repeats only. The reverb’s
// “Rate/Morph” changes per mode. Bypass is soft with **tails**: we stop feeding new
// input but let existing buffers decay into the output.

#include <HaroldPCB.h>
#include <math.h>

static HaroldPCB H;

/*──────────────────────────────────────────────────────────────────────────────
  SIGNAL FLOW (read this like a block diagram)

  IN ──► Dry Tap ────────────────────────────────────────────┐
         │                                                   │
         ├─► Delay (dark repeats + optional wow/flutter) ───►│──► DelayWet
         │      • Time, Repeats, Blend                       │
         │      • Mod: Off / Light / Deep                    │
         │      • LPF in feedback                            │
         │                                                   ▼
                   Reverb (mode-dependent sweetening) ─────────► ReverbWet
                    • Spring: tremolo on wet
                    • Mod: chorus thickness on wet
                    • Shimmer: bright octave-ish send
         │
  OUT =  Active:  Dry*(1−M) + (DelayWet*D + ReverbWet*R)*M   (M clamps D+R≤1 if enabled)
         Bypassed (tails): Dry + (DelayWet*D + ReverbWet*R)   (feed into delay cut to 0)

  FS1 = Hold (momentary) — raises delay feedback near-unity (safe, no runaway)
        • IMPORTANT: FS1 is **ignored when bypassed** to prevent ticks.
  FS2 = Effect ON/OFF (soft, with tails). LED2 mirrors active state.
  LEDs: LED1 lights while FS1 is pressed; LED2 lights when the effect is active.
──────────────────────────────────────────────────────────────────────────────*/

/*──────────────────────────────────────────────────────────────────────────────
  USER TUNING PANEL — all user-tweakable stuff in one spot
  (Make it sound “yours” by editing only this section. Everything else is wired
   to these values.)
──────────────────────────────────────────────────────────────────────────────*/

// ── Behavior Switches (true/false) ───────────────────────────────────────────
// Use “tails” when bypassed (true = wet keeps decaying when off; false = hard dry)
static const bool tUseTailsBypass = true;
// TS4 direction for Warm/Dark voicing: true = TS4 DOWN is Warm, UP is Bright
static const bool tTS4_WarmDown = true;
// When Bright is selected (TS4 opposite of Warm), also deepen delay modulation?
static const bool tBrightSideDeepensDelayMod = true;
// Clamp total wet mix automatically (protect headroom when D+R > 1)
static const bool tClampWetMix = true;

// ── Delay Section ────────────────────────────────────────────────────────────
// Musical time range (ms) — Caverns-like feel
static const float tDelayMinMs = 50.0f;
static const float tDelayMaxMs = 650.0f;
// Modulation depth (± ms) for Light vs Deep; “wow/flutter” feel
static const float tModLightMs = 2.0f;
static const float tModDeepMs = 8.0f;
// Modulation rate range (Hz)
static const float tModRateMinHz = 0.05f;
static const float tModRateMaxHz = 6.0f;
// Hold feedback (FS1) — safe near-unity sustain without runaway
static const float tHoldFeedback = 0.995f;
// Repeats knob response and ceiling
static const float tRepeatsCurveExp = 2.0f;  // 1.0=linear, 2.0=squared
static const float tRepeatsMax = 0.98f;      // safety cap (below 1.0)
// Feedback low-pass (darkens repeats each lap)
static const float tFbLPF_MinHz = 1200.0f;  // darkest
static const float tFbLPF_MaxHz = 5500.0f;  // brightest

// ── Reverb Section ───────────────────────────────────────────────────────────
// Room pre-delay into tank (ms)
static const float tPreDelayMs = 20.0f;
// Decay (RT60) range in seconds
static const float tRT60_MinSec = 0.4f;
static const float tRT60_MaxSec = 12.0f;
// Warm/Dark tone on wet tail (cutoff when Warm vs Bright)
static const float tWarmCutHz = 3500.0f;
static const float tBrightCutHz = 8000.0f;
// Spring mode tremolo (on wet)
static const float tTremMinHz = 0.5f;
static const float tTremMaxHz = 8.0f;
static const float tTremDepth = 0.5f;  // 0..1 (0.5 ≈ gentle 6 dB swing)
// Mod mode chorus jitter (on wet)
static const float tChorusMinHz = 0.1f;
static const float tChorusMaxHz = 2.5f;
static const float tChorusDepth = 0.003f;  // ~0.3% time jitter
// Shimmer “sparkle” voicing (CPU-light octave-ish enhancer)
static const float tShimmerHPF_Hz = 2000.0f;   // high-pass the rectified send
static const float tShimmerMax = 0.8f;         // max send amount
static const float tShimmerMode2Scale = 0.7f;  // Shimmer mode (TS1+TS2=10)
static const float tShimmerMode3Scale = 1.0f;  // Bright-Shimmer (TS1+TS2=11)

// ── Global / Mix / Safety ───────────────────────────────────────────────────
static const float tSoftLimiterKnee = 0.98f;  // output knee target
static const float tPotSmoothN = 8.0f;        // higher = smoother pots

// Audio rate (fixed by board/project; listed here for reference)
static const float kSR = 48000.0f;
static const int kBlockSize = 8;

/*──────────────────────────────────────────────────────────────────────────────
  CONTROL MAP — HaroldPCB v1.3.1 (mono)
──────────────────────────────────────────────────────────────────────────────
  Pots:   RV1=Delay Time, RV2=Repeats, RV3=Delay Blend,
          RV4=Reverb Decay, RV5=Reverb Blend, RV6=Rate/Morph
  Toggles: TS1+TS2 (2-bit reverb mode), TS3 (Delay Mod ON/OFF), TS4 (Warm/Dark)
  Foot:    FS1=Hold (momentary; **ignored when bypassed**), FS2=Active toggle
  LEDs:    LED1 = FS1 pressed;  LED2 = Effect Active
──────────────────────────────────────────────────────────────────────────────*/

/*──────────────────────────────────────────────────────────────────────────────
  SMALL, READABLE HELPERS (unique names to avoid collisions)
──────────────────────────────────────────────────────────────────────────────*/

static inline float clampf(float v, float lo, float hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}
static inline float expMap01(float x, float minv, float maxv) {
  x = clampf(x, 0.0f, 1.0f);
  return minv * powf(maxv / minv, x);
}
static inline float softLimit(float x) {
  const float k = 3.0f;  // curvature
  return tanhf(k * x) / tanhf(k * tSoftLimiterKnee);
}

struct HSP_OnePoleLPF {
  float a = 0.0f, z = 0.0f;
  void setCutoff(float hz) {
    a = 1.0f - expf(-2.0f * 3.14159265359f * hz / kSR);
  }
  float process(float x) {
    z += a * (x - z);
    return z;
  }
};

struct HSP_Allpass {
  int size = 0, w = 0;
  float g = 0.0f;
  float* buf = nullptr;
  void init(float* storage, int samples, float gain) {
    buf = storage;
    size = samples;
    w = 0;
    g = gain;
    for (int i = 0; i < size; ++i) buf[i] = 0.0f;
  }
  float process(float x) {
    float y = buf[w];
    float z = x + (-g) * y;
    buf[w] = z + g * y;
    if (++w >= size) w = 0;
    return y + g * z;
  }
};

struct HSP_Comb {
  int size = 0, w = 0;
  float fb = 0.7f;
  float* buf = nullptr;
  void init(float* storage, int samples) {
    buf = storage;
    size = samples;
    w = 0;
    for (int i = 0; i < size; ++i) buf[i] = 0.0f;
  }
  float process(float x) {
    float y = buf[w];
    buf[w] = x + fb * y;
    if (++w >= size) w = 0;
    return y;
  }
};

static inline float readDelayFrac(float* buf, int size, float idx) {
  int i0 = (int)floorf(idx);
  int i1 = i0 + 1;
  if (i1 >= size) i1 -= size;
  float f = idx - i0;
  return buf[i0] + (buf[i1] - buf[i0]) * f;
}

/*──────────────────────────────────────────────────────────────────────────────
  UI STATE (debounced) + DERIVED PARAMS (computed in loop(), not in audio)
──────────────────────────────────────────────────────────────────────────────*/

struct UIState {
  float p[6];    // RV1..RV6
  bool t[4];     // TS1..TS4
  bool fs_hold;  // FS1 (ignored when bypassed)
  bool fs2_now;  // FS2
};

struct Params {
  // delay
  float timeMs, repeats, dBlend;
  bool modOn, deepMod;
  float modRateHz;

  // reverb
  float rt60, rBlend;
  int modeBits;  // 0..3
  bool warm;
  float rateMorph01;

  // cached tone/engine
  float fbCutHz;
};

static UIState gUI;
static Params gP;
static bool gActive = true;
static bool gPrevFS2 = false;

/*──────────────────────────────────────────────────────────────────────────────
  DELAY ENGINE (dark repeats with optional modulation on repeats)
──────────────────────────────────────────────────────────────────────────────*/

static const int kDelayBufMax = (int)((tDelayMaxMs / 1000.0f) * kSR + 2.0f) + 512;
static float gDelayBuf[kDelayBufMax];
static int gDelayW = 0;

static HSP_OnePoleLPF gFbTone;
static float gModPhase = 0.0f;  // 0..1
static float gModInc = 0.0f;    // set from modRateHz

static inline float delayProcess(float in, bool feedEnable, bool holdNow) {
  float baseSamps = clampf((gP.timeMs / 1000.0f) * kSR,
                           (tDelayMinMs / 1000.0f) * kSR,
                           (tDelayMaxMs / 1000.0f) * kSR);

  if (gP.modOn) {
    float ph = gModPhase;
    float tri = (ph < 0.5f) ? (ph * 4.0f - 1.0f) : (3.0f - ph * 4.0f);  // −1..+1
    float depthMs = gP.deepMod ? tModDeepMs : tModLightMs;
    baseSamps += (depthMs / 1000.0f) * kSR * tri;
    gModPhase += gModInc;
    if (gModPhase >= 1.0f) gModPhase -= 1.0f;
  }

  float readIdx = (float)gDelayW - baseSamps;
  while (readIdx < 0.0f) readIdx += (float)kDelayBufMax;
  float d = readDelayFrac(gDelayBuf, kDelayBufMax, readIdx);

  float fbFiltered = gFbTone.process(d);
  float fbAmt = holdNow ? tHoldFeedback : clampf(gP.repeats, 0.0f, tRepeatsMax);

  float writeVal = (feedEnable ? in : 0.0f) + fbAmt * fbFiltered;
  gDelayBuf[gDelayW] = writeVal;
  if (++gDelayW >= kDelayBufMax) gDelayW = 0;

  return d;
}

/*──────────────────────────────────────────────────────────────────────────────
  REVERB ENGINE (Schroeder core + per-mode sweetening)
  RAM-safe: exact buffer sizes.
──────────────────────────────────────────────────────────────────────────────*/

// Exact line lengths (samples)
static const int C0 = 1557, C1 = 1617, C2 = 1491, C3 = 1422;
static const int kAP0Len = 225, kAP1Len = 556;  // (avoid A0/A1 pin name clash)

// Storage with exact sizes
static float gComb0[C0], gComb1[C1], gComb2[C2], gComb3[C3];
static float gAP0[kAP0Len], gAP1[kAP1Len];

static HSP_Comb gCombs[4];
static HSP_Allpass gAPs[2];

static int gPreDelaySamps = 0, gPreW = 0;
static float gPreBuf[(int)(kSR * 0.060f)];  // ≤60 ms predelay

// Cached reverb coefficients/state
static float gCombFb[4];
static HSP_OnePoleLPF gRvLPF;      // warm/dark LPF
static HSP_OnePoleLPF gHpShimmer;  // shimmer HPF
static float gTremPhase = 0.0f, gTremInc = 0.0f;
static float gChorPhase = 0.0f, gChorInc = 0.0f;
static float gShimAmt = 0.0f;
static int gMode = 0;

static void reverbInit() {
  gCombs[0].init(gComb0, C0);
  gCombs[1].init(gComb1, C1);
  gCombs[2].init(gComb2, C2);
  gCombs[3].init(gComb3, C3);

  gAPs[0].init(gAP0, kAP0Len, 0.5f);
  gAPs[1].init(gAP1, kAP1Len, 0.5f);

  for (size_t i = 0; i < sizeof(gPreBuf) / sizeof(gPreBuf[0]); ++i) gPreBuf[i] = 0.0f;
  gPreDelaySamps = (int)((tPreDelayMs / 1000.0f) * kSR);
  gPreW = 0;

  for (int i = 0; i < 4; ++i) gCombFb[i] = 0.7f;
  gRvLPF.setCutoff(6000.0f);
  gHpShimmer.setCutoff(tShimmerHPF_Hz);

  gTremPhase = gChorPhase = 0.0f;
  gTremInc = gChorInc = 0.0f;
  gShimAmt = 0.0f;
  gMode = 0;
}

static inline float reverbProcess(float in) {
  int preR = gPreW - gPreDelaySamps;
  while (preR < 0) preR += (int)(sizeof(gPreBuf) / sizeof(gPreBuf[0]));
  float preOut = gPreBuf[preR];
  gPreBuf[gPreW] = in;
  if (++gPreW >= (int)(sizeof(gPreBuf) / sizeof(gPreBuf[0]))) gPreW = 0;

  float s = 0.0f;
  for (int i = 0; i < 4; ++i) {
    gCombs[i].fb = gCombFb[i];
    s += gCombs[i].process(preOut);
  }
  s *= 0.25f;

  for (int i = 0; i < 2; ++i) s = gAPs[i].process(s);

  s = gRvLPF.process(s);

  switch (gMode) {
    case 0:
      {                                                                         // Spring: tremolo on wet
        float trem = 0.5f * (1.0f + sinf(2.0f * 3.14159265359f * gTremPhase));  // 0..1
        float depth = clampf(tTremDepth, 0.0f, 1.0f);
        s *= (1.0f - depth) + depth * trem;
        gTremPhase += gTremInc;
        if (gTremPhase >= 1.0f) gTremPhase -= 1.0f;
      }
      break;
    case 1:
      {  // Mod: tiny chorus-like motion
        float jitter = sinf(2.0f * 3.14159265359f * gChorPhase) * tChorusDepth;
        s += jitter * (s - gRvLPF.z) * 0.2f;
        gChorPhase += gChorInc;
        if (gChorPhase >= 1.0f) gChorPhase -= 1.0f;
      }
      break;
    case 2:  // Shimmer: bright octave-ish send amount
    case 3:
      {
        float bright = fabsf(in) - gHpShimmer.process(fabsf(in));
        s += gShimAmt * bright;
      }
      break;
  }
  return s;
}

/*──────────────────────────────────────────────────────────────────────────────
  AUDIO CALLBACK (v1.3.1 mono): one input sample → one output sample
──────────────────────────────────────────────────────────────────────────────*/

static void AudioCB(float in, float& out) {
  const bool feedEnable = gActive;
  const bool holdNow = gUI.fs_hold;  // already gated to false when bypassed

  float yDelay = delayProcess(in, feedEnable, holdNow);
  float yReverb = reverbProcess(yDelay);

  float wetSum = yDelay * gP.dBlend + yReverb * gP.rBlend;
  float mixGain = tClampWetMix ? clampf(gP.dBlend + gP.rBlend, 0.0f, 1.0f) : 1.0f;

  float yActive = in * (1.0f - mixGain) + wetSum * mixGain;
  float yBypass = tUseTailsBypass ? (in + wetSum) : in;

  float y = gActive ? yActive : yBypass;
  out = softLimit(y);
}

/*──────────────────────────────────────────────────────────────────────────────
  CONTROL SERVICE — run in loop() (never in audio)
──────────────────────────────────────────────────────────────────────────────*/

static void updateDerived() {
  // Pots (expo/safe mappings)
  gP.timeMs = expMap01(gUI.p[0], tDelayMinMs, tDelayMaxMs);
  gP.repeats = powf(gUI.p[1], tRepeatsCurveExp) * tRepeatsMax;
  gP.dBlend = clampf(gUI.p[2], 0.0f, 1.0f);

  gP.rt60 = expMap01(gUI.p[3], tRT60_MinSec, tRT60_MaxSec);
  gP.rBlend = clampf(gUI.p[4], 0.0f, 1.0f);
  gP.rateMorph01 = clampf(gUI.p[5], 0.0f, 1.0f);

  // Toggles
  gP.modOn = gUI.t[2];                                         // TS3
  const bool isWarm = tTS4_WarmDown ? (!gUI.t[3]) : gUI.t[3];  // TS4 mapping
  gP.warm = isWarm;
  gP.deepMod = tBrightSideDeepensDelayMod ? (!isWarm) : false;  // deeper mod only when Bright
  gP.modeBits = (gUI.t[0] ? 1 : 0) | (gUI.t[1] ? 2 : 0);        // TS1+TS2 → 0..3

  // Delay LFO rate
  gP.modRateHz = expMap01(gP.rateMorph01, tModRateMinHz, tModRateMaxHz);
  gModInc = gP.modOn ? (gP.modRateHz / kSR) : 0.0f;

  // Feedback low-pass follows repeats (more repeats → darker)
  gP.fbCutHz = expMap01(1.0f - gUI.p[1], tFbLPF_MinHz, tFbLPF_MaxHz);
  gFbTone.setCutoff(gP.fbCutHz);

  // Reverb: RT60 → comb feedbacks: fb ≈ 10^(−3*delay/RT60)
  const int combLen[4] = { C0, C1, C2, C3 };
  for (int i = 0; i < 4; ++i) {
    float delaySamps = (float)combLen[i];
    float fb = powf(10.0f, (-3.0f * delaySamps) / (gP.rt60 * kSR));
    gCombFb[i] = clampf(fb, 0.3f, 0.97f);
  }

  // Reverb warm/dark LPF
  gRvLPF.setCutoff(gP.warm ? tWarmCutHz : tBrightCutHz);

  // Per-mode sweetening setup
  gMode = (gP.modeBits & 0x3);
  switch (gMode) {
    case 0:
      {  // Spring
        float tremHz = expMap01(gP.rateMorph01, tTremMinHz, tTremMaxHz);
        gTremInc = tremHz / kSR;
        gChorInc = 0.0f;
        gShimAmt = 0.0f;
      }
      break;
    case 1:
      {  // Mod
        float chorHz = expMap01(gP.rateMorph01, tChorusMinHz, tChorusMaxHz);
        gChorInc = chorHz / kSR;
        gTremInc = 0.0f;
        gShimAmt = 0.0f;
      }
      break;
    case 2:  // Shimmer
    case 3:
      {
        gTremInc = gChorInc = 0.0f;
        float scale = (gMode == 3) ? tShimmerMode3Scale : tShimmerMode2Scale;
        gShimAmt = tShimmerMax * scale * gP.rateMorph01;
      }
      break;
  }
}

/*──────────────────────────────────────────────────────────────────────────────
  ARDUINO SETUP/LOOP — v1.3.1 API
──────────────────────────────────────────────────────────────────────────────*/

void setup() {
  H.Init(48000, 8);

  // Engines
  reverbInit();
  for (int i = 0; i < kDelayBufMax; ++i) gDelayBuf[i] = 0.0f;

  H.StartAudio(AudioCB);
}

void loop() {
  H.Idle();

  // Pots with mild smoothing (stable feel)
  gUI.p[0] = H.ReadPotSmoothed(RV1, tPotSmoothN);
  gUI.p[1] = H.ReadPotSmoothed(RV2, tPotSmoothN);
  gUI.p[2] = H.ReadPotSmoothed(RV3, tPotSmoothN);
  gUI.p[3] = H.ReadPotSmoothed(RV4, tPotSmoothN);
  gUI.p[4] = H.ReadPotSmoothed(RV5, tPotSmoothN);
  gUI.p[5] = H.ReadPotSmoothed(RV6, tPotSmoothN);

  // Toggles (debounced by library)
  gUI.t[0] = H.ReadToggle(TS1);
  gUI.t[1] = H.ReadToggle(TS2);
  gUI.t[2] = H.ReadToggle(TS3);
  gUI.t[3] = H.ReadToggle(TS4);

  // FS2 (active toggle)
  bool fs2 = H.FootswitchIsPressed(FS2);
  if (fs2 && !gPrevFS2) gActive = !gActive;
  gPrevFS2 = fs2;

  // FS1 (hold) — **read only when active** to avoid ticks in bypass
  bool fs1_now = gActive ? H.FootswitchIsPressed(FS1) : false;
  gUI.fs_hold = fs1_now;

  // LEDs: LED1 follows FS1; LED2 shows active state
  H.SetLED(LED1, fs1_now);
  H.SetLED(LED2, gActive);

  updateDerived();
}

/*──────────────────────────────────────────────────────────────────────────────
  USER GUIDE (controls, purpose, tips)
──────────────────────────────────────────────────────────────────────────────
  What it is
  ----------
  Dark, tape-ish delay before a lush reverb. Delay tops out near 650 ms; repeats
  lose a little top each lap. Modulation wobbles ONLY the repeats (dry stays in tune).
  Reverb modes: Spring (trem on wet), Mod (chorus thickness), Shimmer (bright octave-ish).

  Controls
  --------
  RV1  Delay Time      (50–650 ms, exponential mapping)
  RV2  Repeats         (feedback; “feel” shaped by tRepeatsCurveExp)
  RV3  Delay Blend     (delay amount into the mix)
  RV4  Reverb Decay    (RT60 ~0.4–12 s)
  RV5  Reverb Blend    (reverb amount into the mix)
  RV6  Rate/Morph      (per mode: trem speed / chorus speed / shimmer amount)

  TS1+TS2  Reverb Mode (00=Spring, 01=Mod, 10=Shimmer, 11=Bright-Shimmer)
  TS3      Delay Mod   (ON/OFF)
  TS4      Warm/Dark   (mapping controlled by tTS4_WarmDown; Bright can deepen mod if enabled)

  FS1  Hold (momentary; **ignored when bypassed**) — sets delay feedback near-unity for pads
  FS2  Active/Bypass   — tails behavior controlled by tUseTailsBypass
  LED1 lights when FS1 is pressed; LED2 lights when the effect is active.

  Why the math sounds musical (plain English)
  -------------------------------------------
  • Delay is a circular buffer. A triangle LFO nudges the *read* head so repeats
    wobble while dry stays stable. A 1-pole LPF in feedback “ages” repeats.
  • Reverb is four parallel combs → two allpasses for diffusion. RT60 maps to the
    comb feedbacks; Warm/Dark is a wet-tail low-pass. Modes add trem/chorus/shimmer.

  Quick presets
  -------------
  • Subtle room: RV3 ≈ 15%, RV5 40–60%, TS4=Warm, Mod slow.
  • Pads: Repeats high (not max), RV4 ≥ 3 s, Shimmer (TS1+TS2=10/11), RV6 past noon.
  • Rhythmic: Lower RV5, higher RV3, Mod OFF or Light.

  Next steps / mod ideas
  ----------------------
  1) True +1200-cent pitch-shift shimmer as a library helper (keeps this .ino simple).
  2) Order flip (reverb → delay) as a compile-time option if you want the 80s wash.
  3) Simple ducking sidechain to clear transients on picking.
  4) One-knob tilt-EQ utility on the wet path (reusable across sketches).
*/
