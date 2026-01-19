/******************************************************************************
 * GRAVITY CLOCK
 * Clock-synced bouncing ball physics modulator
 *
 * Takes a clock input and generates a bouncing ball LFO that stays in sync
 * with musical divisions. The ball is "dropped" on each clock pulse and
 * bounces with physically accurate timing.
 *
 * Features:
 *   - Ratio control for clock divisions (1x, 2x, 4x, etc.)
 *   - Elasticity for steady sync (1.0) or ratcheting decay (<1.0)
 *   - LFO output (ball position) and trigger output (on impact)
 ******************************************************************************/

#include "rack.hpp"
#include "ImagePanel.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct GravityClock : Module {
    enum ParamId {
        RATIO_PARAM,      // Clock multiplier/divider
        ELASTICITY_PARAM, // Bounce decay (1.0 = infinite, <1.0 = decaying)
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,      // Master clock input
        RATIO_CV_INPUT,   // CV modulation for ratio
        INPUTS_LEN
    };
    enum OutputId {
        LFO_OUTPUT,       // Ball position (0-5V)
        TRIGGER_OUTPUT,   // Impact trigger
        OUTPUTS_LEN
    };
    enum LightId {
        LOCK_LIGHT,       // Indicates clock is detected
        LIGHTS_LEN
    };

    // Clock detection
    dsp::SchmittTrigger clockTrigger;
    float timeSinceClock = 0.f;
    float detectedPeriod = 0.5f;  // Default ~120 BPM
    bool clockLocked = false;

    // Physics state
    float position = 0.f;   // Ball height (0 = floor)
    float velocity = 0.f;   // Ball velocity
    float targetHeight = 1.f;  // Calculated height for sync

    // Trigger output pulse
    dsp::PulseGenerator impactPulse;

    // Fixed gravity constant (tuned for good feel)
    static constexpr float GRAVITY = 5000.f;

    GravityClock() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Ratio: 0.25x to 8x (quarter speed to 8x speed)
        configParam(RATIO_PARAM, 0.25f, 8.f, 1.f, "Clock Ratio", "x");

        // Elasticity: 0.5 to 1.0 (heavy decay to perfect bounce)
        configParam(ELASTICITY_PARAM, 0.5f, 1.f, 1.f, "Elasticity");

        configInput(CLOCK_INPUT, "Clock");
        configInput(RATIO_CV_INPUT, "Ratio CV");

        configOutput(LFO_OUTPUT, "LFO (Ball Position)");
        configOutput(TRIGGER_OUTPUT, "Impact Trigger");

        configLight(LOCK_LIGHT, "Clock Lock");
    }

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;
        bool resetBall = false;

        // ========================================
        // 1. CLOCK DETECTION
        // ========================================
        timeSinceClock += dt;

        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
            // Clock pulse detected
            if (timeSinceClock > 0.001f) {
                detectedPeriod = timeSinceClock;
                clockLocked = true;
            }
            timeSinceClock = 0.f;
            resetBall = true;
        }

        // Timeout: if no clock for 2 seconds, unlock
        if (timeSinceClock > 2.f) {
            clockLocked = false;
        }

        lights[LOCK_LIGHT].setBrightness(clockLocked ? 1.f : 0.f);

        // ========================================
        // 2. CALCULATE TARGET PERIOD & HEIGHT
        // ========================================
        // Ratio: higher = faster bouncing
        float ratio = params[RATIO_PARAM].getValue();
        if (inputs[RATIO_CV_INPUT].isConnected()) {
            ratio += inputs[RATIO_CV_INPUT].getVoltage() * 0.5f;
        }
        ratio = clamp(ratio, 0.1f, 16.f);

        // Target period = detected period / ratio
        float targetPeriod = detectedPeriod / ratio;

        // Calculate height needed for ball to bounce with this period
        // Physics: T = 2 * sqrt(2*h/g)  =>  h = g*T^2 / 8
        targetHeight = (GRAVITY * targetPeriod * targetPeriod) / 8.f;
        targetHeight = clamp(targetHeight, 0.001f, 1000.f);

        // ========================================
        // 3. PHYSICS SIMULATION
        // ========================================
        float elasticity = params[ELASTICITY_PARAM].getValue();

        if (resetBall) {
            // Drop the ball from calculated height
            position = targetHeight;
            velocity = 0.f;
        } else {
            // Apply gravity
            velocity -= GRAVITY * dt;

            // Update position
            position += velocity * dt;

            // Floor collision
            if (position <= 0.f) {
                position = 0.f;

                // Only bounce if moving downward with significant velocity
                if (velocity < -1.f) {
                    velocity = -velocity * elasticity;
                    impactPulse.trigger(1e-3f);
                } else {
                    // Ball has settled
                    velocity = 0.f;
                }
            }
        }

        // ========================================
        // 4. OUTPUTS
        // ========================================
        // LFO output: normalized position (0-5V)
        float normalizedPos = (targetHeight > 0.001f) ? (position / targetHeight) : 0.f;
        normalizedPos = clamp(normalizedPos, 0.f, 1.f);
        outputs[LFO_OUTPUT].setVoltage(normalizedPos * 5.f);

        // Trigger output
        outputs[TRIGGER_OUTPUT].setVoltage(impactPulse.process(dt) ? 10.f : 0.f);
    }
};

struct GravityClockWidget : ModuleWidget {
    GravityClockWidget(GravityClock* module) {
        setModule(module);
        box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/GravityClock.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float xCenter = box.size.x / 2.f;

        // Main Ratio knob (large)
        addParam(createParamCentered<RoundBigBlackKnob>(
            Vec(xCenter, 60), module, GravityClock::RATIO_PARAM));

        // Elasticity knob
        addParam(createParamCentered<RoundBlackKnob>(
            Vec(xCenter, 120), module, GravityClock::ELASTICITY_PARAM));

        // Clock lock indicator light
        addChild(createLightCentered<SmallLight<GreenLight>>(
            Vec(xCenter, 155), module, GravityClock::LOCK_LIGHT));

        // Inputs
        float xLeft = box.size.x * 0.25f;
        float xRight = box.size.x * 0.75f;

        addInput(createInputCentered<PJ301MPort>(
            Vec(xLeft, 200), module, GravityClock::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            Vec(xRight, 200), module, GravityClock::RATIO_CV_INPUT));

        // Outputs
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xLeft, 280), module, GravityClock::LFO_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            Vec(xRight, 280), module, GravityClock::TRIGGER_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelGravityClock = createModel<WiggleRoom::GravityClock, WiggleRoom::GravityClockWidget>("GravityClock");
