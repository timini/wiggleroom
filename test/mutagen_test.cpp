/**
 * Mutagen Test Executable
 *
 * Standalone tests for the Mutagen sequencer core:
 *  - StepAddresser: clock advance, CV addressing, broadcast follow
 *  - StepSequence:  randomise, mutation rate, undo/redo
 *  - BankStore:     save and recall round trip
 *
 * These cover the logic that in the EucSeq family lived inside Rack-only
 * Module subclasses and therefore went untested, which is how the expander
 * off-by-one and the reset-phase bug survived so long.
 *
 * Usage:
 *   ./mutagen_test --test-address-cv
 *   ./mutagen_test --test-mutation-rate --rate=0.25 --ticks=10000 --seed=42
 */

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "mutagen/BankStore.hpp"
#include "mutagen/MutagenMessage.hpp"
#include "mutagen/StepAddresser.hpp"
#include "mutagen/StepSequence.hpp"

using namespace WiggleRoom;

// ---------------------------------------------------------------- helpers

static int argInt(int argc, char** argv, const char* name, int fallback) {
    std::string prefix = std::string(name) + "=";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind(prefix, 0) == 0) return std::stoi(a.substr(prefix.size()));
    }
    return fallback;
}

static float argFloat(int argc, char** argv, const char* name, float fallback) {
    std::string prefix = std::string(name) + "=";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind(prefix, 0) == 0) return std::stof(a.substr(prefix.size()));
    }
    return fallback;
}

static int report(const char* name, int passed, int failed,
                  const std::vector<std::string>& failures,
                  const std::string& extra = "") {
    std::cout << "{\"test\": \"" << name << "\", \"passed\": " << passed
              << ", \"failed\": " << failed;
    if (!extra.empty()) std::cout << ", " << extra;
    if (!failures.empty()) {
        std::cout << ", \"failures\": [";
        for (size_t i = 0; i < failures.size(); i++) {
            std::cout << "\"" << failures[i] << "\"";
            if (i + 1 < failures.size()) std::cout << ", ";
        }
        std::cout << "]";
    }
    std::cout << "}" << std::endl;
    return failed == 0 ? 0 : 1;
}

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (cond) { passed++; }                            \
        else { failed++; failures.push_back(msg); }        \
    } while (0)

// ------------------------------------------------- addressing: free-running

static int testAddressAdvance(int argc, char** argv) {
    int steps = argInt(argc, argv, "--steps", 8);
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepAddresser a;
    CHECK(!a.hasPlayed(), "fresh addresser reports having played");
    CHECK(a.current(steps) == 0, "fresh addresser does not sit on step 0");

    // First tick plays step 0, not step 1.
    CHECK(a.tick(steps, false, 0.f) == 0, "first tick did not land on step 0");
    for (int i = 1; i < steps; i++) {
        int got = a.tick(steps, false, 0.f);
        if (got != i) {
            failed++;
            failures.push_back("tick " + std::to_string(i) + " gave " + std::to_string(got));
        } else {
            passed++;
        }
    }
    CHECK(a.tick(steps, false, 0.f) == 0, "did not wrap to 0 after the last step");

    a.reset();
    CHECK(!a.hasPlayed(), "reset did not clear the played flag");
    CHECK(a.current(steps) == 0, "reset did not return to step 0");

    return report("address_advance", passed, failed, failures);
}

// ------------------------------------------------------ addressing: CV mode

static int testAddressCv(int argc, char** argv) {
    int steps = argInt(argc, argv, "--steps", 8);
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepAddresser a;

    CHECK(a.tick(steps, true, 0.f) == 0, "0V did not select step 0");
    CHECK(a.tick(steps, true, 1.f) == steps - 1, "full scale did not select the last step");

    // Every step must be reachable, and the mapping must be monotonic.
    int prev = -1;
    bool monotonic = true, allReachable = true;
    std::vector<bool> seen(steps, false);
    for (int i = 0; i <= 1000; i++) {
        float cv = static_cast<float>(i) / 1000.f;
        int got = a.tick(steps, true, cv);
        if (got < prev) monotonic = false;
        if (got < 0 || got >= steps) allReachable = false;
        else seen[got] = true;
        prev = got;
    }
    for (int s = 0; s < steps; s++)
        if (!seen[s]) allReachable = false;
    CHECK(monotonic, "CV to step mapping is not monotonic");
    CHECK(allReachable, "some steps are unreachable by CV, or index went out of range");

    // The address is latched: without a tick nothing moves.
    a.tick(steps, true, 0.f);
    int held = a.current(steps);
    CHECK(held == 0, "latched position was not step 0");
    CHECK(a.current(steps) == held, "position moved without a tick");

    return report("address_cv", passed, failed, failures);
}

// ------------------------------------------------------- broadcast follow

static int testBroadcastFollow(int argc, char** argv) {
    int masterSteps = argInt(argc, argv, "--master-steps", 8);
    int slaveSteps = argInt(argc, argv, "--slave-steps", 5);
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepAddresser master, slave;

    // Equal lengths must stay in lockstep.
    StepAddresser twin;
    for (int i = 0; i < 32; i++) {
        int m = master.tick(masterSteps, false, 0.f);
        twin.followBroadcast(m, masterSteps);
        if (twin.current(masterSteps) != m) {
            failed++;
            failures.push_back("equal lengths diverged at tick " + std::to_string(i));
            break;
        }
    }
    passed++;

    // A shorter slave wraps into its own length rather than reading out of range.
    master.reset();
    bool inRange = true;
    for (int i = 0; i < 64; i++) {
        int m = master.tick(masterSteps, false, 0.f);
        slave.followBroadcast(m, slaveSteps);
        int s = slave.current(slaveSteps);
        if (s < 0 || s >= slaveSteps) { inRange = false; break; }
        if (s != m % slaveSteps) { inRange = false; break; }
    }
    CHECK(inRange, "slave position left its own range or did not wrap as expected");

    return report("broadcast_follow", passed, failed, failures);
}

// --------------------------------------------------------- mutation rate

static int testMutationRate(int argc, char** argv) {
    float rate = argFloat(argc, argv, "--rate", 0.25f);
    int ticks = argInt(argc, argv, "--ticks", 10000);
    int steps = argInt(argc, argv, "--steps", 8);
    uint32_t seed = static_cast<uint32_t>(argInt(argc, argv, "--seed", 42));

    StepSequence seq;
    seq.setSeed(seed);
    seq.setSteps(steps);
    seq.randomize();

    int mutations = 0;
    for (int i = 0; i < ticks; i++)
        if (seq.mutateOnce(rate) >= 0) mutations++;

    double observed = static_cast<double>(mutations) / static_cast<double>(ticks);

    std::ostringstream extra;
    extra << "\"rate\": " << rate
          << ", \"ticks\": " << ticks
          << ", \"mutations\": " << mutations
          << ", \"observed\": " << observed;

    // The caller asserts the tolerance; this command just reports.
    return report("mutation_rate", 1, 0, {}, extra.str());
}

static int testMutationBounds(int argc, char** argv) {
    int steps = argInt(argc, argv, "--steps", 8);
    uint32_t seed = static_cast<uint32_t>(argInt(argc, argv, "--seed", 7));
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepSequence seq;
    seq.setSeed(seed);
    seq.setSteps(steps);
    seq.randomize();

    // Rate 0 must never fire.
    bool anyAtZero = false;
    for (int i = 0; i < 5000; i++)
        if (seq.mutateOnce(0.f) >= 0) anyAtZero = true;
    CHECK(!anyAtZero, "mutation fired at rate 0");

    // Rate 1 must fire on every tick, and only ever inside the active range.
    bool everyTick = true, inRange = true;
    for (int i = 0; i < 5000; i++) {
        int t = seq.mutateOnce(1.f);
        if (t < 0) everyTick = false;
        if (t >= steps) inRange = false;
    }
    CHECK(everyTick, "mutation did not fire on every tick at rate 1");
    CHECK(inRange, "mutation touched a step outside the active length");

    // Values must stay normalised.
    bool normalised = true;
    for (int s = 0; s < steps; s++)
        if (seq.values[s] < 0.f || seq.values[s] > 1.f) normalised = false;
    CHECK(normalised, "a value left the 0 to 1 range");

    return report("mutation_bounds", passed, failed, failures);
}

// -------------------------------------------------------- length changes

static int testLengthChange(int argc, char** argv) {
    (void)argc; (void)argv;
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepSequence seq;
    seq.setSeed(11);
    seq.setSteps(16);
    seq.randomize();

    float longTail = seq.values[15];

    // Shrinking must never read outside the active range.
    seq.setSteps(4);
    bool inRange = true;
    for (int i = 0; i < 64; i++) {
        float v = seq.valueAt(i);
        bool found = false;
        for (int s = 0; s < 4; s++)
            if (std::fabs(seq.values[s] - v) < 1e-9f) found = true;
        if (!found) inRange = false;
    }
    CHECK(inRange, "reading a shrunken sequence returned a value outside the active steps");

    // Growing back must not have wiped the steps beyond the short length.
    seq.setSteps(16);
    CHECK(std::fabs(seq.values[15] - longTail) < 1e-9f,
          "shrinking then growing destroyed values beyond the short length");

    CHECK(seq.steps == 16, "step count did not restore");
    seq.setSteps(999);
    CHECK(seq.steps == StepSequence::MAX_STEPS, "step count was not clamped to MAX_STEPS");
    seq.setSteps(-5);
    CHECK(seq.steps == StepSequence::MIN_STEPS, "step count was not clamped to MIN_STEPS");

    return report("length_change", passed, failed, failures);
}

// ------------------------------------------------------- bank round trip

static int testBankRoundTrip(int argc, char** argv) {
    int bank = argInt(argc, argv, "--bank", 3);
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepSequence seq;
    seq.setSeed(99);
    seq.setSteps(8);
    seq.randomize();

    BankStore store;
    CHECK(!store.occupied(bank), "a fresh slot reports occupied");

    float saved[StepSequence::MAX_STEPS];
    std::copy(seq.values, seq.values + StepSequence::MAX_STEPS, saved);
    store.save(bank, seq);
    CHECK(store.occupied(bank), "slot not marked occupied after save");

    // Churn the live sequence hard.
    for (int i = 0; i < 500; i++) seq.mutateOnce(1.f);
    seq.randomize();
    bool changed = false;
    for (int s = 0; s < 8; s++)
        if (std::fabs(seq.values[s] - saved[s]) > 1e-9f) changed = true;
    CHECK(changed, "mutation and randomise left the sequence unchanged, test is not exercising recall");

    CHECK(store.load(bank, seq), "load returned false for an occupied slot");
    bool restored = true;
    for (int s = 0; s < 8; s++)
        if (std::fabs(seq.values[s] - saved[s]) > 1e-9f) restored = false;
    CHECK(restored, "recall did not restore the saved values exactly");
    CHECK(seq.steps == 8, "recall did not restore the step count");

    // An empty slot must leave the lane alone rather than blanking it.
    int empty = (bank + 1) % BankStore::NUM_BANKS;
    float before = seq.values[0];
    CHECK(!store.load(empty, seq), "load returned true for an empty slot");
    CHECK(std::fabs(seq.values[0] - before) < 1e-9f, "loading an empty slot altered the lane");

    // Out of range must be refused, not crash.
    CHECK(!store.load(-1, seq), "load accepted a negative bank index");
    CHECK(!store.load(BankStore::NUM_BANKS, seq), "load accepted an out of range bank index");

    return report("bank_round_trip", passed, failed, failures);
}

// ------------------------------------------------------------ chain relay

static int testChainRelay(int argc, char** argv) {
    int hops = argInt(argc, argv, "--hops", 3);
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    MutagenExpanderMessage master;
    master.clear();
    master.clockTick = true;
    master.running = true;
    master.stepAddress = 5;
    master.bankIndex = 7;
    master.saveRequest = true;
    master.valid = true;

    // Each slave copies the message through unchanged.
    MutagenExpanderMessage hop = master;
    bool intact = true;
    for (int i = 0; i < hops; i++) {
        MutagenExpanderMessage next = hop;
        if (next.stepAddress != master.stepAddress) intact = false;
        if (next.bankIndex != master.bankIndex) intact = false;
        if (next.clockTick != master.clockTick) intact = false;
        if (next.saveRequest != master.saveRequest) intact = false;
        if (!next.valid) intact = false;
        hop = next;
    }
    CHECK(intact, "the message changed as it travelled down the chain");

    MutagenExpanderMessage fresh;
    CHECK(!fresh.valid, "a default constructed message is already valid");
    fresh.valid = true;
    fresh.clear();
    CHECK(!fresh.valid, "clear() did not invalidate the message");
    CHECK(fresh.stepAddress == -1, "clear() did not reset stepAddress to -1");
    CHECK(fresh.bankIndex == -1, "clear() did not reset bankIndex to -1");
    CHECK(fresh.running, "clear() did not default running to true");

    return report("chain_relay", passed, failed, failures);
}

// -------------------------------------------------------------- undo/redo

static int testUndoRedo(int argc, char** argv) {
    (void)argc; (void)argv;
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    StepSequence seq;
    seq.setSeed(5);
    seq.setSteps(8);
    for (int s = 0; s < 8; s++) seq.values[s] = 0.f;

    seq.setValue(2, 0.75f);
    CHECK(std::fabs(seq.values[2] - 0.75f) < 1e-6f, "setValue did not take");
    CHECK(seq.undo(), "undo returned false after an edit");
    CHECK(std::fabs(seq.values[2]) < 1e-6f, "undo did not restore the old value");
    CHECK(seq.redo(), "redo returned false after an undo");
    CHECK(std::fabs(seq.values[2] - 0.75f) < 1e-6f, "redo did not reapply the edit");

    // Mutation must not flood the undo stack: it fires on every clock step.
    seq.clearHistory();
    for (int i = 0; i < 100; i++) seq.mutateOnce(1.f);
    CHECK(!seq.undo(), "mutation pushed onto the undo stack");

    return report("undo_redo", passed, failed, failures);
}

// ------------------------------------------------------------------ main

static void printUsage() {
    std::cout
        << "Mutagen test tool\n\n"
        << "Commands:\n"
        << "  --test-address-advance     Clock advance and wrap\n"
        << "  --test-address-cv          CV addressing and latching\n"
        << "  --test-broadcast-follow    Slave follows the chain address\n"
        << "  --test-mutation-rate       Observed mutation rate (reports)\n"
        << "  --test-mutation-bounds     Rate 0 and 1, range safety\n"
        << "  --test-length-change       Shrinking and growing the pattern\n"
        << "  --test-bank-round-trip     Save, churn, recall\n"
        << "  --test-chain-relay         Message survives the chain\n"
        << "  --test-undo-redo           Undo covers edits, not mutation\n";
}

static const char* ALL_COMMANDS[] = {
    "--test-address-advance", "--test-address-cv", "--test-broadcast-follow",
    "--test-mutation-rate", "--test-mutation-bounds", "--test-length-change",
    "--test-bank-round-trip", "--test-chain-relay", "--test-undo-redo",
};

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") { printUsage(); return 0; }

    if (cmd == "--list-commands") {
        for (const char* c : ALL_COMMANDS) std::cout << c << "\n";
        return 0;
    }

    if (cmd == "--test-address-advance") return testAddressAdvance(argc, argv);
    if (cmd == "--test-address-cv")       return testAddressCv(argc, argv);
    if (cmd == "--test-broadcast-follow") return testBroadcastFollow(argc, argv);
    if (cmd == "--test-mutation-rate")    return testMutationRate(argc, argv);
    if (cmd == "--test-mutation-bounds")  return testMutationBounds(argc, argv);
    if (cmd == "--test-length-change")    return testLengthChange(argc, argv);
    if (cmd == "--test-bank-round-trip")  return testBankRoundTrip(argc, argv);
    if (cmd == "--test-chain-relay")      return testChainRelay(argc, argv);
    if (cmd == "--test-undo-redo")        return testUndoRedo(argc, argv);

    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
}
