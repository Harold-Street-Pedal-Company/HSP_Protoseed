// =======================================================
// HSP_FX-15.ino — ProtoSEED v1.2 (mono via HaroldPCB)
// “Hot mic + cassette remix” tri-pass emulator
// FS2 = bypass (LED1 shows effect active). FS1 unused.
// Right output is muted by the library. Block size = 8.
// loop(): H.Idle(); readControls();  // library first, then your mappings
// =======================================================
//
// --------------------------- SIGNAL FLOW ----------------------------
//
//        Guitar In
//           │
//           ▼
//      [Pre HPF]
//           │
//     ┌─────┴───────────────────────────────────────────────┐
//     │                                                     │
//     │ Dry Path                                            │ Wet Path
//     │                                                     │
//     ▼                                                     ▼
//   Dry HPF ──► Dry LPF                              [Mic Pre Emu]
//     │                                                     │
//     │                                          ┌─────► Pass 1 (HPF+LPF+clip)
//     │                                          │
//     │                                          ├─────► Pass 2 (HPF+LPF+clip)
//     │                                          │
//     │                                          └─────► Pass 3 (HPF+LPF+clip)
//     │                                                     │
//     │                                           [Optional flutter + hiss]
//     │                                                     │
//     └───────────────► Mix (Dry/Wet crossfade) ◄────────────┘
//                                   │
//                            Master Volume
//                                   │
//                                   ▼
//                                Output
//
// ---------------------------------------------------------------------

#include <HaroldPCB.h>
#include <math.h>

#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

static HaroldPCB H;

// =======================================================
// TUNABLE PARAMETERS (edit these to change “voicing”)
// =======================================================

// Pass stages
static const int MIN_PASSES = 1;
static const int MAX_PASSES = 3;

// Drive
static const float DRIVE_MIN = 1.0f;
static const float DRIVE_MAX = 20.0f;

// Pre high-pass filter
static const float PREHPF_MIN_HZ = 80.0f;
static const float PREHPF_MAX_HZ = 180.0f;

// Base low-pass filter
static const float BASELPF_HI_HZ = 7800.0f;
static const float BASELPF_LO_HZ = 6000.0f;

// Noise
static const float HISS_LEVEL = 0.0025f;

// Flutter smear filters
static const float FLUTTER_A_HZ = 6.0f;
static const float FLUTTER_B_HZ = 6.9f;

// Defaults
static const float INIT_DRIVE = 5.0f;
static const float INIT_PREHPF_HZ = 120.0f;
static const float INIT_BASELPF_HZ = 7800.0f;
static const float INIT_MIX = 0.6f;
static const float INIT_MASTER = 0.8f;

// =======================================================
// Helpers
// =======================================================
static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float mixf(float a, float b, float t) {
  return a + (b - a) * t;
}
static inline float fast_tanh(float x) {
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}
static inline float soft_asym(float x, float asym) {
  const float b = 0.25f * asym;
  float a = fast_tanh(x + b);
  float c = fast_tanh(x - 0.15f * asym);
  return 0.6f * a + 0.4f * c;
}

// =======================================================
// One-pole filter
// =======================================================
struct OnePole {
  float a = 0.f, z = 0.f;
  void setLP(float sr, float fc) {
    fc = clampf(fc, 20.f, 20000.f);
    const float x = expf(-2.f * PI_F * fc / sr);
    a = 1.f - x;
  }
  float lp(float x) {
    z = z + a * (x - z);
    return z;
  }
  float hp(float x) {
    float y = lp(x);
    return x - y;
  }
};

// =======================================================
// White noise (for hiss)
// =======================================================
static uint32_t rng = 22222u;
static inline float white() {
  uint32_t x = rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng = x;
  return ((x >> 1) * (1.f / 2147483648.f)) * 2.f - 1.f;
}

// =======================================================
// Global DSP state
// =======================================================
static float gSR = 48000.f;
static OnePole preHPF, passLP[3], passHP[3], dryHP, dryLP;

struct Smooth {
  float v = 0.f;
  void set(float x) {
    v = x;
  }
  void toward(float t, float a) {
    v += (t - v) * a;
  }
};
static Smooth sm_drive, sm_mix, sm_master, sm_preHPF, sm_baseLPF;

struct Params {
  int passes = 3;
  float drive = INIT_DRIVE;
  float asym = 0.35f;
  float pre_hpf_hz = INIT_PREHPF_HZ;
  float base_lpf_hz = INIT_BASELPF_HZ;
  float mix = INIT_MIX;
  float master = INIT_MASTER;
  bool mic_tilt = false;
  bool tight_bw = false;
  bool hiss_on = false;
  bool flutter_on = false;
} P;

static OnePole fltA, fltB;
static bool fltInit = false;

static bool g_bypass = false;
static bool fs2_prev = false;

// =======================================================
// Pot/toggle wrappers
// =======================================================
static inline float pot(HPCB_Pot p) {
  return clampf(H.ReadPot(p), 0.f, 1.f);
}

// =======================================================
// Read controls
// =======================================================
static void readControls() {
  const float p1 = pot(RV1), p2 = pot(RV2), p3 = pot(RV3), p4 = pot(RV4), p5 = pot(RV5), p6 = pot(RV6);

  int passes = (int)floorf(p1 * MAX_PASSES) + MIN_PASSES;
  passes = passes < MIN_PASSES ? MIN_PASSES : (passes > MAX_PASSES ? MAX_PASSES : passes);

  const float drive = DRIVE_MIN + (DRIVE_MAX - DRIVE_MIN) * (p2 * p2);
  const float preHPFhz = PREHPF_MIN_HZ + (PREHPF_MAX_HZ - PREHPF_MIN_HZ) * p3;
  const float baseLPFhz = mixf(BASELPF_HI_HZ, BASELPF_LO_HZ, p4);
  const float mixwd = p5;
  const float master = p6;

  P.mic_tilt = H.ReadToggle(TS1);
  P.tight_bw = H.ReadToggle(TS2);
  P.hiss_on = H.ReadToggle(TS3);
  P.flutter_on = H.ReadToggle(TS4);
  P.passes = passes;

  sm_drive.toward(drive, 0.02f);
  sm_preHPF.toward(preHPFhz, 0.05f);
  sm_baseLPF.toward(baseLPFhz, 0.05f);
  sm_mix.toward(mixwd, 0.02f);
  sm_master.toward(master, 0.02f);

  preHPF.setLP(gSR, sm_preHPF.v);
  dryHP.setLP(gSR, 120.f);
  dryLP.setLP(gSR, 7800.f);

  const float baseLPF = sm_baseLPF.v;
  for (int i = 0; i < 3; ++i) {
    float fc = baseLPF - i * (P.tight_bw ? 1500.f : 1000.f);
    if (fc < BASELPF_LO_HZ) fc = BASELPF_LO_HZ;
    passLP[i].setLP(gSR, fc);
    passHP[i].setLP(gSR, 110.f + 10.f * i);
  }

  const bool fs2_now = H.FootswitchIsPressed(FS2);
  if (fs2_prev && !fs2_now) { g_bypass = !g_bypass; }
  fs2_prev = fs2_now;

  H.SetLED(LED1, g_bypass);
}

// =======================================================
// Building blocks
// =======================================================
static inline float mic_pre(float x) {
  if (P.mic_tilt) {
    const float hp = preHPF.hp(x);
    x = 0.75f * x + 0.35f * hp;
  } else {
    x = preHPF.hp(x);
  }
  float y = soft_asym(x * sm_drive.v, P.asym);
  return 0.75f * y;
}

static inline float pass_cell_idx(float x, int idx) {
  float y = soft_asym(x * 1.4f, 0.25f);
  y = passHP[idx].hp(y);
  y = passLP[idx].lp(y);
  return y;
}

static inline float flutter(float wet) {
  if (!P.flutter_on) return wet;
  if (!fltInit) {
    fltA.setLP(gSR, FLUTTER_A_HZ);
    fltB.setLP(gSR, FLUTTER_B_HZ);
    fltInit = true;
  }
  float a = fltA.lp(wet);
  float b = fltB.lp(wet);
  return 0.9f * wet + 0.1f * (a - b);
}

// =======================================================
// Audio callback
// =======================================================
static void AudioCB(float in, float &out) {
  if (g_bypass) {
    out = in;
    return;
  }

  float dry = in;
  dry = dryHP.hp(dry);
  dry = dryLP.lp(dry);

  float wet = mic_pre(in);
  if (P.passes >= 1) wet = pass_cell_idx(wet, 0);
  if (P.passes >= 2) wet = pass_cell_idx(wet, 1);
  if (P.passes >= 3) wet = pass_cell_idx(wet, 2);

  wet = flutter(wet);
  if (P.hiss_on) wet += white() * HISS_LEVEL;

  float y = mixf(dry, wet, clampf(sm_mix.v, 0.f, 1.f));
  y *= sm_master.v;
  out = clampf(y, -1.f, 1.f);
}

// =======================================================
// setup / loop
// =======================================================
void setup() {
  H.Init(48000, 8);
  H.StartAudio(AudioCB);

  gSR = (float)H.SampleRate();

  sm_drive.set(INIT_DRIVE);
  sm_preHPF.set(INIT_PREHPF_HZ);
  sm_baseLPF.set(INIT_BASELPF_HZ);
  sm_mix.set(INIT_MIX);
  sm_master.set(INIT_MASTER);

  preHPF.setLP(gSR, sm_preHPF.v);
  for (int i = 0; i < 3; ++i) {
    passLP[i].setLP(gSR, INIT_BASELPF_HZ - 1000.f * i);
    passHP[i].setLP(gSR, 110.f + 10.f * i);
  }
  dryHP.setLP(gSR, 120.f);
  dryLP.setLP(gSR, 7800.f);

  H.SetLED(LED1, g_bypass);
}

void loop() {
  H.Idle();
  readControls();
}

// =======================================================
// USER GUIDE
// =======================================================
//
// What this pedal does:
// - Emulates an overdriven microphone pushed onto cassette.
// - Audio is split into a “dry” sweetened path and a “wet”
//   tri-pass path with distortion+filter stages.
// - Optional tape-like flutter and hiss can be blended in.
// - FS2 toggles bypass; LED1 shows when effect is active.
//
// Pots:
// - RV1: Pass count (1–3 stages)
// - RV2: Drive
// - RV3: Pre high-pass (80–180 Hz)
// - RV4: Base low-pass (7.8k → 6k)
// - RV5: Dry/Wet mix
// - RV6: Master output
//
// Toggles:
// - TS1: Mic tilt (restores some highs before drive)
// - TS2: Tight band (narrows stage spacing)
// - TS3: Add hiss (noise layer)
// - TS4: Flutter (subtle tape wobble)
//
// Experiments:
// - Compare 1 vs 3 passes for subtle vs crushed tone.
// - Crank drive with mic tilt ON for raspy mic breakup.
// - Blend dry/wet for clarity vs lofi grit.
// - Add flutter+hiss for instant cassette vibe.
//
