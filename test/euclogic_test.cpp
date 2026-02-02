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
#include "modules/Euclogic/EuclideanEngine.hpp"
#include "modules/Euclogic/TruthTable.hpp"
#include "modules/Euclogic/ProbabilityGate.hpp"

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

    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
}
