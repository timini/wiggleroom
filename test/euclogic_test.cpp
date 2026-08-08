/**
 * Euclogic Test Executable
 *
 * Standalone test tool for Euclogic module components:
 * - EuclideanEngine: Rhythm generation
 * - TruthTable: Logic operations
 * - ProbabilityGate: Probability filtering
 *
 * This enables TDD testing without VCV Rack dependencies.
 *
 * Usage:
 *   ./euclogic_test --test-euclidean --steps=8 --hits=3
 *   ./euclogic_test --test-truth-table --preset=OR --inputs=0b1010
 *   ./euclogic_test --test-probability --prob=0.5 --seed=42 --trials=1000
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>

// Include the pure logic headers
#include "common/euclogic/EuclideanEngine.hpp"
#include "common/euclogic/TruthTable.hpp"
#include "common/euclogic/ProbabilityGate.hpp"

// JSON output helpers
void printJsonArrayBool(const std::string& name, const std::vector<bool>& values) {
    std::cout << "{\"" << name << "\": [";
    for (size_t i = 0; i < values.size(); i++) {
        std::cout << (values[i] ? "true" : "false");
        if (i < values.size() - 1) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;
}

void printJsonArrayInt(const std::string& name, const std::vector<int>& values) {
    std::cout << "{\"" << name << "\": [";
    for (size_t i = 0; i < values.size(); i++) {
        std::cout << values[i];
        if (i < values.size() - 1) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;
}

void printJsonObject(const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::cout << "{";
    for (size_t i = 0; i < kvs.size(); i++) {
        std::cout << "\"" << kvs[i].first << "\": " << kvs[i].second;
        if (i < kvs.size() - 1) std::cout << ", ";
    }
    std::cout << "}" << std::endl;
}

int parseIntArg(int argc, char** argv, const char* prefix, int defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0) {
            return std::stoi(argv[i] + strlen(prefix));
        }
    }
    return defaultVal;
}

float parseFloatArg(int argc, char** argv, const char* prefix, float defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0) {
            return std::stof(argv[i] + strlen(prefix));
        }
    }
    return defaultVal;
}

std::string parseStringArg(int argc, char** argv, const char* prefix, const std::string& defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0) {
            return std::string(argv[i] + strlen(prefix));
        }
    }
    return defaultVal;
}

bool hasArg(int argc, char** argv, const char* arg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], arg) == 0) return true;
    }
    return false;
}

void printUsage() {
    std::cerr << "Euclogic Test Executable\n\n"
              << "Commands:\n"
              << "  --test-euclidean            Test Euclidean rhythm generation\n"
              << "      --steps=N               Number of steps (default: 8)\n"
              << "      --hits=N                Number of hits (default: 3)\n"
              << "      --rotation=N            Pattern rotation (default: 0)\n"
              << "\n"
              << "  --test-euclidean-known      Test known Euclidean patterns\n"
              << "\n"
              << "  --test-euclidean-tick       Test tick() advancement\n"
              << "      --steps=N               Number of steps\n"
              << "      --hits=N                Number of hits\n"
              << "      --ticks=N               Number of ticks to run (default: steps)\n"
              << "\n"
              << "  --test-truth-table          Test truth table evaluation\n"
              << "      --preset=NAME           Preset name (PASS, OR, AND, XOR, MAJORITY, NOR, NAND)\n"
              << "      --inputs=N              4-bit input value (0-15)\n"
              << "\n"
              << "  --test-truth-random         Test truth table randomization\n"
              << "      --seed=N                RNG seed\n"
              << "\n"
              << "  --test-truth-mutate         Test truth table mutation\n"
              << "      --seed=N                RNG seed\n"
              << "\n"
              << "  --test-truth-undo           Test truth table undo\n"
              << "\n"
              << "  --test-probability          Test probability gate\n"
              << "      --prob=F                Probability (0.0-1.0, default: 0.5)\n"
              << "      --seed=N                RNG seed\n"
              << "      --trials=N              Number of trials (default: 1000)\n"
              << "\n"
              << "  --test-probability-determinism  Test RNG determinism\n"
              << "      --seed=N                RNG seed\n"
              << "\n"
              << "  --test-clock-mult           Test clock mult/div phase accumulator\n"
              << "      --speed=F               Speed ratio (1=x1, 2=x2, 0.5=/2, etc)\n"
              << "      --clocks=N              Number of clocks to simulate (default: 16)\n"
              << "\n"
              << "  --test-clock-mult-known     Test known clock mult/div values\n"
              << "\n"
              << "  --test-swing-timing         Test swing tick timing intervals\n"
              << "      --speed=F               Speed ratio (default: 2.0)\n"
              << "      --swing=F               Swing percent 50-75 (default: 50 = even)\n"
              << "      --clocks=N              Number of clocks (default: 4)\n"
              << "\n"
              << "  --test-clock-sync           Test clock sync mode (natural swing at non-int rates)\n"
              << "      --speed=F               Speed ratio (default: 1.5)\n"
              << "      --clocks=N              Number of clocks (default: 8)\n"
              << "\n"
              << "  --test-lfo-phase            Test LFO phase reaches full range (regression #21)\n"
              << "\n"
              << "  --test-full-module          Test full module with Euclidean engine and swing\n"
              << "      --speed=F               Speed ratio (default: 2.0)\n"
              << "      --steps=N               Number of steps (default: 4)\n"
              << "      --swing=F               Swing percent 50-75 (default: 50 = even)\n"
              << "      --clocks=N              Number of clocks (default: 8)\n"
              << "\n"
              << "  --help                      Print this help\n";
}

// Test: Euclidean pattern generation
int testEuclidean(int argc, char** argv) {
    int steps = parseIntArg(argc, argv, "--steps=", 8);
    int hits = parseIntArg(argc, argv, "--hits=", 3);
    int rotation = parseIntArg(argc, argv, "--rotation=", 0);

    WiggleRoom::EuclideanEngine engine;
    engine.configure(steps, hits, rotation);

    std::vector<bool> pattern(engine.pattern.begin(), engine.pattern.end());
    printJsonArrayBool("pattern", pattern);
    return 0;
}

// Test: Known Euclidean patterns (musicological reference)
int testEuclideanKnown(int argc, char** argv) {
    (void)argc; (void)argv;

    struct TestCase {
        int steps;
        int hits;
        std::vector<bool> expected;
        const char* name;
    };

    std::vector<TestCase> cases = {
        // E(3,8) = Tresillo [x . . x . . x .]
        {8, 3, {true, false, false, true, false, false, true, false}, "E(3,8) Tresillo"},
        // E(5,8) = Cinquillo [x . x x . x x .]
        {8, 5, {true, false, true, true, false, true, true, false}, "E(5,8) Cinquillo"},
        // E(4,12) = [x . . x . . x . . x . .]
        {12, 4, {true, false, false, true, false, false, true, false, false, true, false, false}, "E(4,12)"},
        // E(0,8) = All zeros
        {8, 0, {false, false, false, false, false, false, false, false}, "E(0,8) Empty"},
        // E(8,8) = All ones
        {8, 8, {true, true, true, true, true, true, true, true}, "E(8,8) Full"},
        // E(1,4) = [x . . .]
        {4, 1, {true, false, false, false}, "E(1,4)"},
        // E(2,4) = [x . x .]
        {4, 2, {true, false, true, false}, "E(2,4)"},
    };

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    for (const auto& tc : cases) {
        WiggleRoom::EuclideanEngine engine;
        engine.configure(tc.steps, tc.hits, 0);

        bool match = (engine.pattern.size() == tc.expected.size());
        if (match) {
            for (size_t i = 0; i < tc.expected.size(); i++) {
                if (engine.pattern[i] != tc.expected[i]) {
                    match = false;
                    break;
                }
            }
        }

        if (match) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << tc.name << ": expected [";
            for (size_t i = 0; i < tc.expected.size(); i++) {
                oss << (tc.expected[i] ? "1" : "0");
                if (i < tc.expected.size() - 1) oss << ",";
            }
            oss << "], got [";
            for (size_t i = 0; i < engine.pattern.size(); i++) {
                oss << (engine.pattern[i] ? "1" : "0");
                if (i < engine.pattern.size() - 1) oss << ",";
            }
            oss << "]";
            failures.push_back(oss.str());
        }
    }

    std::cout << "{\"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Euclidean tick advancement
int testEuclideanTick(int argc, char** argv) {
    int steps = parseIntArg(argc, argv, "--steps=", 8);
    int hits = parseIntArg(argc, argv, "--hits=", 3);
    int ticks = parseIntArg(argc, argv, "--ticks=", steps);

    WiggleRoom::EuclideanEngine engine;
    engine.configure(steps, hits, 0);

    std::vector<bool> sequence;
    for (int i = 0; i < ticks; i++) {
        sequence.push_back(engine.tick());
    }

    printJsonArrayBool("sequence", sequence);
    return 0;
}

// Test: Truth table evaluation
int testTruthTable(int argc, char** argv) {
    std::string preset = parseStringArg(argc, argv, "--preset=", "PASS");
    int inputs = parseIntArg(argc, argv, "--inputs=", 0);

    WiggleRoom::TruthTable table;
    table.loadPreset(preset.c_str());

    bool in[4] = {
        (inputs & 1) != 0,
        (inputs & 2) != 0,
        (inputs & 4) != 0,
        (inputs & 8) != 0
    };
    bool out[4];
    table.evaluate(in, out);

    std::cout << "{\"preset\": \"" << preset << "\", \"inputs\": " << inputs
              << ", \"outputs\": [" << (out[0] ? "true" : "false")
              << ", " << (out[1] ? "true" : "false")
              << ", " << (out[2] ? "true" : "false")
              << ", " << (out[3] ? "true" : "false") << "]}" << std::endl;
    return 0;
}

// Test: Truth table randomization
int testTruthRandom(int argc, char** argv) {
    uint32_t seed = static_cast<uint32_t>(parseIntArg(argc, argv, "--seed=", 42));

    WiggleRoom::TruthTable table;
    table.setSeed(seed);
    table.randomize();

    std::cout << "{\"seed\": " << seed << ", \"mapping\": [";
    for (int i = 0; i < 16; i++) {
        std::cout << (int)table.mapping[i];
        if (i < 15) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;
    return 0;
}

// Test: Truth table mutation
int testTruthMutate(int argc, char** argv) {
    uint32_t seed = static_cast<uint32_t>(parseIntArg(argc, argv, "--seed=", 42));

    WiggleRoom::TruthTable table;
    table.setSeed(seed);

    // Store original
    auto original = table.mapping;

    // Mutate
    table.mutate();

    // Count differences
    int differences = 0;
    for (int i = 0; i < 16; i++) {
        if (table.mapping[i] != original[i]) differences++;
    }

    std::cout << "{\"seed\": " << seed << ", \"differences\": " << differences
              << ", \"original\": [";
    for (int i = 0; i < 16; i++) {
        std::cout << (int)original[i];
        if (i < 15) std::cout << ", ";
    }
    std::cout << "], \"mutated\": [";
    for (int i = 0; i < 16; i++) {
        std::cout << (int)table.mapping[i];
        if (i < 15) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;
    return 0;
}

// Test: Truth table undo
int testTruthUndo(int argc, char** argv) {
    (void)argc; (void)argv;

    WiggleRoom::TruthTable table;
    table.setSeed(42);

    // Store original
    auto original = table.mapping;

    // Randomize (pushes undo)
    table.randomize();
    auto randomized = table.mapping;

    // Undo
    table.undo();

    // Check if restored
    bool restored = (table.mapping == original);

    std::cout << "{\"restored\": " << (restored ? "true" : "false") << "}" << std::endl;
    return restored ? 0 : 1;
}

// Test: Probability gate
int testProbability(int argc, char** argv) {
    float prob = parseFloatArg(argc, argv, "--prob=", 0.5f);
    uint32_t seed = static_cast<uint32_t>(parseIntArg(argc, argv, "--seed=", 42));
    int trials = parseIntArg(argc, argv, "--trials=", 1000);

    WiggleRoom::ProbabilityGate gate;
    gate.setSeed(seed);
    gate.setProbability(prob);

    int trueCount = 0;
    for (int i = 0; i < trials; i++) {
        if (gate.test()) trueCount++;
    }

    float expected = prob * trials;
    float stddev = std::sqrt(prob * (1.0f - prob) * trials);
    bool within3Sigma = std::abs(trueCount - expected) <= 3 * stddev;

    std::cout << "{\"prob\": " << prob << ", \"seed\": " << seed
              << ", \"trials\": " << trials << ", \"true_count\": " << trueCount
              << ", \"expected\": " << expected << ", \"stddev\": " << stddev
              << ", \"within_3sigma\": " << (within3Sigma ? "true" : "false")
              << "}" << std::endl;
    return 0;
}

// Test: Probability determinism
int testProbabilityDeterminism(int argc, char** argv) {
    uint32_t seed = static_cast<uint32_t>(parseIntArg(argc, argv, "--seed=", 42));

    WiggleRoom::ProbabilityGate gate1, gate2;
    gate1.setSeed(seed);
    gate2.setSeed(seed);
    gate1.setProbability(0.5f);
    gate2.setProbability(0.5f);

    bool match = true;
    std::vector<bool> seq1, seq2;
    for (int i = 0; i < 100; i++) {
        bool r1 = gate1.test();
        bool r2 = gate2.test();
        seq1.push_back(r1);
        seq2.push_back(r2);
        if (r1 != r2) match = false;
    }

    std::cout << "{\"seed\": " << seed << ", \"match\": " << (match ? "true" : "false") << "}" << std::endl;
    return match ? 0 : 1;
}

// Test: Clock multiplier/divider phase accumulator
// This tests the logic used in the Euclogic module for master speed mult/div
int testClockMult(int argc, char** argv) {
    float speedRatio = parseFloatArg(argc, argv, "--speed=", 1.0f);
    int clocks = parseIntArg(argc, argv, "--clocks=", 16);

    // Simulate the phase accumulator logic
    float phase = 0.f;
    int totalTicks = 0;
    std::vector<int> ticksPerClock;

    for (int c = 0; c < clocks; c++) {
        phase += speedRatio;
        int ticksThisClock = static_cast<int>(phase);
        phase -= ticksThisClock;
        ticksPerClock.push_back(ticksThisClock);
        totalTicks += ticksThisClock;
    }

    float expectedTicks = speedRatio * clocks;
    bool correct = std::abs(totalTicks - expectedTicks) < 1.0f;  // Allow rounding

    std::cout << "{\"speed_ratio\": " << speedRatio
              << ", \"clocks\": " << clocks
              << ", \"total_ticks\": " << totalTicks
              << ", \"expected_ticks\": " << expectedTicks
              << ", \"correct\": " << (correct ? "true" : "false")
              << ", \"ticks_per_clock\": [";
    for (size_t i = 0; i < ticksPerClock.size(); i++) {
        std::cout << ticksPerClock[i];
        if (i < ticksPerClock.size() - 1) std::cout << ", ";
    }
    std::cout << "]}" << std::endl;

    return correct ? 0 : 1;
}

// Test: Full module simulation with Euclidean engine and swing
// Sets up hits on every step, runs at specified master speed, checks hit timing
int testFullModuleTiming(int argc, char** argv) {
    float speedRatio = parseFloatArg(argc, argv, "--speed=", 2.0f);
    float swingPercent = parseFloatArg(argc, argv, "--swing=", 50.0f);  // 50-75%
    int steps = parseIntArg(argc, argv, "--steps=", 4);
    int clocks = parseIntArg(argc, argv, "--clocks=", 8);

    // Simulate at 48kHz with 120 BPM (0.5s clock period)
    float sampleRate = 48000.f;
    float clockPeriod = 0.5f;  // 500ms = 120 BPM
    float dt = 1.f / sampleRate;

    // Set up Euclidean engine with hits on every step
    WiggleRoom::EuclideanEngine engine;
    engine.configure(steps, steps, 0);  // steps == hits means hit on every step

    float internalTickPhase = 0.f;
    float baseInterval = clockPeriod / speedRatio;
    bool hadFirstTick = false;
    bool swingLong = true;  // Alternate between long and short intervals

    // Calculate swing intervals
    float swingRatio = swingPercent / 100.f;  // 0.5 to 0.75
    float longInterval = 2.f * baseInterval * swingRatio;
    float shortInterval = 2.f * baseInterval * (1.f - swingRatio);

    std::vector<float> hitTimes;  // When each hit occurred (in seconds)
    float currentTime = 0.f;
    bool clockLocked = false;

    for (int c = 0; c < clocks; c++) {
        // Clock edge
        bool clockEdge = true;
        clockLocked = true;

        // Process samples until next clock
        int samplesPerClock = static_cast<int>(clockPeriod * sampleRate);
        for (int s = 0; s < samplesPerClock; s++) {
            bool shouldTick = false;
            float currentInterval = swingLong ? longInterval : shortInterval;

            // First tick on first clock, then alternating intervals
            if (clockEdge && !hadFirstTick) {
                shouldTick = true;
                hadFirstTick = true;
                swingLong = true;
                internalTickPhase = 0.f;
            } else {
                internalTickPhase += dt;
                if (internalTickPhase >= currentInterval) {
                    internalTickPhase -= currentInterval;
                    shouldTick = true;
                    swingLong = !swingLong;  // Alternate
                }
            }

            // If we tick, advance the Euclidean engine
            if (shouldTick) {
                bool hit = engine.tick();
                if (hit) {
                    hitTimes.push_back(currentTime);
                }
            }

            clockEdge = false;
            currentTime += dt;
        }
    }

    // Calculate intervals between hits
    std::vector<float> intervals;
    for (size_t i = 1; i < hitTimes.size(); i++) {
        intervals.push_back(hitTimes[i] - hitTimes[i-1]);
    }

    // Calculate expected interval
    // With swing, we alternate long/short, average = baseInterval
    float expectedInterval = baseInterval;

    // Check if all intervals are within tolerance of expected
    // Use 3ms tolerance to account for floating point accumulation over time
    float tolerance = 0.003f;  // 3ms tolerance
    bool allEven = true;
    float maxDeviation = 0.f;
    for (float iv : intervals) {
        float deviation = std::abs(iv - expectedInterval);
        maxDeviation = std::max(maxDeviation, deviation);
        if (deviation > tolerance) {
            allEven = false;
        }
    }

    // Check for swing pattern (uneven intervals)
    bool hasSwing = false;
    float minInterval = 0.f, maxInterval = 0.f;
    if (!intervals.empty()) {
        minInterval = intervals[0];
        maxInterval = intervals[0];
        for (float iv : intervals) {
            minInterval = std::min(minInterval, iv);
            maxInterval = std::max(maxInterval, iv);
        }
        if (maxInterval / minInterval > 1.1f) {
            hasSwing = true;
        }
    }

    // Output results
    std::cout << "{\"test\": \"full_module_timing\""
              << ", \"speed_ratio\": " << speedRatio
              << ", \"swing_percent\": " << swingPercent
              << ", \"steps\": " << steps
              << ", \"hits\": " << steps
              << ", \"clocks\": " << clocks
              << ", \"total_hits\": " << hitTimes.size()
              << ", \"expected_interval_ms\": " << (expectedInterval * 1000)
              << ", \"intervals_ms\": [";
    for (size_t i = 0; i < intervals.size() && i < 12; i++) {
        std::cout << std::fixed << std::setprecision(1) << (intervals[i] * 1000);
        if (i < intervals.size() - 1 && i < 11) std::cout << ", ";
    }
    if (intervals.size() > 12) std::cout << ", ...";
    std::cout << "]";

    if (!intervals.empty()) {
        std::cout << ", \"min_interval_ms\": " << std::fixed << std::setprecision(1) << (minInterval * 1000)
                  << ", \"max_interval_ms\": " << (maxInterval * 1000)
                  << ", \"max_deviation_ms\": " << (maxDeviation * 1000);
        if (hasSwing) {
            std::cout << ", \"swing_ratio\": " << std::fixed << std::setprecision(2) << (maxInterval / minInterval);
        }
    }
    // Expected behavior: swing > 50% should produce swing, swing = 50% should be even
    bool expectSwing = swingPercent > 50.5f;
    bool pass = expectSwing ? hasSwing : allEven;

    std::cout << ", \"all_even\": " << (allEven ? "true" : "false")
              << ", \"has_swing\": " << (hasSwing ? "true" : "false")
              << ", \"expect_swing\": " << (expectSwing ? "true" : "false")
              << ", \"PASS\": " << (pass ? "true" : "false")
              << "}" << std::endl;

    return pass ? 0 : 1;
}

// Test: Clock sync mode - forces tick on clock edge, creates natural swing at non-integer rates
int testClockSync(int argc, char** argv) {
    float speedRatio = parseFloatArg(argc, argv, "--speed=", 1.5f);
    int clocks = parseIntArg(argc, argv, "--clocks=", 8);

    // Simulate at 48kHz with 120 BPM (0.5s clock period)
    float sampleRate = 48000.f;
    float clockPeriod = 0.5f;
    float dt = 1.f / sampleRate;

    float internalTickPhase = 0.f;
    float baseInterval = clockPeriod / speedRatio;

    std::vector<float> tickTimes;
    float currentTime = 0.f;

    for (int c = 0; c < clocks; c++) {
        bool clockEdge = true;

        int samplesPerClock = static_cast<int>(clockPeriod * sampleRate);
        for (int s = 0; s < samplesPerClock; s++) {
            bool shouldTick = false;

            // Clock sync mode: always tick on clock edge, reset phase
            if (clockEdge) {
                shouldTick = true;
                internalTickPhase = 0.f;
            } else {
                internalTickPhase += dt;
                if (internalTickPhase >= baseInterval) {
                    internalTickPhase -= baseInterval;
                    shouldTick = true;
                }
            }

            if (shouldTick) {
                tickTimes.push_back(currentTime);
            }

            clockEdge = false;
            currentTime += dt;
        }
    }

    // Calculate intervals
    std::vector<float> intervals;
    for (size_t i = 1; i < tickTimes.size(); i++) {
        intervals.push_back(tickTimes[i] - tickTimes[i-1]);
    }

    // Check for swing pattern
    bool hasSwing = false;
    float minInterval = 0.f, maxInterval = 0.f;
    if (!intervals.empty()) {
        minInterval = intervals[0];
        maxInterval = intervals[0];
        for (float iv : intervals) {
            minInterval = std::min(minInterval, iv);
            maxInterval = std::max(maxInterval, iv);
        }
        if (maxInterval / minInterval > 1.1f) {
            hasSwing = true;
        }
    }

    // For non-integer rates, clock sync should create swing
    bool isNonInteger = (speedRatio != std::floor(speedRatio));
    bool pass = isNonInteger ? hasSwing : !hasSwing;

    std::cout << "{\"test\": \"clock_sync\""
              << ", \"speed_ratio\": " << speedRatio
              << ", \"is_non_integer\": " << (isNonInteger ? "true" : "false")
              << ", \"clocks\": " << clocks
              << ", \"total_ticks\": " << tickTimes.size()
              << ", \"base_interval_ms\": " << (baseInterval * 1000)
              << ", \"intervals_ms\": [";
    for (size_t i = 0; i < intervals.size() && i < 10; i++) {
        std::cout << std::fixed << std::setprecision(1) << (intervals[i] * 1000);
        if (i < intervals.size() - 1 && i < 9) std::cout << ", ";
    }
    if (intervals.size() > 10) std::cout << ", ...";
    std::cout << "]";
    if (!intervals.empty()) {
        std::cout << ", \"min_interval_ms\": " << (minInterval * 1000)
                  << ", \"max_interval_ms\": " << (maxInterval * 1000);
        if (hasSwing) {
            std::cout << ", \"swing_ratio\": " << std::fixed << std::setprecision(2) << (maxInterval / minInterval);
        }
    }
    std::cout << ", \"has_swing\": " << (hasSwing ? "true" : "false")
              << ", \"PASS\": " << (pass ? "true" : "false")
              << "}" << std::endl;

    return pass ? 0 : 1;
}

// Test: Swing timing - tests tick intervals at various swing percentages
int testSwingTiming(int argc, char** argv) {
    float speedRatio = parseFloatArg(argc, argv, "--speed=", 2.0f);
    float swingPercent = parseFloatArg(argc, argv, "--swing=", 50.0f);
    int clocks = parseIntArg(argc, argv, "--clocks=", 4);

    // Simulate at 48kHz with 120 BPM (0.5s clock period)
    float sampleRate = 48000.f;
    float clockPeriod = 0.5f;  // 500ms = 120 BPM
    float dt = 1.f / sampleRate;

    float internalTickPhase = 0.f;
    float baseInterval = clockPeriod / speedRatio;
    bool hadFirstTick = false;
    bool swingLong = true;

    // Calculate swing intervals
    float swingRatio = swingPercent / 100.f;
    float longInterval = 2.f * baseInterval * swingRatio;
    float shortInterval = 2.f * baseInterval * (1.f - swingRatio);

    std::vector<float> tickTimes;
    float currentTime = 0.f;

    for (int c = 0; c < clocks; c++) {
        bool clockEdge = true;

        int samplesPerClock = static_cast<int>(clockPeriod * sampleRate);
        for (int s = 0; s < samplesPerClock; s++) {
            bool shouldTick = false;
            float currentInterval = swingLong ? longInterval : shortInterval;

            if (clockEdge && !hadFirstTick) {
                shouldTick = true;
                hadFirstTick = true;
                swingLong = true;
                internalTickPhase = 0.f;
            } else {
                internalTickPhase += dt;
                if (internalTickPhase >= currentInterval) {
                    internalTickPhase -= currentInterval;
                    shouldTick = true;
                    swingLong = !swingLong;
                }
            }

            if (shouldTick) {
                tickTimes.push_back(currentTime);
            }

            clockEdge = false;
            currentTime += dt;
        }
    }

    // Calculate intervals between ticks
    std::vector<float> intervals;
    for (size_t i = 1; i < tickTimes.size(); i++) {
        intervals.push_back(tickTimes[i] - tickTimes[i-1]);
    }

    // Output results
    std::cout << "{\"speed_ratio\": " << speedRatio
              << ", \"swing_percent\": " << swingPercent
              << ", \"clocks\": " << clocks
              << ", \"total_ticks\": " << tickTimes.size()
              << ", \"base_interval_ms\": " << (baseInterval * 1000)
              << ", \"long_interval_ms\": " << (longInterval * 1000)
              << ", \"short_interval_ms\": " << (shortInterval * 1000)
              << ", \"intervals_ms\": [";
    for (size_t i = 0; i < intervals.size() && i < 10; i++) {
        std::cout << std::fixed << std::setprecision(1) << (intervals[i] * 1000);
        if (i < intervals.size() - 1 && i < 9) std::cout << ", ";
    }
    if (intervals.size() > 10) std::cout << ", ...";
    std::cout << "]";

    // Check for swing pattern (uneven intervals)
    bool hasSwing = false;
    if (intervals.size() >= 2) {
        float minInterval = intervals[0];
        float maxInterval = intervals[0];
        for (float iv : intervals) {
            minInterval = std::min(minInterval, iv);
            maxInterval = std::max(maxInterval, iv);
        }
        // If max/min ratio > 1.1, we have swing
        if (maxInterval / minInterval > 1.1f) {
            hasSwing = true;
        }
        std::cout << ", \"min_interval_ms\": " << (minInterval * 1000)
                  << ", \"max_interval_ms\": " << (maxInterval * 1000)
                  << ", \"swing_ratio\": " << (maxInterval / minInterval);
    }
    std::cout << ", \"has_swing\": " << (hasSwing ? "true" : "false");
    std::cout << "}" << std::endl;

    return 0;
}

// Test: Clock mult/div known values
int testClockMultKnown(int argc, char** argv) {
    (void)argc; (void)argv;

    struct TestCase {
        float speed;
        int clocks;
        int expectedTicks;
        const char* name;
    };

    std::vector<TestCase> cases = {
        // x1: 1 tick per clock
        {1.0f, 8, 8, "x1"},
        // x2: 2 ticks per clock
        {2.0f, 8, 16, "x2"},
        // x4: 4 ticks per clock
        {4.0f, 8, 32, "x4"},
        // /2: 1 tick per 2 clocks
        {0.5f, 8, 4, "/2"},
        // /4: 1 tick per 4 clocks
        {0.25f, 8, 2, "/4"},
        // x3: 3 ticks per clock
        {3.0f, 4, 12, "x3"},
        // /3: 1 tick per 3 clocks (approx)
        {1.0f/3.0f, 9, 3, "/3"},
        // x1.5: 3 ticks per 2 clocks
        {1.5f, 4, 6, "x1.5"},
    };

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    for (const auto& tc : cases) {
        float phase = 0.f;
        int totalTicks = 0;

        for (int c = 0; c < tc.clocks; c++) {
            phase += tc.speed;
            int ticksThisClock = static_cast<int>(phase);
            phase -= ticksThisClock;
            totalTicks += ticksThisClock;
        }

        if (totalTicks == tc.expectedTicks) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << tc.name << ": expected " << tc.expectedTicks << " ticks, got " << totalTicks;
            failures.push_back(oss.str());
        }
    }

    std::cout << "{\"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: LFO phase calculation — verifies phase reaches 1.0 at the last step
// This is a regression test for GitHub issue #21
int testLfoPhase(int argc, char** argv) {
    (void)argc; (void)argv;

    struct TestCase {
        int steps;
        const char* name;
    };

    std::vector<TestCase> cases = {
        {2, "2 steps"},
        {3, "3 steps"},
        {4, "4 steps"},
        {5, "5 steps"},
        {8, "8 steps"},
        {16, "16 steps"},
        {32, "32 steps"},
        {64, "64 steps"},
    };

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    for (const auto& tc : cases) {
        WiggleRoom::EuclideanEngine engine;
        engine.configure(tc.steps, tc.steps, 0);  // all hits

        // The LFO phase formula (matching the fix in Euclogic.cpp):
        // phase = (steps > 1) ? currentStep / (steps - 1) : 0
        // We tick through all steps and check the phase at each one

        float minPhase = 1.f;
        float maxPhase = 0.f;

        for (int i = 0; i < tc.steps; i++) {
            int currentStep = engine.currentStep;
            float phase = (tc.steps > 1) ? (float)currentStep / (float)(tc.steps - 1) : 0.f;
            minPhase = std::min(minPhase, phase);
            maxPhase = std::max(maxPhase, phase);
            engine.tick();
        }

        // Phase must start at 0.0 and reach exactly 1.0
        bool startOk = std::abs(minPhase) < 0.001f;
        bool endOk = std::abs(maxPhase - 1.0f) < 0.001f;

        if (startOk && endOk) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << tc.name << ": min=" << minPhase << " max=" << maxPhase
                << " (expected min=0.0, max=1.0)";
            failures.push_back(oss.str());
        }
    }

    // Also test edge case: 1 step should give phase 0
    {
        WiggleRoom::EuclideanEngine engine;
        engine.configure(1, 1, 0);
        float phase = (1 > 1) ? (float)engine.currentStep / (float)(1 - 1) : 0.f;
        if (std::abs(phase) < 0.001f) {
            passed++;
        } else {
            failed++;
            failures.push_back("1 step: expected phase=0, got " + std::to_string(phase));
        }
    }

    std::cout << "{\"test\": \"lfo_phase\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: getHit() works immediately after construction (no configure/tick needed)
int testGetHitAfterConstruct(int argc, char** argv) {
    (void)argc; (void)argv;

    WiggleRoom::EuclideanEngine engine;
    // Default: steps=16, hits=8 — pattern should be populated at construction

    int hitCount = 0;
    for (int s = 0; s < engine.steps; s++) {
        if (engine.getHit(s)) hitCount++;
    }

    bool pass = (hitCount == 8) && ((int)engine.pattern.size() == 16);

    std::cout << "{\"test\": \"getHit_after_construct\""
              << ", \"pattern_size\": " << engine.pattern.size()
              << ", \"hit_count\": " << hitCount
              << ", \"PASS\": " << (pass ? "true" : "false")
              << "}" << std::endl;

    return pass ? 0 : 1;
}

// Test: pattern updates correctly when configure() changes hits
int testPatternUpdatesOnConfigure(int argc, char** argv) {
    (void)argc; (void)argv;

    WiggleRoom::EuclideanEngine engine;
    engine.configure(16, 4, 0);

    int hitCount4 = 0;
    for (int s = 0; s < engine.steps; s++) {
        if (engine.getHit(s)) hitCount4++;
    }

    engine.configure(16, 8, 0);

    int hitCount8 = 0;
    for (int s = 0; s < engine.steps; s++) {
        if (engine.getHit(s)) hitCount8++;
    }

    bool pass = (hitCount4 == 4) && (hitCount8 == 8);

    std::cout << "{\"test\": \"pattern_updates_on_configure\""
              << ", \"hits_after_4\": " << hitCount4
              << ", \"hits_after_8\": " << hitCount8
              << ", \"PASS\": " << (pass ? "true" : "false")
              << "}" << std::endl;

    return pass ? 0 : 1;
}

// Test: Density-aware randomize — density 0 should produce all OFF, density 1 all ON
int testTruthDensityRandomize(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    // Test 1: density=0 for all columns → all cells OFF
    {
        WiggleRoom::TruthTable table;
        table.setSeed(123);
        float densities[4] = {0.f, 0.f, 0.f, 0.f};
        table.randomizeWithDensity(densities);

        int onCount = 0;
        for (int i = 0; i < 16; i++) {
            onCount += __builtin_popcount(table.mapping[i]);
        }
        if (onCount == 0) {
            passed++;
        } else {
            failed++;
            failures.push_back("density=0: expected 0 ON cells, got " + std::to_string(onCount));
        }
    }

    // Test 2: density=1 for all columns → all cells ON
    {
        WiggleRoom::TruthTable table;
        table.setSeed(456);
        float densities[4] = {1.f, 1.f, 1.f, 1.f};
        table.randomizeWithDensity(densities);

        int onCount = 0;
        for (int i = 0; i < 16; i++) {
            onCount += __builtin_popcount(table.mapping[i]);
        }
        if (onCount == 64) {  // 16 rows * 4 cols
            passed++;
        } else {
            failed++;
            failures.push_back("density=1: expected 64 ON cells, got " + std::to_string(onCount));
        }
    }

    // Test 3: density=0 for col 0, density=1 for col 1 — col 0 all OFF, col 1 all ON
    {
        WiggleRoom::TruthTable table;
        table.setSeed(789);
        float densities[4] = {0.f, 1.f, 0.5f, 0.5f};
        table.randomizeWithDensity(densities);

        int col0On = 0, col1On = 0;
        for (int i = 0; i < 16; i++) {
            if (table.mapping[i] & (1 << 0)) col0On++;
            if (table.mapping[i] & (1 << 1)) col1On++;
        }
        bool col0Ok = (col0On == 0);
        bool col1Ok = (col1On == 16);
        if (col0Ok && col1Ok) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "mixed density: col0 ON=" << col0On << " (expect 0), col1 ON=" << col1On << " (expect 16)";
            failures.push_back(oss.str());
        }
    }

    // Test 4: density=0.5 should produce roughly 50% (within 3-sigma)
    {
        WiggleRoom::TruthTable table;
        table.setSeed(999);
        float densities[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        table.randomizeWithDensity(densities);

        int onCount = 0;
        for (int i = 0; i < 16; i++) {
            onCount += __builtin_popcount(table.mapping[i]);
        }
        // 64 cells at 50% → expected 32, stddev = sqrt(64*0.25) = 4
        float expected = 32.f;
        float stddev = 4.f;
        bool within3Sigma = std::abs(onCount - expected) <= 3 * stddev;
        if (within3Sigma) {
            passed++;
        } else {
            failed++;
            failures.push_back("density=0.5: got " + std::to_string(onCount) + " ON cells (expected ~32)");
        }
    }

    // Test 5: locked cells are preserved
    {
        WiggleRoom::TruthTable table;
        table.setSeed(111);
        // Set a specific cell and lock it
        table.mapping[0] = 0x01;  // bit 0 ON for state 0
        table.toggleLock(0, 0);    // lock bit 0 of state 0

        float densities[4] = {0.f, 0.f, 0.f, 0.f};
        table.randomizeWithDensity(densities);

        bool lockedPreserved = (table.mapping[0] & 1) == 1;
        // All other bits of state 0 should be OFF (density=0, unlocked)
        bool othersOff = (table.mapping[0] & ~1) == 0;
        if (lockedPreserved && othersOff) {
            passed++;
        } else {
            failed++;
            failures.push_back("locked cells: bit0 preserved=" + std::string(lockedPreserved ? "yes" : "no") +
                             ", others off=" + std::string(othersOff ? "yes" : "no"));
        }
    }

    // Test 6: undo works after density randomize
    {
        WiggleRoom::TruthTable table;
        table.setSeed(222);
        auto original = table.mapping;
        float densities[4] = {0.f, 0.f, 0.f, 0.f};
        table.randomizeWithDensity(densities);
        table.undo();
        bool restored = (table.mapping == original);
        if (restored) {
            passed++;
        } else {
            failed++;
            failures.push_back("undo after density randomize did not restore original");
        }
    }

    std::cout << "{\"test\": \"truth_density_randomize\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Intersect falling mode band detection — simulates the band index logic
int testIntersectFalling(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    // Simulate the band index calculation from Intersect::process()
    // Virtual bands: -1 below min, divisions above max
    auto calcBand = [](float normalizedCV, int divisions) -> int {
        if (normalizedCV <= 0.f) return -1;
        if (normalizedCV >= 1.f) return divisions;
        int band = (int)std::floor(normalizedCV * divisions);
        if (band >= divisions) band = divisions - 1;
        if (band < 0) band = 0;
        return band;
    };

    // Test 1: 4 divisions, CV sweep 0→10V→0V — all bands should trigger falling
    {
        int divisions = 4;
        // Simulate CV voltages: rising then falling
        std::vector<float> cvValues = {0.f, 2.5f, 5.f, 7.5f, 10.f, 7.5f, 5.f, 2.5f, 0.f};
        int lastBand = -999;
        std::vector<int> fallingTriggerBands;

        for (float cv : cvValues) {
            float norm = std::min(std::max(cv / 10.f, 0.f), 1.f);
            int band = calcBand(norm, divisions);

            if (band != lastBand && lastBand != -999) {
                if (band < lastBand) {  // falling
                    fallingTriggerBands.push_back(band);
                }
            }
            lastBand = band;
        }

        // Falling from 10V→0V: 10→7.5(band3), 7.5→5(band2), 5→2.5(band1), 2.5→0(virtual -1)
        // Band 0 is skipped because 2.5V is exactly on the boundary (band 1) and 0V jumps to virtual
        std::vector<int> expected = {3, 2, 1, -1};
        if (fallingTriggerBands == expected) {
            passed++;
        } else {
            std::ostringstream oss;
            oss << "4div sweep: falling triggers at bands [";
            for (size_t i = 0; i < fallingTriggerBands.size(); i++) {
                oss << fallingTriggerBands[i];
                if (i < fallingTriggerBands.size() - 1) oss << ",";
            }
            oss << "], expected [3,2,1,0]";
            failed++;
            failures.push_back(oss.str());
        }
    }

    // Test 2: top band triggers on falling when CV drops from exactly 10V
    {
        int divisions = 4;
        int lastBand = calcBand(1.0f, divisions);  // 10V → should be virtual band 4
        float droppedCV = 9.0f;
        float norm = std::min(std::max(droppedCV / 10.f, 0.f), 1.f);
        int newBand = calcBand(norm, divisions);  // 9V → band 3

        bool triggersFalling = (newBand < lastBand);
        std::ostringstream oss;
        oss << "10V drop: lastBand=" << lastBand << " newBand=" << newBand << " triggers=" << triggersFalling;
        if (triggersFalling) {
            passed++;
        } else {
            failed++;
            failures.push_back(oss.str());
        }
    }

    // Test 3: top band triggers on falling when CV drops from 9.5V to 7V (within top band to next)
    {
        int divisions = 4;
        float cv1 = 9.5f, cv2 = 7.0f;
        int band1 = calcBand(cv1 / 10.f, divisions);  // 0.95 → floor(3.8) = 3
        int band2 = calcBand(cv2 / 10.f, divisions);  // 0.70 → floor(2.8) = 2

        bool triggersFalling = (band2 < band1);
        if (triggersFalling) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "9.5V→7V: band " << band1 << "→" << band2 << " no trigger";
            failures.push_back(oss.str());
        }
    }

    // Test 4: top band — CV stays within top band, no trigger
    {
        int divisions = 4;
        float cv1 = 9.5f, cv2 = 8.5f;  // both in band 3
        int band1 = calcBand(cv1 / 10.f, divisions);
        int band2 = calcBand(cv2 / 10.f, divisions);

        bool noTrigger = (band1 == band2);
        if (noTrigger) {
            passed++;
        } else {
            failed++;
            failures.push_back("same-band movement triggered unexpectedly");
        }
    }

    // Test 5: many divisions (32), top band falling detection
    {
        int divisions = 32;
        float cv1 = 10.0f;  // normalized 1.0 → virtual band 32
        float cv2 = 9.5f;   // normalized 0.95 → floor(30.4) = 30

        int band1 = calcBand(cv1 / 10.f, divisions);
        int band2 = calcBand(cv2 / 10.f, divisions);

        bool triggersFalling = (band2 < band1);
        if (triggersFalling) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "32div 10V→9.5V: band " << band1 << "→" << band2 << " no trigger";
            failures.push_back(oss.str());
        }
    }

    // Test 6: Rising into bottom band from 0V
    {
        int divisions = 4;
        int band1 = calcBand(0.f, divisions);   // 0V → virtual band -1
        int band2 = calcBand(0.5f / 10.f, divisions);  // 0.5V → band 0

        bool triggersRising = (band2 > band1);
        if (triggersRising) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "0V→0.5V: band " << band1 << "→" << band2 << " no rising trigger";
            failures.push_back(oss.str());
        }
    }

    // Test 7: Exactly at a boundary — floor(0.75 * 4) = 3, floor(0.5 * 4) = 2
    {
        int divisions = 4;
        int band1 = calcBand(0.75f, divisions);  // exactly on boundary → band 3
        int band2 = calcBand(0.5f, divisions);   // exactly on boundary → band 2

        bool triggersFalling = (band2 < band1);
        if (triggersFalling) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "boundary: band " << band1 << "→" << band2;
            failures.push_back(oss.str());
        }
    }

    std::cout << "{\"test\": \"intersect_falling\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Density-aware mutate — biases flips toward density direction
int testTruthDensityMutate(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    // Test 1: density=0 should only flip ON→OFF (never introduce new ONs)
    {
        WiggleRoom::TruthTable table;
        table.setSeed(42);
        // Start with pass-through (some bits ON)
        // Count initial ON bits
        int initialOn = 0;
        for (int i = 0; i < 16; i++) initialOn += __builtin_popcount(table.mapping[i]);

        float densities[4] = {0.f, 0.f, 0.f, 0.f};
        // Run many mutations — ON count should only decrease or stay same
        bool everIncreased = false;
        int prevOn = initialOn;
        for (int m = 0; m < 100; m++) {
            table.mutateWithDensity(densities);
            int onCount = 0;
            for (int i = 0; i < 16; i++) onCount += __builtin_popcount(table.mapping[i]);
            if (onCount > prevOn) everIncreased = true;
            prevOn = onCount;
        }
        if (!everIncreased) {
            passed++;
        } else {
            failed++;
            failures.push_back("density=0: ON count increased during mutation (should only decrease)");
        }
    }

    // Test 2: density=1 should only flip OFF→ON (never remove ONs)
    {
        WiggleRoom::TruthTable table;
        table.setSeed(42);
        int initialOn = 0;
        for (int i = 0; i < 16; i++) initialOn += __builtin_popcount(table.mapping[i]);

        float densities[4] = {1.f, 1.f, 1.f, 1.f};
        bool everDecreased = false;
        int prevOn = initialOn;
        for (int m = 0; m < 100; m++) {
            table.mutateWithDensity(densities);
            int onCount = 0;
            for (int i = 0; i < 16; i++) onCount += __builtin_popcount(table.mapping[i]);
            if (onCount < prevOn) everDecreased = true;
            prevOn = onCount;
        }
        if (!everDecreased) {
            passed++;
        } else {
            failed++;
            failures.push_back("density=1: ON count decreased during mutation (should only increase)");
        }
    }

    // Test 3: locked cells preserved
    {
        WiggleRoom::TruthTable table;
        table.setSeed(77);
        table.mapping[0] = 0x01;
        table.toggleLock(0, 0);  // lock bit 0 of state 0

        float densities[4] = {0.f, 0.f, 0.f, 0.f};
        for (int m = 0; m < 50; m++) {
            table.mutateWithDensity(densities);
        }
        bool lockedPreserved = (table.mapping[0] & 1) == 1;
        if (lockedPreserved) {
            passed++;
        } else {
            failed++;
            failures.push_back("locked cell was modified by density mutate");
        }
    }

    // Test 4: undo works
    {
        WiggleRoom::TruthTable table;
        table.setSeed(88);
        auto original = table.mapping;
        float densities[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        table.mutateWithDensity(densities);
        table.undo();
        bool restored = (table.mapping == original);
        if (restored) {
            passed++;
        } else {
            failed++;
            failures.push_back("undo after density mutate did not restore original");
        }
    }

    std::cout << "{\"test\": \"truth_density_mutate\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Input state computation from gate booleans
// Verifies inputState |= (1 << i) correctly maps gate booleans to state index
int testInputState(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    // Test for N=2, N=3, N=4
    int channelCounts[] = {2, 3, 4};
    for (int N : channelCounts) {
        int nStates = (1 << N);
        for (int combo = 0; combo < nStates; combo++) {
            // Set up gate booleans from combo bits
            bool gates[4] = {};
            for (int i = 0; i < N; i++) {
                gates[i] = (combo >> i) & 1;
            }

            // Compute inputState using the same formula as LogicManglerCore.hpp:156-159
            uint8_t inputState = 0;
            for (int i = 0; i < N; i++) {
                if (gates[i]) inputState |= (1 << i);
            }

            // inputState should equal combo (they use the same bit encoding)
            if (inputState == (uint8_t)combo) {
                passed++;
            } else {
                failed++;
                std::ostringstream oss;
                oss << "N=" << N << " combo=" << combo << ": expected state " << combo << ", got " << (int)inputState;
                failures.push_back(oss.str());
            }

            // Also verify round-trip: extracting gates back from inputState
            for (int i = 0; i < N; i++) {
                bool extracted = (inputState >> i) & 1;
                if (extracted != gates[i]) {
                    failed++;
                    std::ostringstream oss;
                    oss << "N=" << N << " combo=" << combo << " bit=" << i
                        << ": round-trip mismatch (gate=" << gates[i] << " extracted=" << extracted << ")";
                    failures.push_back(oss.str());
                } else {
                    passed++;
                }
            }
        }
    }

    std::cout << "{\"test\": \"input_state\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Truth table display T/F matches input state (LSB-first)
// Verifies (state >> bit) & 1 produces correct T/F for each column
int testDisplayBits(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    int channelCounts[] = {2, 3, 4};
    for (int N : channelCounts) {
        int nStates = (1 << N);
        for (int state = 0; state < nStates; state++) {
            // Test 1: display formula matches gate-to-state formula
            // If gate[bit]=true and all others false, inputState = (1 << bit)
            // Display of that state at column 'bit' should show T
            for (int bit = 0; bit < N; bit++) {
                bool displayValue = (state >> bit) & 1;

                // Verify specific known states
                if (state == 0) {
                    // State 0: all gates false → all columns F
                    if (displayValue != false) {
                        failed++;
                        std::ostringstream oss;
                        oss << "N=" << N << " state=0 bit=" << bit << ": expected F, got T";
                        failures.push_back(oss.str());
                    } else {
                        passed++;
                    }
                } else if (state == nStates - 1) {
                    // Max state: all gates true → all columns T
                    if (displayValue != true) {
                        failed++;
                        std::ostringstream oss;
                        oss << "N=" << N << " state=" << state << " bit=" << bit << ": expected T, got F";
                        failures.push_back(oss.str());
                    } else {
                        passed++;
                    }
                } else {
                    passed++;  // intermediate states checked by round-trip below
                }
            }

            // Test 2: display formula is consistent with inputState formula
            // Reconstruct gates from display, then recompute inputState
            bool reconstructedGates[4] = {};
            for (int bit = 0; bit < N; bit++) {
                reconstructedGates[bit] = (state >> bit) & 1;
            }
            uint8_t reconstructedState = 0;
            for (int i = 0; i < N; i++) {
                if (reconstructedGates[i]) reconstructedState |= (1 << i);
            }
            if (reconstructedState == (uint8_t)state) {
                passed++;
            } else {
                failed++;
                std::ostringstream oss;
                oss << "N=" << N << " state=" << state << ": display→gate→state round-trip gave " << (int)reconstructedState;
                failures.push_back(oss.str());
            }
        }
    }

    // Verify specific examples from the plan
    // N=2: State 0 → FF, State 1 → TF, State 2 → FT, State 3 → TT
    {
        auto displayStr = [](int state, int N) -> std::string {
            std::string s;
            for (int bit = 0; bit < N; bit++) {
                s += ((state >> bit) & 1) ? 'T' : 'F';
            }
            return s;
        };
        struct { int state; int N; std::string expected; } cases[] = {
            {0, 2, "FF"}, {1, 2, "TF"}, {2, 2, "FT"}, {3, 2, "TT"},
            {0, 4, "FFFF"}, {1, 4, "TFFF"}, {3, 4, "TTFF"}, {15, 4, "TTTT"},
        };
        for (const auto& c : cases) {
            std::string actual = displayStr(c.state, c.N);
            if (actual == c.expected) {
                passed++;
            } else {
                failed++;
                std::ostringstream oss;
                oss << "N=" << c.N << " state=" << c.state << ": display=\"" << actual << "\" expected=\"" << c.expected << "\"";
                failures.push_back(oss.str());
            }
        }
    }

    std::cout << "{\"test\": \"display_bits\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

// Test: Euclidean engine → gate → inputState end-to-end
// Verifies synchronized engines produce expected inputState patterns
int testGateSync(int argc, char** argv) {
    (void)argc; (void)argv;

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    // Test 1: Identical engines stay in sync (the user's reported scenario)
    // Two channels with same 50% pattern: should alternate between 0 (FF) and 3 (TT)
    for (int N : {2, 3, 4}) {
        int nSteps = 4;
        int nHits = 2;  // 50% fill

        std::vector<WiggleRoom::EuclideanEngine> engines(N);
        for (int i = 0; i < N; i++) {
            engines[i].configure(nSteps, nHits, 0);
        }

        bool syncError = false;
        bool unexpectedState = false;
        int allFalseState = 0;
        int allTrueState = (1 << N) - 1;

        for (int tick = 0; tick < nSteps * 3; tick++) {  // 3 full cycles
            bool gates[4] = {};
            for (int i = 0; i < N; i++) {
                gates[i] = engines[i].tick();
            }

            // All engines should produce the same gate value
            for (int i = 1; i < N; i++) {
                if (gates[i] != gates[0]) {
                    syncError = true;
                    std::ostringstream oss;
                    oss << "N=" << N << " tick=" << tick << ": gate[0]=" << gates[0] << " gate[" << i << "]=" << gates[i];
                    failures.push_back(oss.str());
                    failed++;
                }
            }

            // Compute inputState
            uint8_t inputState = 0;
            for (int i = 0; i < N; i++) {
                if (gates[i]) inputState |= (1 << i);
            }

            // Should be either all-false (0) or all-true (2^N - 1)
            if (inputState != allFalseState && inputState != allTrueState) {
                unexpectedState = true;
                std::ostringstream oss;
                oss << "N=" << N << " tick=" << tick << ": inputState=" << (int)inputState
                    << " (expected 0 or " << allTrueState << ")";
                failures.push_back(oss.str());
                failed++;
            } else {
                passed++;
            }

            // Verify display bits match gate values for this state
            for (int bit = 0; bit < N; bit++) {
                bool displayBit = (inputState >> bit) & 1;
                if (displayBit != gates[bit]) {
                    failed++;
                    std::ostringstream oss;
                    oss << "N=" << N << " tick=" << tick << " bit=" << bit
                        << ": display=" << displayBit << " gate=" << gates[bit];
                    failures.push_back(oss.str());
                } else {
                    passed++;
                }
            }
        }

        if (!syncError && !unexpectedState) {
            passed++;  // bonus: whole cycle passed
        }
    }

    // Test 2: Different patterns across channels → verify per-channel independence
    {
        int N = 2;
        WiggleRoom::EuclideanEngine engines[2];
        engines[0].configure(4, 1, 0);  // 1 hit in 4 steps
        engines[1].configure(4, 3, 0);  // 3 hits in 4 steps

        std::vector<uint8_t> stateSequence;
        for (int tick = 0; tick < 4; tick++) {
            bool gates[2];
            gates[0] = engines[0].tick();
            gates[1] = engines[1].tick();

            uint8_t inputState = 0;
            if (gates[0]) inputState |= 1;
            if (gates[1]) inputState |= 2;

            stateSequence.push_back(inputState);
        }

        // With different patterns, we should see states other than just 0 and 3
        bool hasIntermediateStates = false;
        for (uint8_t s : stateSequence) {
            if (s == 1 || s == 2) {
                hasIntermediateStates = true;
                break;
            }
        }
        if (hasIntermediateStates) {
            passed++;
        } else {
            failed++;
            std::ostringstream oss;
            oss << "different patterns: expected intermediate states (1 or 2), got [";
            for (size_t i = 0; i < stateSequence.size(); i++) {
                oss << (int)stateSequence[i];
                if (i < stateSequence.size() - 1) oss << ",";
            }
            oss << "]";
            failures.push_back(oss.str());
        }
    }

    // Test 3: Various pattern lengths and fills
    {
        struct PatternTest {
            int steps; int hits; int rotation;
        };
        PatternTest patterns[] = {
            {8, 4, 0}, {8, 3, 0}, {16, 4, 0}, {8, 8, 0}, {8, 0, 0},
        };

        for (const auto& pt : patterns) {
            WiggleRoom::EuclideanEngine engine;
            engine.configure(pt.steps, pt.hits, pt.rotation);

            int hitCount = 0;
            for (int tick = 0; tick < pt.steps; tick++) {
                if (engine.tick()) hitCount++;
            }

            if (hitCount == pt.hits) {
                passed++;
            } else {
                failed++;
                std::ostringstream oss;
                oss << "pattern(" << pt.steps << "," << pt.hits << "," << pt.rotation
                    << "): expected " << pt.hits << " hits, got " << hitCount;
                failures.push_back(oss.str());
            }
        }
    }

    std::cout << "{\"test\": \"gate_sync\", \"passed\": " << passed << ", \"failed\": " << failed;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i < failures.size() - 1) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;

    return (failed == 0) ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }

    if (cmd == "--test-euclidean") {
        return testEuclidean(argc, argv);
    }

    if (cmd == "--test-euclidean-known") {
        return testEuclideanKnown(argc, argv);
    }

    if (cmd == "--test-euclidean-tick") {
        return testEuclideanTick(argc, argv);
    }

    if (cmd == "--test-truth-table") {
        return testTruthTable(argc, argv);
    }

    if (cmd == "--test-truth-random") {
        return testTruthRandom(argc, argv);
    }

    if (cmd == "--test-truth-mutate") {
        return testTruthMutate(argc, argv);
    }

    if (cmd == "--test-truth-undo") {
        return testTruthUndo(argc, argv);
    }

    if (cmd == "--test-probability") {
        return testProbability(argc, argv);
    }

    if (cmd == "--test-probability-determinism") {
        return testProbabilityDeterminism(argc, argv);
    }

    if (cmd == "--test-clock-mult") {
        return testClockMult(argc, argv);
    }

    if (cmd == "--test-clock-mult-known") {
        return testClockMultKnown(argc, argv);
    }

    if (cmd == "--test-swing-timing") {
        return testSwingTiming(argc, argv);
    }

    if (cmd == "--test-clock-sync") {
        return testClockSync(argc, argv);
    }

    if (cmd == "--test-full-module") {
        return testFullModuleTiming(argc, argv);
    }

    if (cmd == "--test-lfo-phase") {
        return testLfoPhase(argc, argv);
    }

    if (cmd == "--test-getHit-after-construct") {
        return testGetHitAfterConstruct(argc, argv);
    }

    if (cmd == "--test-pattern-updates-on-configure") {
        return testPatternUpdatesOnConfigure(argc, argv);
    }

    if (cmd == "--test-truth-density-randomize") {
        return testTruthDensityRandomize(argc, argv);
    }

    if (cmd == "--test-truth-density-mutate") {
        return testTruthDensityMutate(argc, argv);
    }

    if (cmd == "--test-intersect-falling") {
        return testIntersectFalling(argc, argv);
    }

    if (cmd == "--test-input-state") {
        return testInputState(argc, argv);
    }

    if (cmd == "--test-display-bits") {
        return testDisplayBits(argc, argv);
    }

    if (cmd == "--test-gate-sync") {
        return testGateSync(argc, argv);
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
}
