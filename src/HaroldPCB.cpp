#include "HaroldPCB.h"

// -----------------------------------------------------------------------------
// HaroldPCB.cpp  — v1.3.0 runtime (DaisyDuino begin API, PascalCase setters)
// -----------------------------------------------------------------------------

static HaroldPCB* _g_instance = nullptr;

void _BlockCB(float **in, float **out, size_t sz)
{
    HaroldPCB* h = _g_instance;
    if(!h || !h->cb_) return;

    for(size_t i = 0; i < sz; i++)
    {
        float o = 0.0f;
        float x = in[0][i];
        h->cb_(x, o);
        out[0][i] = o * h->master_level_;
        out[1][i] = 0.0f; // mono: right muted
    }
}

float HaroldPCB::_applyCurve(float v, HPCB_Curve c)
{
    if(v < 0.0f) v = 0.0f;
    if(v > 1.0f) v = 1.0f;

    switch(c)
    {
        case HPCB_Curve::Log10: return (powf(10.0f, v) - 1.0f) / 9.0f;
        case HPCB_Curve::Exp10: return log10f(1.0f + 9.0f * v);
        default:                 return v;
    }
}

void HaroldPCB::_serviceMaster()
{
    if(master_bound_)
    {
        float raw = ReadPot(master_src_);
        master_level_ = _applyCurve(raw, master_curve_);
    }
}

void HaroldPCB::_serviceFootswitches()
{
    const uint32_t now = millis();

    for(int i = 0; i < HPCB_NUM_FOOTSWITCHES; i++)
    {
        const bool raw_level = !digitalRead(footswitch_pins_[i]);

        if(raw_level != fs_pressed_[i] && (now - fs_last_change_[i]) >= fs_timing_.debounce_ms)
        {
            fs_pressed_[i]     = raw_level;
            fs_last_change_[i] = now;

            if(raw_level)
            {
                const uint32_t gap = now - fs_last_press_time_[i];
                fs_last_press_time_[i] = now;
                fs_click_count_[i]++;

                if(gap <= fs_timing_.multiclick_gap_ms && fs_click_count_[i] == 2)
                    fs_evt_double_[i] = true;
            }
            else
            {
                const uint32_t held = now - fs_last_press_time_[i];
                if(held >= fs_timing_.longpress_ms)
                {
                    if(fs_click_count_[i] == 1)      fs_evt_long_[i]       = true;
                    else if(fs_click_count_[i] == 2) fs_evt_doublelong_[i] = true;
                }
                fs_click_count_[i] = 0;
            }
        }
    }
}

bool HaroldPCB::Init(uint32_t sample_rate_hz, uint16_t block_size)
{
    sr_    = sample_rate_hz ? sample_rate_hz : 48000;
    block_ = block_size ? block_size : 8;
    _g_instance = this;

    for(int i = 0; i < HPCB_NUM_POTS; i++)         pinMode(pot_pins_[i], INPUT);
    for(int i = 0; i < HPCB_NUM_TOGGLES; i++)      pinMode(toggle_pins_[i], INPUT_PULLUP);
    for(int i = 0; i < HPCB_NUM_FOOTSWITCHES; i++) pinMode(footswitch_pins_[i], INPUT_PULLUP);
    for(int i = 0; i < HPCB_NUM_LEDS; i++)         pinMode(led_pins_[i], OUTPUT);

    // Daisy hardware/audio
    DAISY.init(DAISY_SEED);
    DAISY.SetAudioBlockSize(block_);
    DAISY.SetAudioSampleRate((daisy::SaiHandle::Config::SampleRate)sr_);

    return true;
}

bool HaroldPCB::StartAudio(HPCB_AudioCB_Mono cb_mono)
{
    cb_ = cb_mono;
    if(!cb_) return false;
    DAISY.begin(_BlockCB);
    return true;
}

void HaroldPCB::StopAudio() { DAISY.end(); }

void HaroldPCB::Idle()
{
    _serviceMaster();
    _serviceFootswitches();
}

float HaroldPCB::ReadPot(uint8_t index)
{
    if(index >= HPCB_NUM_POTS) return 0.0f;
    return analogRead(pot_pins_[index]) / 1023.0f; // 10-bit Daisy ADC
}

float HaroldPCB::ReadPotMapped(uint8_t index, float min, float max, HPCB_Curve curve)
{
    float v = ReadPot(index);
    return min + (max - min) * _applyCurve(v, curve);
}

float HaroldPCB::ReadPotSmoothed(uint8_t index, float smooth_ms)
{
    float v = ReadPot(index);
    if(index >= HPCB_NUM_POTS) return v;

    if(smooth_ms <= 0.0f) { pot_state_[index] = v; return v; }

    const float blocks_per_second = (float)sr_ / (float)block_;
    const float a = 1.0f - expf(-1.0f / (smooth_ms * (blocks_per_second / 1000.0f)));
    pot_state_[index] += a * (v - pot_state_[index]);
    return pot_state_[index];
}

bool HaroldPCB::ReadToggle(uint8_t index) const
{
    if(index >= HPCB_NUM_TOGGLES) return false;
    return !digitalRead(toggle_pins_[index]);
}

bool HaroldPCB::FootswitchIsPressed(uint8_t index) const
{
    if(index >= HPCB_NUM_FOOTSWITCHES) return false;
    return fs_pressed_[index];
}

bool HaroldPCB::FootswitchIsReleased(uint8_t index) const
{
    return !FootswitchIsPressed(index);
}

bool HaroldPCB::FootswitchIsLongPressed(uint8_t index)
{
    if(index >= HPCB_NUM_FOOTSWITCHES) return false;
    bool f = fs_evt_long_[index];
    fs_evt_long_[index] = false;
    return f;
}

bool HaroldPCB::FootswitchIsDoublePressed(uint8_t index)
{
    if(index >= HPCB_NUM_FOOTSWITCHES) return false;
    bool f = fs_evt_double_[index];
    fs_evt_double_[index] = false;
    return f;
}

bool HaroldPCB::FootswitchIsDoubleLongPressed(uint8_t index)
{
    if(index >= HPCB_NUM_FOOTSWITCHES) return false;
    bool f = fs_evt_doublelong_[index];
    fs_evt_doublelong_[index] = false;
    return f;
}

void HaroldPCB::SetFootswitchTiming(const HPCB_FootswitchTiming &t) { fs_timing_ = t; }
void HaroldPCB::SetDebounce(uint16_t ms)      { fs_timing_.debounce_ms       = ms; }
void HaroldPCB::SetLongPress(uint16_t ms)     { fs_timing_.longpress_ms      = ms; }
void HaroldPCB::SetMultiClickGap(uint16_t ms) { fs_timing_.multiclick_gap_ms = ms; }

void HaroldPCB::SetLED(uint8_t index, bool on)
{
    if(index >= HPCB_NUM_LEDS) return;
    digitalWrite(led_pins_[index], on); 

}
