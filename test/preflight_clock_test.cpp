/**
 * PreFlightClock Test Executable
 *
 * Tests the reset/run timing of PreFlightClock to verify that:
 * - Reset fires exactly on 0.0.0 (the downbeat when RUNNING starts)
 * - NOT during the count-in
 *
 * Usage:
 *   ./preflight_clock_test
 *   ./preflight_clock_test --verbose
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <cassert>

// ============================================================================
// Minimal VCV Rack stubs
// ============================================================================

namespace rack {
    struct Plugin;

    inline float clamp(float x, float lo, float hi) {
        return std::max(lo, std::min(hi, x));
    }

    struct Module {
        struct Param {
            float value = 0.f;
            float getValue() const { return value; }
            void setValue(float v) { value = v; }
        };
        struct Input {
            float voltage = 0.f;
            bool connected = false;
            float getVoltage() const { return voltage; }
            void setVoltage(float v) { voltage = v; }
            bool isConnected() const { return connected; }
        };
        struct Output {
            float voltage = 0.f;
            float getVoltage() const { return voltage; }
            void setVoltage(float v) { voltage = v; }
        };
        struct Light {
            float brightness = 0.f;
            void setBrightness(float b) { brightness = b; }
        };

        struct ProcessArgs {
            float sampleTime;
            float sampleRate;
        };

        struct Expander {
            Module* module = nullptr;
            void* producerMessage = nullptr;
            void* consumerMessage = nullptr;
            void requestMessageFlip() {}
        };
        Expander rightExpander;
        Expander leftExpander;

        std::vector<Param> params;
        std::vector<Input> inputs;
        std::vector<Output> outputs;
        std::vector<Light> lights;

        void config(int numParams, int numInputs, int numOutputs, int numLights) {
            params.resize(numParams);
            inputs.resize(numInputs);
            outputs.resize(numOutputs);
            lights.resize(numLights);
        }

        void configParam(int id, float min, float max, float def, const std::string& name, const std::string& unit = "") {
            if (id >= 0 && id < (int)params.size()) params[id].value = def;
        }
        void configButton(int id, const std::string& name) {}
        void configSwitch(int id, float min, float max, float def, const std::string& name, const std::vector<std::string>& labels = {}) {
            if (id >= 0 && id < (int)params.size()) params[id].value = def;
        }
        void configInput(int id, const std::string& name) {}
        void configOutput(int id, const std::string& name) {}
        void configLight(int id, const std::string& name) {}

        virtual void onReset() {}
    };

    namespace dsp {
        struct SchmittTrigger {
            bool state = false;
            bool process(float in, float lo = 0.1f, float hi = 1.f) {
                if (state) {
                    if (in <= lo) { state = false; }
                } else {
                    if (in >= hi) { state = true; return true; }
                }
                return false;
            }
        };

        struct PulseGenerator {
            float remaining = 0.f;
            void trigger(float duration = 1e-3f) { remaining = duration; }
            bool process(float dt) {
                if (remaining > 0.f) {
                    remaining -= dt;
                    return remaining > 0.f || remaining + dt > 0.f;
                }
                return false;
            }
        };
    }
}

using namespace rack;

struct Model;
Plugin* pluginInstance = nullptr;

// Stub json_t for dataToJson/dataFromJson
struct json_t;

// ============================================================================
// Include the PreFlightClock module logic inline (extract from the .cpp)
// We replicate the module struct here to avoid Widget/GUI dependencies
// ============================================================================

struct PreFlightClock : Module {
    enum ParamId {
        BPM_PARAM,
        PLAY_PARAM,
        STOP_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        BPM_CV_INPUT,
        PLAY_TRIG_INPUT,
        STOP_TRIG_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        METRO_OUTPUT,
        RESET_OUTPUT,
        RUN_OUTPUT,
        CLOCK_OUTPUT,
        MASTER_OUTPUT,
        X2_OUTPUT,
        X3_OUTPUT,
        X4_OUTPUT,
        X8_OUTPUT,
        X16_OUTPUT,
        X32_OUTPUT,
        DIV1_5_OUTPUT,
        DIV2_OUTPUT,
        DIV3_OUTPUT,
        DIV4_OUTPUT,
        DIV8_OUTPUT,
        DIV16_OUTPUT,
        DIV32_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        STATE_RED_LIGHT,
        STATE_GREEN_LIGHT,
        STATE_BLUE_LIGHT,
        BEAT_LIGHT,
        LIGHTS_LEN
    };

    enum State {
        STOPPED,
        COUNTING_IN,
        RUNNING
    };

    double beatPhase = 0.0;
    double prevBeatPhase = 0.0;
    State state = STOPPED;
    int countInBeats = 0;
    bool waitingForBeat = false;

    float metroPhase = 0.f;
    float metroEnvelope = 0.f;
    float metroFreq = 440.f;
    float metroDecayTime = 0.05f;

    dsp::SchmittTrigger playTrigger;
    dsp::SchmittTrigger stopTrigger;
    dsp::SchmittTrigger playButtonTrigger;
    dsp::SchmittTrigger stopButtonTrigger;
    dsp::PulseGenerator resetPulse;
    dsp::PulseGenerator masterPulse;
    dsp::PulseGenerator clockPulse;
    dsp::PulseGenerator multPulses[6];
    dsp::PulseGenerator divPulses[7];

    float beatBrightness = 0.f;
    std::atomic<int> displayCount{0};
    std::atomic<int> displayState{0};

    PreFlightClock() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        params[BPM_PARAM].value = 120.f;
    }

    void process(const ProcessArgs& args) {
        float dt = args.sampleTime;
        float bpm = params[BPM_PARAM].getValue();
        if (inputs[BPM_CV_INPUT].isConnected()) {
            float cv = inputs[BPM_CV_INPUT].getVoltage();
            bpm += (cv / 10.f) * 270.f;
        }
        bpm = rack::clamp(bpm, 30.f, 300.f);

        bool playTriggered = false;
        bool stopTriggered = false;

        if (playButtonTrigger.process(params[PLAY_PARAM].getValue())) playTriggered = true;
        if (playTrigger.process(inputs[PLAY_TRIG_INPUT].getVoltage(), 0.1f, 1.f)) playTriggered = true;
        if (stopButtonTrigger.process(params[STOP_PARAM].getValue())) stopTriggered = true;
        if (stopTrigger.process(inputs[STOP_TRIG_INPUT].getVoltage(), 0.1f, 1.f)) stopTriggered = true;

        if (stopTriggered) {
            state = STOPPED;
            metroEnvelope = 0.f;
            waitingForBeat = false;
            countInBeats = 0;
        }

        if (playTriggered && state == STOPPED) {
            state = COUNTING_IN;
            waitingForBeat = true;
            countInBeats = 4;
        }

        double delta = static_cast<double>(bpm) / (60.0 * args.sampleRate);
        prevBeatPhase = beatPhase;
        beatPhase += delta;

        bool masterBeat = (std::floor(beatPhase) != std::floor(prevBeatPhase));

        if (masterBeat) {
            masterPulse.trigger(1e-3f);
            clockPulse.trigger(1e-3f);
            beatBrightness = 1.f;

            if (state == COUNTING_IN) {
                if (waitingForBeat) {
                    waitingForBeat = false;
                }

                if (countInBeats > 0) {
                    if (countInBeats == 4) {
                        // First count-in beat (-3): accented "tock"
                        metroPhase = 0.f;
                        metroFreq = 880.f;
                        metroEnvelope = 1.f;
                        metroDecayTime = 0.08f;
                    } else {
                        // Remaining beats (-2, -1, 0): regular "tic"
                        metroPhase = 0.f;
                        metroFreq = 440.f;
                        metroEnvelope = 0.7f;
                        metroDecayTime = 0.04f;
                    }
                    countInBeats--;
                } else {
                    state = RUNNING;
                    resetPulse.trigger(1e-3f);
                    // No metronome beep on the downbeat
                }
            }
        }

        // Multiplied/divided beat detection (not needed for test, but keep for completeness)
        static const float multRatios[] = {2.f, 3.f, 4.f, 8.f, 16.f, 32.f};
        for (int i = 0; i < 6; i++) {
            double r = static_cast<double>(multRatios[i]);
            if (std::floor(beatPhase * r) != std::floor(prevBeatPhase * r))
                multPulses[i].trigger(1e-3f);
        }
        static const double divRatios[] = { 2.0/3.0, 0.5, 1.0/3.0, 0.25, 0.125, 0.0625, 0.03125 };
        for (int i = 0; i < 7; i++) {
            if (std::floor(beatPhase * divRatios[i]) != std::floor(prevBeatPhase * divRatios[i]))
                divPulses[i].trigger(1e-3f);
        }

        // Metronome synthesis
        float metroOut = 0.f;
        if (metroEnvelope > 0.001f) {
            metroOut = std::sin(2.f * M_PI * metroPhase) * metroEnvelope;
            metroPhase += metroFreq * dt;
            if (metroPhase >= 1.f) metroPhase -= 1.f;
            metroEnvelope *= std::exp(-dt / metroDecayTime);
        }

        outputs[METRO_OUTPUT].setVoltage(metroOut * 5.f);
        outputs[RESET_OUTPUT].setVoltage(resetPulse.process(dt) ? 10.f : 0.f);
        outputs[RUN_OUTPUT].setVoltage(state == RUNNING ? 10.f : 0.f);
        outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(dt) ? 10.f : 0.f);

        bool isRunning = (state == RUNNING);
        outputs[MASTER_OUTPUT].setVoltage((isRunning && masterPulse.process(dt)) ? 10.f : 0.f);
        for (int i = 0; i < 6; i++) {
            bool pulse = multPulses[i].process(dt);
            outputs[X2_OUTPUT + i].setVoltage((isRunning && pulse) ? 10.f : 0.f);
        }
        for (int i = 0; i < 7; i++) {
            bool pulse = divPulses[i].process(dt);
            outputs[DIV1_5_OUTPUT + i].setVoltage((isRunning && pulse) ? 10.f : 0.f);
        }

        lights[BEAT_LIGHT].setBrightness(beatBrightness);
        displayState.store(static_cast<int>(state));
        displayCount.store(countInBeats);
    }
};

// ============================================================================
// Test helpers
// ============================================================================

struct BeatEvent {
    int beatNumber;        // Sequential beat count from play
    double beatPhase;      // Phase at this beat
    bool resetHigh;        // Was RESET_OUTPUT high?
    bool runHigh;          // Was RUN_OUTPUT high?
    bool masterClockHigh;  // Was MASTER_OUTPUT high?
    bool clockHigh;        // Was CLOCK_OUTPUT high?
    int state;             // Module state
};

// Process N samples, return events for each master beat detected
std::vector<BeatEvent> simulatePlaySequence(float bpm, float sampleRate, bool verbose) {
    PreFlightClock clock;
    Module::ProcessArgs args;
    args.sampleTime = 1.f / sampleRate;
    args.sampleRate = sampleRate;

    std::vector<BeatEvent> events;

    // Run a few samples in stopped state first
    for (int i = 0; i < 100; i++)
        clock.process(args);

    // Press play (set button high for one sample, then low)
    clock.params[PreFlightClock::PLAY_PARAM].setValue(10.f);
    clock.process(args);
    clock.params[PreFlightClock::PLAY_PARAM].setValue(0.f);

    // Now run through enough samples for count-in + several running beats
    // At 120 BPM, one beat = 0.5s = 24000 samples at 48kHz
    float beatDuration = 60.f / bpm;
    int samplesPerBeat = static_cast<int>(beatDuration * sampleRate);
    int totalSamples = samplesPerBeat * 10;  // 10 beats worth

    int beatCount = 0;
    double prevPhase = clock.beatPhase;

    for (int s = 0; s < totalSamples; s++) {
        clock.process(args);

        // Detect beat crossing
        bool beatCrossed = (std::floor(clock.beatPhase) != std::floor(prevPhase));
        prevPhase = clock.beatPhase;

        if (beatCrossed) {
            beatCount++;
            // Check outputs on this sample and the next few (pulse width)
            BeatEvent evt;
            evt.beatNumber = beatCount;
            evt.beatPhase = clock.beatPhase;
            evt.resetHigh = clock.outputs[PreFlightClock::RESET_OUTPUT].getVoltage() > 5.f;
            evt.runHigh = clock.outputs[PreFlightClock::RUN_OUTPUT].getVoltage() > 5.f;
            evt.masterClockHigh = clock.outputs[PreFlightClock::MASTER_OUTPUT].getVoltage() > 5.f;
            evt.clockHigh = clock.outputs[PreFlightClock::CLOCK_OUTPUT].getVoltage() > 5.f;
            evt.state = clock.displayState.load();
            events.push_back(evt);

            if (verbose) {
                const char* stateStr = (evt.state == 0) ? "STOPPED" :
                                       (evt.state == 1) ? "COUNTING_IN" : "RUNNING";
                std::cout << "  Beat " << evt.beatNumber
                          << ": state=" << stateStr
                          << " reset=" << (evt.resetHigh ? "HIGH" : "low")
                          << " run=" << (evt.runHigh ? "HIGH" : "low")
                          << " master=" << (evt.masterClockHigh ? "HIGH" : "low")
                          << " clock=" << (evt.clockHigh ? "HIGH" : "low")
                          << std::endl;
            }
        }
    }

    return events;
}

// ============================================================================
// Tests
// ============================================================================

int testResetOnDownbeat(bool verbose) {
    std::cout << "=== Test: Reset fires on 0.0.0 (downbeat) ===" << std::endl;

    auto events = simulatePlaySequence(120.f, 48000.f, verbose);

    if (events.size() < 6) {
        std::cout << "FAIL: Not enough beats detected (" << events.size() << ")" << std::endl;
        return 1;
    }

    int failures = 0;

    // The first 4 beats should be count-in (COUNTING_IN state)
    for (int i = 0; i < 4; i++) {
        if (events[i].state != PreFlightClock::COUNTING_IN) {
            std::cout << "FAIL: Beat " << events[i].beatNumber
                      << " should be COUNTING_IN but was " << events[i].state << std::endl;
            failures++;
        }
    }

    // Beat 5 should be RUNNING (the downbeat, 0.0.0)
    if (events[4].state != PreFlightClock::RUNNING) {
        std::cout << "FAIL: Beat 5 should be RUNNING (downbeat) but was " << events[4].state << std::endl;
        failures++;
    }

    // CRITICAL: Reset should fire on the downbeat (beat 5), NOT during count-in
    // Check that reset does NOT fire on count-in beats 1-4
    for (int i = 0; i < 4; i++) {
        if (events[i].resetHigh) {
            std::cout << "FAIL: Reset fired during count-in (beat " << events[i].beatNumber
                      << ") — should only fire on downbeat" << std::endl;
            failures++;
        }
    }

    // Check that reset DOES fire on beat 5 (the downbeat)
    if (!events[4].resetHigh) {
        std::cout << "FAIL: Reset did NOT fire on the downbeat (beat 5)" << std::endl;
        failures++;
    }

    // RUN should go high on beat 5
    if (!events[4].runHigh) {
        std::cout << "FAIL: RUN did not go high on the downbeat" << std::endl;
        failures++;
    }

    // RUN should NOT be high during count-in
    for (int i = 0; i < 4; i++) {
        if (events[i].runHigh) {
            std::cout << "FAIL: RUN was high during count-in (beat " << events[i].beatNumber << ")" << std::endl;
            failures++;
        }
    }

    // MASTER_OUTPUT should fire on beat 5 (first clock pulse when running)
    if (!events[4].masterClockHigh) {
        std::cout << "FAIL: MASTER_OUTPUT did not fire on the downbeat" << std::endl;
        failures++;
    }

    // MASTER_OUTPUT should NOT fire during count-in
    for (int i = 0; i < 4; i++) {
        if (events[i].masterClockHigh) {
            std::cout << "FAIL: MASTER_OUTPUT fired during count-in (beat " << events[i].beatNumber << ")" << std::endl;
            failures++;
        }
    }

    if (failures == 0) {
        std::cout << "PASS" << std::endl;
    } else {
        std::cout << failures << " failure(s)" << std::endl;
    }
    return failures;
}

int testResetAndRunSimultaneous(bool verbose) {
    std::cout << "\n=== Test: Reset and Run go high on the same sample ===" << std::endl;

    PreFlightClock clock;
    Module::ProcessArgs args;
    args.sampleTime = 1.f / 48000.f;
    args.sampleRate = 48000.f;

    // Run stopped
    for (int i = 0; i < 100; i++) clock.process(args);

    // Press play
    clock.params[PreFlightClock::PLAY_PARAM].setValue(10.f);
    clock.process(args);
    clock.params[PreFlightClock::PLAY_PARAM].setValue(0.f);

    // Track first sample where reset goes high and first sample where run goes high
    int resetFirstSample = -1;
    int runFirstSample = -1;
    float beatDuration = 60.f / 120.f;
    int totalSamples = static_cast<int>(beatDuration * 48000.f * 10);

    for (int s = 0; s < totalSamples; s++) {
        clock.process(args);

        if (resetFirstSample < 0 && clock.outputs[PreFlightClock::RESET_OUTPUT].getVoltage() > 5.f)
            resetFirstSample = s;
        if (runFirstSample < 0 && clock.outputs[PreFlightClock::RUN_OUTPUT].getVoltage() > 5.f)
            runFirstSample = s;

        if (resetFirstSample >= 0 && runFirstSample >= 0)
            break;
    }

    if (verbose) {
        std::cout << "  Reset first high at sample: " << resetFirstSample << std::endl;
        std::cout << "  Run first high at sample:   " << runFirstSample << std::endl;
        if (resetFirstSample >= 0 && runFirstSample >= 0) {
            float timeDiffMs = std::abs(resetFirstSample - runFirstSample) / 48000.f * 1000.f;
            std::cout << "  Time difference: " << timeDiffMs << " ms" << std::endl;
        }
    }

    int failures = 0;
    if (resetFirstSample < 0) {
        std::cout << "FAIL: Reset never went high" << std::endl;
        failures++;
    }
    if (runFirstSample < 0) {
        std::cout << "FAIL: Run never went high" << std::endl;
        failures++;
    }
    if (resetFirstSample >= 0 && runFirstSample >= 0 && resetFirstSample != runFirstSample) {
        std::cout << "FAIL: Reset and Run did not go high on the same sample" << std::endl;
        std::cout << "  Reset at sample " << resetFirstSample << ", Run at sample " << runFirstSample << std::endl;
        float beatDurationSamples = beatDuration * 48000.f;
        float beatsDiff = std::abs(resetFirstSample - runFirstSample) / beatDurationSamples;
        std::cout << "  That's " << beatsDiff << " beats apart" << std::endl;
        failures++;
    }

    if (failures == 0) {
        std::cout << "PASS" << std::endl;
    } else {
        std::cout << failures << " failure(s)" << std::endl;
    }
    return failures;
}

int testNoClockOutputDuringCountIn(bool verbose) {
    std::cout << "\n=== Test: CLOCK_OUTPUT behavior during count-in ===" << std::endl;

    PreFlightClock clock;
    Module::ProcessArgs args;
    args.sampleTime = 1.f / 48000.f;
    args.sampleRate = 48000.f;

    for (int i = 0; i < 100; i++) clock.process(args);

    clock.params[PreFlightClock::PLAY_PARAM].setValue(10.f);
    clock.process(args);
    clock.params[PreFlightClock::PLAY_PARAM].setValue(0.f);

    // Count clock pulses during count-in vs running
    int clockPulsesDuringCountIn = 0;
    int masterPulsesDuringCountIn = 0;
    float beatDuration = 60.f / 120.f;
    int totalSamples = static_cast<int>(beatDuration * 48000.f * 8);
    bool wasClockHigh = false;
    bool wasMasterHigh = false;

    for (int s = 0; s < totalSamples; s++) {
        clock.process(args);

        bool clockHigh = clock.outputs[PreFlightClock::CLOCK_OUTPUT].getVoltage() > 5.f;
        bool masterHigh = clock.outputs[PreFlightClock::MASTER_OUTPUT].getVoltage() > 5.f;

        if (clock.state == PreFlightClock::COUNTING_IN) {
            if (clockHigh && !wasClockHigh) clockPulsesDuringCountIn++;
            if (masterHigh && !wasMasterHigh) masterPulsesDuringCountIn++;
        }

        wasClockHigh = clockHigh;
        wasMasterHigh = masterHigh;
    }

    if (verbose) {
        std::cout << "  CLOCK_OUTPUT pulses during count-in: " << clockPulsesDuringCountIn << std::endl;
        std::cout << "  MASTER_OUTPUT pulses during count-in: " << masterPulsesDuringCountIn << std::endl;
    }

    int failures = 0;

    // MASTER_OUTPUT should NOT pulse during count-in
    if (masterPulsesDuringCountIn > 0) {
        std::cout << "FAIL: MASTER_OUTPUT pulsed " << masterPulsesDuringCountIn << " times during count-in" << std::endl;
        failures++;
    }

    // Note: CLOCK_OUTPUT is documented as "always running" so it WILL pulse.
    // This test just documents the behavior.
    std::cout << "  INFO: CLOCK_OUTPUT is always-running and pulses " << clockPulsesDuringCountIn
              << " times during count-in (by design)" << std::endl;

    if (failures == 0) {
        std::cout << "PASS" << std::endl;
    } else {
        std::cout << failures << " failure(s)" << std::endl;
    }
    return failures;
}

int testMetronomeTockTicPattern(bool verbose) {
    std::cout << "\n=== Test: Metronome is tock-tic-tic-tic, silent on downbeat ===" << std::endl;

    PreFlightClock clock;
    Module::ProcessArgs args;
    args.sampleTime = 1.f / 48000.f;
    args.sampleRate = 48000.f;

    for (int i = 0; i < 100; i++) clock.process(args);

    // Press play
    clock.params[PreFlightClock::PLAY_PARAM].setValue(10.f);
    clock.process(args);
    clock.params[PreFlightClock::PLAY_PARAM].setValue(0.f);

    // Track metronome frequency at each beat
    struct MetroBeat {
        float freq;
        float envelope;
    };
    std::vector<MetroBeat> metroBeats;

    float beatDuration = 60.f / 120.f;
    int totalSamples = static_cast<int>(beatDuration * 48000.f * 8);
    double prevPhase = clock.beatPhase;

    for (int s = 0; s < totalSamples; s++) {
        clock.process(args);

        bool beatCrossed = (std::floor(clock.beatPhase) != std::floor(prevPhase));
        prevPhase = clock.beatPhase;

        if (beatCrossed) {
            // Capture metronome state right after the beat
            metroBeats.push_back({clock.metroFreq, clock.metroEnvelope});
        }
    }

    int failures = 0;

    if (metroBeats.size() < 6) {
        std::cout << "FAIL: Not enough beats" << std::endl;
        return 1;
    }

    // Beat 0 (-3): tock = 880Hz, full envelope
    if (verbose) std::cout << "  Beat -3: freq=" << metroBeats[0].freq << " env=" << metroBeats[0].envelope << std::endl;
    if (std::abs(metroBeats[0].freq - 880.f) > 1.f) {
        std::cout << "FAIL: Beat -3 should be 880Hz (tock), got " << metroBeats[0].freq << std::endl;
        failures++;
    }

    // Beats 1-3 (-2, -1, 0): tic = 440Hz, lower envelope
    for (int i = 1; i <= 3; i++) {
        if (verbose) std::cout << "  Beat " << (i - 3) << ": freq=" << metroBeats[i].freq << " env=" << metroBeats[i].envelope << std::endl;
        if (std::abs(metroBeats[i].freq - 440.f) > 1.f) {
            std::cout << "FAIL: Beat " << (i - 3) << " should be 440Hz (tic), got " << metroBeats[i].freq << std::endl;
            failures++;
        }
    }

    // Beat 4 (1.1.1 downbeat): no metronome — envelope should be near zero
    // The envelope decays exponentially, so by the next beat it should be very small
    if (verbose) std::cout << "  Downbeat: freq=" << metroBeats[4].freq << " env=" << metroBeats[4].envelope << std::endl;
    if (metroBeats[4].envelope > 0.01f) {
        std::cout << "FAIL: Downbeat should have no metronome (envelope=" << metroBeats[4].envelope << ")" << std::endl;
        failures++;
    }

    if (failures == 0)
        std::cout << "PASS" << std::endl;
    else
        std::cout << failures << " failure(s)" << std::endl;
    return failures;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
            verbose = true;
    }

    std::cout << "PreFlightClock Reset Timing Tests" << std::endl;
    std::cout << "=================================" << std::endl;

    int totalFailures = 0;
    totalFailures += testResetOnDownbeat(verbose);
    totalFailures += testResetAndRunSimultaneous(verbose);
    totalFailures += testNoClockOutputDuringCountIn(verbose);
    totalFailures += testMetronomeTockTicPattern(verbose);

    std::cout << "\n=================================" << std::endl;
    if (totalFailures == 0) {
        std::cout << "ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << totalFailures << " TOTAL FAILURE(S)" << std::endl;
    }

    return totalFailures > 0 ? 1 : 0;
}
