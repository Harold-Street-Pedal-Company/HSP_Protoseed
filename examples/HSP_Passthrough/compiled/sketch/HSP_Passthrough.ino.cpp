#line 1 "C:\\Users\\oryan\\Documents\\Arduino\\libraries\\HaroldPCB\\examples\\Passthrough\\HSP_Passthrough\\HSP_Passthrough.ino"
/*
    HaroldPCB Mono Passthrough Example
    ----------------------------------
    This example takes audio from the *left* input, sends it through the pedal
    without changing it, and sends it straight to the *left* output.

    - The right channel is intentionally silent.
    - This uses HaroldPCB's "Idle()" function to handle all the board's
      background work (like footswitch debouncing and LED updates).

    Written for absolute beginners.
*/

#include <Arduino.h>     // Core Arduino functions (setup, loop, etc.)
#include <HaroldPCB.h>   // Our custom library for Harold Street Pedal hardware

// Create an instance of the HaroldPCB class
// This object manages *all* the hardware: pots, switches, LEDs, and audio I/O.
HaroldPCB hpcb;

/*
    AudioCallback()
    ---------------
    This is the function that processes the audio.
    HaroldPCB calls it automatically, one block of samples at a time.

    - `in` is the incoming audio sample from the LEFT input.
    - `out` is where we put the processed sample to send to the LEFT output.
    - The RIGHT channel is ignored here — it stays silent.
*/
#line 31 "C:\\Users\\oryan\\Documents\\Arduino\\libraries\\HaroldPCB\\examples\\Passthrough\\HSP_Passthrough\\HSP_Passthrough.ino"
void AudioCallback(float in, float &out);
#line 44 "C:\\Users\\oryan\\Documents\\Arduino\\libraries\\HaroldPCB\\examples\\Passthrough\\HSP_Passthrough\\HSP_Passthrough.ino"
void setup();
#line 67 "C:\\Users\\oryan\\Documents\\Arduino\\libraries\\HaroldPCB\\examples\\Passthrough\\HSP_Passthrough\\HSP_Passthrough.ino"
void loop();
#line 31 "C:\\Users\\oryan\\Documents\\Arduino\\libraries\\HaroldPCB\\examples\\Passthrough\\HSP_Passthrough\\HSP_Passthrough.ino"
void AudioCallback(float in, float &out) {
    out = in; // No processing — just pass it through.
}

/*
    setup()
    -------
    This runs once when the pedal powers on.

    Steps:
    1. Initialize the HaroldPCB hardware.
    2. Start the audio engine, using our `AudioCallback` function above.
*/
void setup() {
    hpcb.Init();                    // Set up pots, switches, LEDs, audio, etc.
    hpcb.StartAudio(AudioCallback); // Begin audio processing (mono version)
}

/*
    loop()
    ------
    This runs over and over, forever.
    - We don't put audio code here (it's handled in the callback).
    - Instead, we handle background tasks and input polling.

    hpcb.Idle() does the following automatically:
      - Checks the footswitches and debounces them.
      - Updates LED states.
      - Reads toggles and pots.
      - Keeps everything responsive.

    NOTE for learners:
      Idle() is the *easy mode* way to handle controls.
      You can also read footswitches, pots, and toggles manually if you want
      more control or special behavior — that will be the **next lesson**.
*/
void loop() {
    hpcb.Idle(); // Run the HaroldPCB background tasks continuously.
}

