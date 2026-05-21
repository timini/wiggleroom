/******************************************************************************
 * THE WEAVER
 * Scale-Bus-aware polyphonic arpeggiator
 *
 * Weaves an incoming polyphonic chord (or an auto-generated diatonic triad
 * derived from the Scale Bus) into rhythmic patterns. Supports clock
 * division / multiplication, 11 arpeggio variations, manual addressing via
 * Index CV, and hard-synced or free-running operation.
 *
 * Scale Bus protocol (shared with TheArchitect / ACID9Seq):
 *   Channels 0-11: 10 V if that chromatic degree is in the scale, else 0 V.
 *   Channel 15  : Root note as V/Oct (0 V = C, 1/12 V = C#, ...).
 ******************************************************************************/

#include "rack.hpp"
#include "ImagePanel.hpp"
#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

// ---- Pattern definitions -----------------------------------------------------

enum Pattern {
    PATTERN_UP = 0,
    PATTERN_DOWN,
    PATTERN_UP_DOWN,
    PATTERN_ORDER_PLAYED,
    PATTERN_RANDOM,
    PATTERN_BROWNIAN,
    PATTERN_CONVERGE,
    PATTERN_DIVERGE,
    PATTERN_PINKY_THUMB,
    PATTERN_SCALE_RUN,
    PATTERN_OCTAVE_JUMP,
    PATTERN_COUNT
};

static const std::vector<std::string> PATTERN_NAMES = {
    "Up", "Down", "Up/Down", "Order Played", "Random", "Brownian",
    "Converge", "Diverge", "Pinky/Thumb", "Scale Run", "Octave Jump"
};

// ---- Clock division / multiplication table ----------------------------------

// Ratio = output-ticks-per-input-tick. x1 is index 5.
static const std::vector<float> CLOCK_RATIOS = {
    1.f/16, 1.f/8, 1.f/4, 1.f/3, 1.f/2,
    1.f,
    2.f, 3.f, 4.f, 8.f, 16.f
};

static const std::vector<std::string> CLOCK_RATIO_LABELS = {
    "/16", "/8", "/4", "/3", "/2", "x1", "x2", "x3", "x4", "x8", "x16"
};

constexpr int CLOCK_RATIO_UNITY = 5;  // index of x1
constexpr int MAX_POLY_IN = 16;
constexpr int MAX_OCTAVE_SPREAD = 4;
constexpr int MAX_POOL = MAX_POLY_IN * MAX_OCTAVE_SPREAD;
constexpr float CLOCK_LOST_TIMEOUT = 3.f;   // seconds
constexpr float DEFAULT_CLOCK_PERIOD = 0.5f; // 120 BPM
constexpr float EOC_PULSE_DURATION = 1e-3f;  // 1 ms
constexpr float GATE_HIGH_V = 10.f;

struct TheWeaver : Module {
    enum ParamId {
        PATTERN_PARAM,
        OCTAVE_SPREAD_PARAM,
        CLOCK_DIV_PARAM,
        GATE_LENGTH_PARAM,
        SYNC_MODE_PARAM,
        HOLD_PARAM,
        INDEX_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        NOTES_INPUT,
        SCALE_BUS_INPUT,
        PATTERN_CV_INPUT,
        DIV_CV_INPUT,
        INDEX_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CV_OUTPUT,
        GATE_OUTPUT,
        EOC_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        CLOCK_LIGHT,
        GATE_LIGHT,
        HOLD_LIGHT,
        LIGHTS_LEN
    };

    // ---- Triggers / pulses --------------------------------------------------
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::PulseGenerator eocPulse;

    // ---- Clock state --------------------------------------------------------
    float clockPeriod = DEFAULT_CLOCK_PERIOD;
    float timeSinceClock = 0.f;
    bool clockDetected = false;
    float phase = 0.f;        // output-tick phase accumulator [0, 1)
    int divCounter = 0;       // for ratios < 1

    // ---- Gate output state --------------------------------------------------
    float gateTimer = 0.f;          // counts seconds since current tick fired
    float currentTickPeriod = DEFAULT_CLOCK_PERIOD;  // measured at last fire
    bool gateHigh = false;

    // ---- Pattern state ------------------------------------------------------
    int arpIndex = 0;
    int upDownDir = +1;        // +1 ascending, -1 descending
    int convergeStep = 0;      // pairs consumed in converge/diverge
    bool convergeSide = false; // false = low half, true = high half
    int pinkyWalker = 1;       // walker index for Pinky/Thumb
    bool pinkyLow = true;      // alternates low/walker
    int orderIndex = 0;        // step within order-played pool
    int stepsSinceEoc = 0;     // for random/brownian EOC timing
    bool firstStepAfterReset = true;  // emit step 0 (or pattern-specific
                                      // starting index) on the first tick

    // ---- Note pool ----------------------------------------------------------
    std::vector<float> sortedPool;       // ascending V/Oct voltages
    std::vector<float> orderPool;        // raw poly-channel order
    std::vector<float> scaleRunPool;     // scale-run variant

    // ---- Scale bus state ----------------------------------------------------
    int scaleMask = 0xFFF;   // chromatic fallback (all 12 active)
    int scaleRoot = 0;       // 0 = C
    bool scaleBusActive = false;

    // ---- Held notes (for Hold/Latch) ----------------------------------------
    std::vector<float> heldNotes;         // raw V/Oct, in poly-channel order
    bool hadActiveInput = false;

    // ---- Last output values -------------------------------------------------
    float lastCv = 0.f;
    int lastIndexCvInt = -1;

    // ---- RNG ----------------------------------------------------------------
    std::mt19937 rng{std::random_device{}()};

    TheWeaver() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        auto* p = configSwitch(PATTERN_PARAM, 0.f, (float)(PATTERN_COUNT - 1), 0.f,
                               "Pattern", PATTERN_NAMES);
        p->snapEnabled = true;

        auto* o = configParam(OCTAVE_SPREAD_PARAM, 1.f, (float)MAX_OCTAVE_SPREAD, 1.f,
                              "Octave Spread", " oct");
        o->snapEnabled = true;

        auto* d = configSwitch(CLOCK_DIV_PARAM, 0.f, (float)(CLOCK_RATIOS.size() - 1),
                               (float)CLOCK_RATIO_UNITY, "Clock Div/Mult",
                               CLOCK_RATIO_LABELS);
        d->snapEnabled = true;

        configParam(GATE_LENGTH_PARAM, 0.01f, 1.f, 0.5f, "Gate Length", "%", 0.f, 100.f);

        configSwitch(SYNC_MODE_PARAM, 0.f, 1.f, 1.f, "Sync Mode",
                     {"Free Running", "Hard Sync"});
        configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold / Latch", {"Off", "On"});
        configParam(INDEX_PARAM, 0.f, 1.f, 0.f, "Manual Index", "%", 0.f, 100.f);

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(NOTES_INPUT, "Notes (V/Oct, poly)");
        configInput(SCALE_BUS_INPUT, "Scale Bus (16ch poly)");
        configInput(PATTERN_CV_INPUT, "Pattern CV");
        configInput(DIV_CV_INPUT, "Clock Div/Mult CV");
        configInput(INDEX_CV_INPUT, "Index CV");

        configOutput(CV_OUTPUT, "Pitch (V/Oct)");
        configOutput(GATE_OUTPUT, "Gate");
        configOutput(EOC_OUTPUT, "End of Cycle");

        sortedPool.reserve(MAX_POOL);
        orderPool.reserve(MAX_POLY_IN);
        scaleRunPool.reserve(MAX_POLY_IN * 12);
        heldNotes.reserve(MAX_POLY_IN);
    }

    // ------------------------------------------------------------------------
    // Scale bus helpers
    // ------------------------------------------------------------------------

    void readScaleBus() {
        scaleBusActive = false;
        scaleMask = 0xFFF;
        scaleRoot = 0;

        if (!inputs[SCALE_BUS_INPUT].isConnected()) return;
        int ch = inputs[SCALE_BUS_INPUT].getChannels();
        if (ch < 12) return;

        int mask = 0;
        for (int i = 0; i < 12; i++) {
            if (inputs[SCALE_BUS_INPUT].getVoltage(i) > 0.5f) {
                mask |= (1 << i);
            }
        }
        if (mask == 0) return;  // empty mask — treat as unpatched

        scaleMask = mask;
        scaleBusActive = true;

        if (ch >= 16) {
            float rootV = inputs[SCALE_BUS_INPUT].getVoltage(15);
            int r = static_cast<int>(std::round(rootV * 12.f)) % 12;
            if (r < 0) r += 12;
            scaleRoot = r;
        }
    }

    // Quantize a V/Oct voltage to the nearest chroma active in `scaleMask`.
    // The mask uses absolute chromatic positions (bit 0 = C, bit 1 = C#, ...).
    float quantizeToScale(float voltage) const {
        if (scaleMask == 0xFFF) return voltage;

        float midi = voltage * 12.f + 60.f;
        int octave = static_cast<int>(std::floor(midi / 12.f));
        int chroma = static_cast<int>(std::round(midi)) - octave * 12;
        if (chroma < 0) { chroma += 12; octave -= 1; }
        if (chroma > 11) { chroma -= 12; octave += 1; }

        if ((scaleMask >> chroma) & 1) {
            return ((float)(octave * 12 + chroma) - 60.f) / 12.f;
        }
        for (int offset = 1; offset <= 6; offset++) {
            int lo = (chroma - offset + 12) % 12;
            int hi = (chroma + offset) % 12;
            if ((scaleMask >> lo) & 1) {
                int n = octave * 12 + chroma - offset;
                return ((float)n - 60.f) / 12.f;
            }
            if ((scaleMask >> hi) & 1) {
                int n = octave * 12 + chroma + offset;
                return ((float)n - 60.f) / 12.f;
            }
        }
        return voltage;
    }

    // ------------------------------------------------------------------------
    // Note pool construction
    // ------------------------------------------------------------------------

    // Generate an auto-triad from the Scale Bus when no notes are played.
    // Outputs root + 3rd scale degree + 5th scale degree (e.g. C-E-G in C major),
    // not the first two scale notes above the root (which would yield C-D-E).
    void generateAutoTriad(std::vector<float>& out) const {
        int rootMidi = 60 + scaleRoot;  // C4 + root offset
        out.push_back(((float)rootMidi - 60.f) / 12.f);

        int degree = 0;     // scale degrees walked past the root
        int cur = rootMidi;
        while (degree < 5 && cur - rootMidi < 24) {
            cur++;
            int chroma = ((cur % 12) + 12) % 12;
            if ((scaleMask >> chroma) & 1) {
                degree++;
                // 3rd scale degree = 2 steps past root; 5th = 4 steps past root.
                if (degree == 2 || degree == 4) {
                    out.push_back(((float)cur - 60.f) / 12.f);
                }
            }
        }
    }

    // Build orderPool (raw poly-channel order) and sortedPool (ascending,
    // octave-spread, scale-quantized).
    void buildPools(int octaveSpread, bool hold) {
        orderPool.clear();
        sortedPool.clear();

        // Detect "active" input: connected, has channels, AND at least one
        // channel carries a non-zero voltage. Many polyphonic sources keep a
        // fixed channel count and signal note-off by driving the V/Oct back
        // to 0 V (MIDI-CV-style note-off, sequencers between steps). Without
        // this filter Hold/Latch would continually overwrite heldNotes with
        // the idle 0 V state and never actually latch the previous chord.
        bool inputConnected = inputs[NOTES_INPUT].isConnected() &&
                              inputs[NOTES_INPUT].getChannels() > 0;
        std::vector<float> currentInputNotes;
        if (inputConnected) {
            int ch = std::min(inputs[NOTES_INPUT].getChannels(), MAX_POLY_IN);
            for (int i = 0; i < ch; i++) {
                float v = inputs[NOTES_INPUT].getVoltage(i);
                if (v != 0.f) currentInputNotes.push_back(v);
            }
        }
        bool inputHasNotes = !currentInputNotes.empty();

        if (inputHasNotes) {
            heldNotes = currentInputNotes;
            hadActiveInput = true;
        }

        if (!inputHasNotes && hold && hadActiveInput && !heldNotes.empty()) {
            // keep heldNotes as-is
        } else if (!inputHasNotes) {
            heldNotes.clear();
            hadActiveInput = false;
            if (scaleBusActive) {
                generateAutoTriad(heldNotes);
            }
        }

        if (heldNotes.empty()) return;

        // Order-played pool (raw, quantized)
        for (float v : heldNotes) {
            orderPool.push_back(quantizeToScale(v));
        }

        // Sorted pool with octave spread
        std::vector<float> base = orderPool;
        std::sort(base.begin(), base.end());
        base.erase(std::unique(base.begin(), base.end(),
                               [](float a, float b) { return std::fabs(a - b) < 1e-4f; }),
                   base.end());

        for (int o = 0; o < octaveSpread; o++) {
            for (float v : base) {
                sortedPool.push_back(v + (float)o);
            }
        }
        std::sort(sortedPool.begin(), sortedPool.end());
    }

    // Build scale-run pool: every semitone in scaleMask between min and max
    // of sortedPool.
    void buildScaleRunPool() {
        scaleRunPool.clear();
        if (sortedPool.size() < 2) {
            scaleRunPool = sortedPool;
            return;
        }
        float lo = sortedPool.front();
        float hi = sortedPool.back();

        int loMidi = static_cast<int>(std::round(lo * 12.f + 60.f));
        int hiMidi = static_cast<int>(std::round(hi * 12.f + 60.f));
        for (int m = loMidi; m <= hiMidi; m++) {
            int chroma = ((m % 12) + 12) % 12;
            if ((scaleMask >> chroma) & 1) {
                scaleRunPool.push_back(((float)m - 60.f) / 12.f);
            }
        }
        if (scaleRunPool.empty()) scaleRunPool = sortedPool;
    }

    // ------------------------------------------------------------------------
    // Pattern advance
    // ------------------------------------------------------------------------

    // Returns the pool to index into for the given pattern.
    const std::vector<float>& activePool(int pattern) {
        if (pattern == PATTERN_ORDER_PLAYED && !orderPool.empty()) return orderPool;
        if (pattern == PATTERN_SCALE_RUN) {
            buildScaleRunPool();
            return scaleRunPool;
        }
        return sortedPool;
    }

    // Advance state for the current pattern and return (newIndex, didWrap).
    // `size` is the active pool size (>0 guaranteed by caller).
    struct StepResult {
        int index;
        bool wrapped;
        float octaveOffset;  // for OCTAVE_JUMP
    };

    StepResult advancePattern(int pattern, int size) {
        StepResult r{0, false, 0.f};
        if (size <= 0) return r;

        std::uniform_int_distribution<int> rand3(-1, 1);
        std::uniform_int_distribution<int> randPool(0, size - 1);

        // First tick after reset/start: emit the pattern's starting index
        // without advancing or wrapping. This guarantees step 0 is heard on
        // ascending patterns and prevents DOWN/CONVERGE etc. from spuriously
        // firing EOC on the first emitted note.
        if (firstStepAfterReset) {
            firstStepAfterReset = false;
            switch (pattern) {
                case PATTERN_DOWN:
                    arpIndex = size - 1;
                    break;
                case PATTERN_DIVERGE: {
                    int k = size / 2;
                    arpIndex = (size % 2 == 1) ? k : (k - 1);
                    convergeStep = 1;
                    break;
                }
                case PATTERN_CONVERGE:
                    arpIndex = 0;
                    convergeStep = 1;
                    break;
                case PATTERN_RANDOM:
                    arpIndex = randPool(rng);
                    break;
                case PATTERN_BROWNIAN:
                    arpIndex = 0;
                    break;
                case PATTERN_PINKY_THUMB:
                    arpIndex = 0;
                    pinkyLow = false;
                    pinkyWalker = 1;
                    break;
                case PATTERN_OCTAVE_JUMP:
                    arpIndex = 0;
                    r.octaveOffset = 0.f;
                    break;
                default:
                    // UP, UP_DOWN, ORDER_PLAYED, SCALE_RUN: start at index 0.
                    arpIndex = 0;
                    orderIndex = 0;
                    upDownDir = +1;
                    break;
            }
            r.index = arpIndex;
            return r;
        }

        switch (pattern) {
            case PATTERN_UP: {
                int next = arpIndex + 1;
                if (next >= size) { next = 0; r.wrapped = true; }
                r.index = next;
                break;
            }
            case PATTERN_DOWN: {
                int next = arpIndex - 1;
                if (next < 0) { next = size - 1; r.wrapped = true; }
                r.index = next;
                break;
            }
            case PATTERN_UP_DOWN: {
                if (size == 1) { r.index = 0; r.wrapped = true; break; }
                int next = arpIndex + upDownDir;
                if (next >= size) { upDownDir = -1; next = size - 2; }
                else if (next < 0) { upDownDir = +1; next = 1; r.wrapped = true; }
                r.index = next;
                break;
            }
            case PATTERN_ORDER_PLAYED: {
                int next = orderIndex + 1;
                if (next >= size) { next = 0; r.wrapped = true; }
                orderIndex = next;
                r.index = next;
                break;
            }
            case PATTERN_RANDOM: {
                r.index = randPool(rng);
                stepsSinceEoc++;
                if (stepsSinceEoc >= size) { r.wrapped = true; stepsSinceEoc = 0; }
                break;
            }
            case PATTERN_BROWNIAN: {
                int step = rand3(rng);
                int next = arpIndex + step;
                if (next < 0) next = 0;
                if (next >= size) next = size - 1;
                r.index = next;
                stepsSinceEoc++;
                if (stepsSinceEoc >= size) { r.wrapped = true; stepsSinceEoc = 0; }
                break;
            }
            case PATTERN_CONVERGE: {
                // Single pass outside-in over every index: 0, N-1, 1, N-2, ...
                // Cycle length is exactly N. For even N: ends on the lower of
                // the two center indices; for odd N: ends on the single center.
                int t = convergeStep;
                int half = t / 2;
                r.index = (t % 2 == 0) ? half : (size - 1 - half);
                convergeStep++;
                if (convergeStep >= size) {
                    convergeStep = 0;
                    r.wrapped = true;
                }
                convergeSide = false;
                break;
            }
            case PATTERN_DIVERGE: {
                // Single pass inside-out over every index. Cycle length = N.
                // Odd N=2k+1:  k, k-1, k+1, k-2, k+2, ..., 0, N-1
                // Even N=2k:   k-1, k, k-2, k+1, ..., 0, N-1
                int t = convergeStep;
                int half = t / 2;
                int outIdx;
                if (size % 2 == 1) {
                    int k = size / 2;
                    if (t == 0) outIdx = k;
                    else if (t % 2 == 1) outIdx = k - ((t + 1) / 2);
                    else outIdx = k + (t / 2);
                } else {
                    int k = size / 2;
                    outIdx = (t % 2 == 0) ? (k - 1 - half) : (k + half);
                }
                r.index = clamp(outIdx, 0, size - 1);
                convergeStep++;
                if (convergeStep >= size) {
                    convergeStep = 0;
                    r.wrapped = true;
                }
                convergeSide = false;
                break;
            }
            case PATTERN_PINKY_THUMB: {
                if (pinkyLow) {
                    r.index = 0;
                    pinkyLow = false;
                } else {
                    if (pinkyWalker >= size || pinkyWalker < 1) pinkyWalker = 1;
                    r.index = pinkyWalker;
                    pinkyWalker++;
                    if (pinkyWalker >= size) {
                        pinkyWalker = 1;
                        r.wrapped = true;
                    }
                    pinkyLow = true;
                }
                break;
            }
            case PATTERN_SCALE_RUN: {
                int next = arpIndex + 1;
                if (next >= size) { next = 0; r.wrapped = true; }
                r.index = next;
                break;
            }
            case PATTERN_OCTAVE_JUMP: {
                int next = arpIndex + 1;
                if (next >= size) { next = 0; r.wrapped = true; }
                r.index = next;
                std::uniform_int_distribution<int> jump(-1, 1);
                r.octaveOffset = static_cast<float>(jump(rng));
                break;
            }
            default:
                r.index = 0;
                break;
        }

        r.index = clamp(r.index, 0, size - 1);
        return r;
    }

    void resetPatternState() {
        arpIndex = 0;
        upDownDir = +1;
        convergeStep = 0;
        convergeSide = false;
        pinkyWalker = 1;
        pinkyLow = true;
        orderIndex = 0;
        stepsSinceEoc = 0;
        firstStepAfterReset = true;
    }

    // ------------------------------------------------------------------------
    // Main process
    // ------------------------------------------------------------------------

    void process(const ProcessArgs& args) override {
        float dt = args.sampleTime;

        // -- Parameters + CV modulation
        int pattern = static_cast<int>(params[PATTERN_PARAM].getValue());
        if (inputs[PATTERN_CV_INPUT].isConnected()) {
            pattern += static_cast<int>(std::round(inputs[PATTERN_CV_INPUT].getVoltage()));
        }
        pattern = clamp(pattern, 0, PATTERN_COUNT - 1);

        int octaveSpread = static_cast<int>(params[OCTAVE_SPREAD_PARAM].getValue());
        octaveSpread = clamp(octaveSpread, 1, MAX_OCTAVE_SPREAD);

        int divIdx = static_cast<int>(params[CLOCK_DIV_PARAM].getValue());
        if (inputs[DIV_CV_INPUT].isConnected()) {
            divIdx += static_cast<int>(std::round(inputs[DIV_CV_INPUT].getVoltage()));
        }
        divIdx = clamp(divIdx, 0, (int)CLOCK_RATIOS.size() - 1);
        float ratio = CLOCK_RATIOS[divIdx];

        float gateLength = clamp(params[GATE_LENGTH_PARAM].getValue(), 0.01f, 1.f);
        bool sync = params[SYNC_MODE_PARAM].getValue() > 0.5f;
        bool hold = params[HOLD_PARAM].getValue() > 0.5f;

        // -- Read scale bus, build pools
        readScaleBus();
        buildPools(octaveSpread, hold);

        const std::vector<float>& pool = activePool(pattern);
        int poolSize = (int)pool.size();

        // -- Reset handling
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
            resetPatternState();
            phase = 0.f;
            divCounter = 0;
        }

        // -- Clock edge detection + period measurement
        timeSinceClock += dt;
        bool clockEdge = false;
        if (inputs[CLOCK_INPUT].isConnected()) {
            clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
        }
        if (clockEdge) {
            if (timeSinceClock > 0.001f) {
                clockPeriod = timeSinceClock;
                clockDetected = true;
            }
            timeSinceClock = 0.f;
        }
        if (timeSinceClock > CLOCK_LOST_TIMEOUT) clockDetected = false;

        // -- Fire-tick decision
        bool fireTick = false;

        // Index-CV mode bypasses clock/pattern entirely.
        bool indexCvMode = inputs[INDEX_CV_INPUT].isConnected() && poolSize > 0;
        int indexCvInt = -1;

        if (indexCvMode) {
            float iv = clamp(inputs[INDEX_CV_INPUT].getVoltage() * 0.1f +
                             params[INDEX_PARAM].getValue(), 0.f, 1.f);
            indexCvInt = clamp(static_cast<int>(iv * poolSize), 0, poolSize - 1);
            // Refire when the addressed index changes OR when the pool value
            // at the current index changes (e.g. new chord at the same CV
            // position). Without the latter check, holding the index steady
            // while the chord changes leaves lastCv pointing at the old note.
            float poolValAtIndex = pool[indexCvInt];
            bool poolValChanged = std::fabs(poolValAtIndex - lastCv) > 1e-5f;
            if (indexCvInt != lastIndexCvInt || poolValChanged) {
                fireTick = true;
                arpIndex = indexCvInt;
                lastIndexCvInt = indexCvInt;
            }
            // Keep phase/divCounter clean for when user unpatches
            phase = 0.f;
            divCounter = 0;
        } else {
            lastIndexCvInt = -1;

            float outputPeriod = (ratio > 0.f) ? (clockPeriod / ratio) : DEFAULT_CLOCK_PERIOD;
            if (outputPeriod < 1e-4f) outputPeriod = 1e-4f;

            if (sync && inputs[CLOCK_INPUT].isConnected()) {
                // Hard sync path
                if (clockEdge) {
                    if (ratio >= 1.f) {
                        // Multiplier: fire on every input edge, plus sub-ticks between.
                        fireTick = true;
                        phase = 0.f;
                    } else {
                        // Divider: count input edges.
                        int N = std::max(1, static_cast<int>(std::round(1.f / ratio)));
                        divCounter++;
                        if (divCounter >= N) {
                            divCounter = 0;
                            fireTick = true;
                            phase = 0.f;
                        }
                    }
                }
                // Sub-tick interpolation for multipliers (ratio > 1).
                if (!fireTick && ratio > 1.f) {
                    phase += dt / outputPeriod;
                    if (phase >= 1.f) {
                        phase -= 1.f;
                        fireTick = true;
                    }
                }
            } else {
                // Free-running: phase accumulator.
                phase += dt / outputPeriod;
                if (phase >= 1.f) {
                    phase -= 1.f;
                    fireTick = true;
                }
            }
        }

        // -- Advance pattern on tick
        bool cycleWrapped = false;
        float octaveOffset = 0.f;
        if (fireTick && poolSize > 0 && !indexCvMode) {
            StepResult step = advancePattern(pattern, poolSize);
            arpIndex = step.index;
            cycleWrapped = step.wrapped;
            octaveOffset = step.octaveOffset;
        }

        // -- Gate timing
        if (fireTick && poolSize > 0) {
            gateTimer = 0.f;
            gateHigh = true;
            currentTickPeriod = indexCvMode ? DEFAULT_CLOCK_PERIOD
                                            : (ratio > 0.f ? clockPeriod / ratio : DEFAULT_CLOCK_PERIOD);
            if (currentTickPeriod < 1e-4f) currentTickPeriod = 1e-4f;

            float cv = clamp(pool[arpIndex] + octaveOffset, -10.f, 10.f);
            lastCv = cv;

            if (cycleWrapped) eocPulse.trigger(EOC_PULSE_DURATION);
        }
        if (gateHigh) {
            gateTimer += dt;
            float gateHighLen = std::min(gateLength * currentTickPeriod,
                                         currentTickPeriod * 0.99f);
            if (gateTimer >= gateHighLen) gateHigh = false;
        }

        // -- Outputs
        if (poolSize > 0) {
            outputs[CV_OUTPUT].setVoltage(lastCv);
            outputs[GATE_OUTPUT].setVoltage(gateHigh ? GATE_HIGH_V : 0.f);
        } else {
            outputs[CV_OUTPUT].setVoltage(0.f);
            outputs[GATE_OUTPUT].setVoltage(0.f);
            gateHigh = false;
        }

        bool eoc = eocPulse.process(dt);
        outputs[EOC_OUTPUT].setVoltage(eoc ? GATE_HIGH_V : 0.f);

        // -- Lights
        lights[CLOCK_LIGHT].setBrightnessSmooth(clockDetected ? 1.f : 0.f, dt);
        lights[GATE_LIGHT].setBrightnessSmooth(gateHigh ? 1.f : 0.f, dt);
        lights[HOLD_LIGHT].setBrightness(hold ? 1.f : 0.f);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "arpIndex", json_integer(arpIndex));
        // Persist held notes so Hold/Latch survives a save.
        json_t* heldJ = json_array();
        for (float v : heldNotes) json_array_append_new(heldJ, json_real(v));
        json_object_set_new(rootJ, "heldNotes", heldJ);
        json_object_set_new(rootJ, "hadActiveInput", json_boolean(hadActiveInput));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* idxJ = json_object_get(rootJ, "arpIndex");
        if (idxJ) arpIndex = static_cast<int>(json_integer_value(idxJ));

        heldNotes.clear();
        json_t* heldJ = json_object_get(rootJ, "heldNotes");
        if (heldJ && json_is_array(heldJ)) {
            size_t n = json_array_size(heldJ);
            for (size_t i = 0; i < n; i++) {
                json_t* v = json_array_get(heldJ, i);
                if (json_is_number(v)) heldNotes.push_back((float)json_number_value(v));
            }
        }
        json_t* hadJ = json_object_get(rootJ, "hadActiveInput");
        if (hadJ) hadActiveInput = json_boolean_value(hadJ);
    }
};

// ---- Widget -----------------------------------------------------------------

struct TheWeaverWidget : ModuleWidget {
    TheWeaverWidget(TheWeaver* module) {
        setModule(module);
        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        addChild(new WiggleRoom::ImagePanel(
            asset::plugin(pluginInstance, "res/TheWeaver.png"), box.size));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 12HP = 60.96mm wide. Layout columns (mm):
        const float col1 = 10.f;
        const float col2 = 30.48f;   // center
        const float col3 = 51.f;

        // Row 1 (y=22): Big Pattern knob in center + flanking knobs
        addParam(createParamCentered<RoundBigBlackKnob>(
            mm2px(Vec(col2, 22.f)), module, TheWeaver::PATTERN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, 22.f)), module, TheWeaver::OCTAVE_SPREAD_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, 22.f)), module, TheWeaver::CLOCK_DIV_PARAM));

        // Row 2 (y=40): Gate length, Index knob
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col1, 40.f)), module, TheWeaver::GATE_LENGTH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(col3, 40.f)), module, TheWeaver::INDEX_PARAM));

        // Row 3 (y=52): Sync + Hold switches + Clock lock light (center)
        addParam(createParamCentered<CKSS>(
            mm2px(Vec(col1, 52.f)), module, TheWeaver::SYNC_MODE_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(
            mm2px(Vec(col2, 52.f)), module, TheWeaver::CLOCK_LIGHT));
        addParam(createParamCentered<CKSS>(
            mm2px(Vec(col3, 52.f)), module, TheWeaver::HOLD_PARAM));

        // Row 4 (y=68): Pattern CV, Div CV, Index CV
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col1, 68.f)), module, TheWeaver::PATTERN_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col2, 68.f)), module, TheWeaver::DIV_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col3, 68.f)), module, TheWeaver::INDEX_CV_INPUT));

        // Row 5 (y=84): Clock, Reset, Notes In
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col1, 84.f)), module, TheWeaver::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col2, 84.f)), module, TheWeaver::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col3, 84.f)), module, TheWeaver::NOTES_INPUT));

        // Row 6 (y=100): Scale Bus in
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(col1, 100.f)), module, TheWeaver::SCALE_BUS_INPUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(
            mm2px(Vec(col2, 100.f)), module, TheWeaver::HOLD_LIGHT));

        // Row 7 (y=114): Outputs - CV, Gate, EOC
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(col1, 114.f)), module, TheWeaver::CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(col2, 114.f)), module, TheWeaver::GATE_OUTPUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(col2 + 6.f, 108.f)), module, TheWeaver::GATE_LIGHT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(col3, 114.f)), module, TheWeaver::EOC_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelTheWeaver = createModel<WiggleRoom::TheWeaver, WiggleRoom::TheWeaverWidget>("TheWeaver");
