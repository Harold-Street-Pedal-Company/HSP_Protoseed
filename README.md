# HSP Protoseed

Beginner-friendly Arduino library for quick **guitar pedal prototyping** on **Daisy Seed** (HSP v1.2 hardware). It handles audio, pots, toggles, LEDs, and footswitch events with simple, clear calls—so you can focus on creating and testing pedal ideas fast.

- **Mono audio** (Left in/out). The library **mutes Right out** for you.
- **Controls**: 6 pots, 4 toggles, 2 footswitches, 2 LEDs (active-low).
- **No RTOS, no backend**—just Arduino sketches.

> **License:** GNU GPLv3

---

## Table of contents

- [Quick start](#quick-start)
- [Project structure](#project-structure)
- [API reference](#api-reference)
  - [Types](#types)
  - [Core](#core)
  - [Controls](#controls)
  - [LEDs](#leds)
  - [Idle loop](#idle-loop)
- [Hardware notes](#hardware-notes)
- [FAQ](#faq)
- [Contributing](#contributing)
- [License](#license)

---

## Quick start

```cpp
#include <HaroldPCB.h>

HaroldPCB hpcb;

// Your mono audio callback: in (L) ➜ out (L). R is muted by the library.
void AudioCallback(float in, float &out) {
  // simple passthrough:
  out = in;
}

void setup() {
  // Init audio @48k / blocksize 48 (both have defaults if omitted)
  hpcb.Init(48000, 48);
  hpcb.StartAudio(AudioCallback);
}

void loop() {
  // Keep controls responsive (debounce, smoothing, etc.)
  hpcb.Idle();

  // Example reads:
  float gain = hpcb.ReadPotSmoothed(0, 25.0f);  // pot0 smoothed over ~25 ms
  bool  on   = hpcb.ReadToggle(0);              // toggle0 (active-low hardware)

  // LED1 mirrors toggle for fun:
  hpcb.SetLED(1, on);
}
```

> ℹ️ **Heads-up:** `Idle()` is the *easiest* place to read footswitches and toggles in a simple sketch.  
> It’s **not the only way**—you can also read switches in the audio callback (carefully).  
> We’ll cover that approach in a later lesson.

---

## Project structure

```
HSP_Protoseed/
├─ src/
│  ├─ HaroldPCB.h
│  ├─ HaroldPCB.cpp
│  ├─ ThirdPartyDaisy.h        # vendored DaisyDuino shim
│  └─ vendor/…                  # vendored Daisy support sources (no conflicts)
├─ examples/
│  └─ Passthrough/
│     └─ HSP_Passthrough.ino
├─ README.md
└─ library.properties
```

Install like any Arduino library (clone into your Arduino `libraries` folder or use Zip import).

---

## API reference

### Types

```cpp
// Mono per-sample audio callback
using HPCB_AudioCB_Mono = void (*)(float in, float &out);

// Pot mapping curve
enum class HPCB_Curve : uint8_t { Linear, Log10, Exp10 };
```

- **`HPCB_AudioCB_Mono`** — Your audio function. Library passes **Left in** as `in` and expects you to fill **Left out** via `out`. **Right out is muted** internally.
- **`HPCB_Curve`** — Helper for mapping pot values to your desired range:
  - `Linear` (default) — direct 0..1 mapping
  - `Log10` — more resolution near 0 (great for gain)
  - `Exp10` — more resolution near 1

---

### Core

```cpp
bool Init(uint32_t sample_rate_hz = 48000, uint16_t block_size = 48);
```
Initialize hardware (GPIO, ADCs, LEDs) and the audio engine.  
Returns `true` on success. Supported sample rates: **48000** and **96000**.

```cpp
bool StartAudio(HPCB_AudioCB_Mono cb_mono);
```
Start audio with your mono per-sample callback.  
Returns `false` if `cb_mono` is `nullptr`.

```cpp
void StopAudio();
```
Stop the audio engine.

```cpp
uint32_t SampleRate() const;
```
Returns the active sample rate (Hz).

---

### Controls

All pot indices are **0..5**, toggles **0..3**, footswitches **0..1**.

```cpp
float ReadPot(uint8_t index);
```
Raw pot read in **0.0..1.0** (unfiltered). Out of range index ➜ `0.0f`.

```cpp
float ReadPotMapped(uint8_t index, float min, float max,
                    HPCB_Curve curve = HPCB_Curve::Linear);
```
Pot mapped directly into `[min, max]` using the chosen curve.

```cpp
float ReadPotSmoothed(uint8_t index, float smooth_ms);
```
Low-pass filtered pot in **0.0..1.0**.  
`smooth_ms` is the time constant; `0` means “no smoothing”.

```cpp
bool ReadToggle(uint8_t index) const;
```
Read toggle switch. Returns `true` when the switch is **ON**.  
(Handles the **active-low** hardware for you.)

```cpp
bool FootswitchIsPressed(uint8_t index) const;
bool FootswitchIsReleased(uint8_t index) const;
```
Momentary footswitch state. `Pressed` is **active-low** hardware normalized to `true`.  
> Note: long-press / double-press helpers are stubbed for now and reserved for a future update.

---

### LEDs

```cpp
void SetLED(uint8_t index, bool on);
```
Turn LED **on/off** (library inverts for the **active-low** hardware).

---

### Idle loop

```cpp
void Idle();
```

Call this **frequently** (e.g. every `loop()` iteration). It keeps control reads responsive and is where future debouncing/timing helpers will live.

> **Note:** You *can* read switches inside the audio callback for absolute lowest latency, but you’ll need to be careful about blocking and timing. `Idle()` is the recommended place for most sketches. We’ll show the audio-thread approach in the next lesson.

---

## Hardware notes

- **Channels:** Mono Left in/out. Right out is **muted** by the library.
- **Controls:**  
  - Pots: **6** (`index 0..5`)  
  - Toggles: **4** (`index 0..3`, normalized so `true` == ON)  
  - Footswitches: **2** (`index 0..1`, normalized so `true` == pressed)  
  - LEDs: **2** (`index 0..1`, `SetLED(i, true)` turns them ON despite active-low wiring)

---

## FAQ

**Why is Right out silent?**  
This library focuses on **mono pedal prototyping**. Mirroring left to right is trivial if you want it, but we default to **muted Right** to keep noise down on builds with only a Left jack.

**What sample rates are supported?**  
`Init()` supports **48k** and **96k** (pass exactly `48000` or `96000`).

**Can I skip `Idle()`?**  
You can, but your switches/pots won’t feel as snappy. `Idle()` is lightweight—call it every `loop()`.

---

## Contributing

Issues and PRs welcome:
- Keep examples tiny and well-commented.
- Avoid pulling another copy of DaisyDuino—use the vendored shim.
- No breaking changes to the public API without discussion.

---

## License

This project is licensed under the **GNU GPLv3**. See `LICENSE` for details.

---

### Changelog (snippet)

- **1.1.0** — Public release; vendored Daisy shim; Right out muted; clean, minimal API.
