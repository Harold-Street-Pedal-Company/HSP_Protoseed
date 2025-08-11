# HSP_Protoseed (HaroldPCB)

Beginner-friendly Arduino library for **Daisy Seed** pedal prototyping on the Harold Street Pedal Company PCB.  
It wraps audio bring-up, pots/toggles/footswitch reads, and LED control into a tiny API with sane defaults. Vendored DaisyDuino is included, so no extra installs.

- **Mono audio path**: processes **left** input → **left** output. Right output is **silent** by default.  
- **LEDs are active-low**.  
- **Idle()** keeps the control system responsive (debounce, state updates).  

License: **GNU GPLv3**

## Table of contents
- [Install](#install)
- [Hardware assumptions](#hardware-assumptions)
- [Folder layout](#folder-layout)
- [Quick start](#quick-start)
- [API reference](#api-reference)
  - [Core](#core)
  - [Controls](#controls)
  - [LEDs](#leds)
  - [Idle / background](#idle--background)
  - [Enums & structs](#enums--structs)
- [Behavior notes](#behavior-notes)
- [Versioning](#versioning)
- [Contributing](#contributing)

## Install
1. Clone or download this repo.
2. Put the folder into your Arduino libraries dir:
   - **Windows**: `C:\Users\<you>\Documents\Arduino\libraries\HSP_Protoseed`
3. Restart Arduino IDE.

> We vendor DaisyDuino under `src/vendor/daisy`, so you don’t need to install it separately.

## Hardware assumptions
- **Board**: Daisy Seed
- **Sample rate**: 48 kHz default (96 kHz supported via `Init(96000, ...)`)
- **Block size**: 48 samples by default
- **Mono pipeline**: left in → left out; right out is muted

### HaroldPCB v1.2 pin map (used internally)
- **Pots (6)**: `A6, A5, A4, A3, A2, A1` (0…1 normalized)
- **Toggles (4)**: `10, 9, 8, 7` (active-low)
- **Footswitches (2)**: `26, 25` (active-low)
- **LEDs (2)**: `22, 23` (active-low)

You address these by **index** (0-based). You don’t pass pin numbers.

## Folder layout
HSP_Protoseed/
README.md
library.properties
src/
HaroldPCB.h
HaroldPCB.cpp
ThirdPartyDaisy.h
vendor/
daisy/
src/...
utility/...
examples/
passthrough/
passthrough.ino

csharp
Copy
Edit

## Quick start
```cpp
#include <HaroldPCB.h>

HaroldPCB hpcb;

// Your mono audio callback: in (L) → out (L). R is muted by the library.
void AudioCallback(float in, float &out) { out = in; }

void setup() {
  hpcb.Init(48000, 48);         // rate, blocksize (defaults if omitted)
  hpcb.StartAudio(AudioCallback);
}

void loop() {
  hpcb.Idle();                  // keep controls responsive (debounce, etc.)
}
```

API reference
Core
bool Init(uint32_t sample_rate_hz = 48000, uint16_t block_size = 48)
Initialize hardware (GPIO, ADCs, LEDs) and audio engine. Returns true.

bool StartAudio(HPCB_AudioCB_Mono cb)
Start audio with a mono per-sample callback void cb(float in, float &out).

void StopAudio()
Stop the audio engine.

uint32_t SampleRate() const
Return active sample rate.

Controls
float ReadPot(uint8_t index) → 0.0…1.0 normalized.
float ReadPotMapped(uint8_t index, float min, float max, HPCB_Curve curve=Linear)
Curves: Linear, Log10 (more low-end resolution), Exp10 (more high-end).
float ReadPotSmoothed(uint8_t index, float smooth_ms)
One-pole smoothing with time constant in ms.
bool ReadToggle(uint8_t index) const
Active-low; true means switch is ON/closed.
bool FootswitchIsPressed(uint8_t index) const, bool FootswitchIsReleased(uint8_t index) const
Active-low instantaneous state.
Note: Long/double press helpers are present but not yet implemented (return false).

LEDs
void SetLED(uint8_t index, bool on)
Active-low output; on=true drives pin LOW (LED ON).

Idle / background
void Idle()
Runs background housekeeping (polls controls, placeholder for debounce/event FSM).
You can handle footswitches manually; Idle() is the easy-mode path. A follow-up lesson will cover manual handling.

Enums & structs
enum class HPCB_Curve : uint8_t { Linear, Log10, Exp10 };
struct HPCB_FootswitchTiming { uint16_t debounce_ms=12, longpress_ms=500, multiclick_gap_ms=300; };
Present for forward compatibility.

Behavior notes
Mono per-sample callback; right channel muted by default.

Active-low: toggles, footswitches, LEDs.

ADC pots are 10-bit mapped to 0.0–1.0.

Out-of-range indices return safe defaults.

Versioning
v0.1.0 – first public release (mono path, controls, LEDs, Idle scaffold, vendored DaisyDuino, passthrough example)

Contributing
PRs welcome. C++17, Arduino constraints (no RTTI/exceptions).

Maintain active-low semantics and 0-based indexing.

Verify: 48k/96k start, 0…1 pot reads, LED logic (LOW=on), right channel silent.

