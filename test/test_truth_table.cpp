/******************************************************************************
 * Unit tests for TruthTable undo/redo functionality
 * Tests both TruthTable (4-channel) from TruthTable.hpp
 *
 * Build: g++ -std=c++17 -I../src/modules/Euclogic -o test_truth_table test_truth_table.cpp
 * Run:   ./test_truth_table
 ******************************************************************************/

#include <cassert>
#include <iostream>
#include "../src/modules/Euclogic/TruthTable.hpp"

void test_undo_after_mutate() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto original = tt.serialize();

    tt.mutate();  // pushUndo + modify
    auto mutated = tt.serialize();
    assert(original != mutated);

    bool result = tt.undo();
    assert(result);
    assert(tt.serialize() == original);
    std::cout << "PASS: test_undo_after_mutate\n";
}

void test_undo_multiple() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto state0 = tt.serialize();

    tt.mutate();
    auto state1 = tt.serialize();

    tt.mutate();
    // auto state2 = tt.serialize();

    bool r1 = tt.undo();
    assert(r1);
    assert(tt.serialize() == state1);

    bool r2 = tt.undo();
    assert(r2);
    assert(tt.serialize() == state0);
    std::cout << "PASS: test_undo_multiple\n";
}

void test_undo_empty_noop() {
    WiggleRoom::TruthTable tt;
    auto original = tt.serialize();
    bool result = tt.undo();
    assert(!result);
    assert(tt.serialize() == original);
    std::cout << "PASS: test_undo_empty_noop\n";
}

void test_redo_basic() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto original = tt.serialize();

    tt.mutate();
    auto mutated = tt.serialize();

    tt.undo();
    assert(tt.serialize() == original);

    bool result = tt.redo();
    assert(result);
    assert(tt.serialize() == mutated);
    std::cout << "PASS: test_redo_basic\n";
}

void test_redo_multiple() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto state0 = tt.serialize();

    tt.mutate();
    auto state1 = tt.serialize();

    tt.mutate();
    auto state2 = tt.serialize();

    // Undo twice
    tt.undo();
    tt.undo();
    assert(tt.serialize() == state0);

    // Redo twice
    tt.redo();
    assert(tt.serialize() == state1);

    tt.redo();
    assert(tt.serialize() == state2);
    std::cout << "PASS: test_redo_multiple\n";
}

void test_redo_cleared_on_new_action() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);

    tt.mutate();
    tt.undo();

    tt.randomize();  // This should clear redo stack
    bool result = tt.redo();
    assert(!result);
    std::cout << "PASS: test_redo_cleared_on_new_action\n";
}

void test_redo_empty_noop() {
    WiggleRoom::TruthTable tt;
    auto original = tt.serialize();
    bool result = tt.redo();
    assert(!result);
    assert(tt.serialize() == original);
    std::cout << "PASS: test_redo_empty_noop\n";
}

void test_undo_after_randomize() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto original = tt.serialize();

    tt.randomize();
    assert(tt.serialize() != original);

    tt.undo();
    assert(tt.serialize() == original);
    std::cout << "PASS: test_undo_after_randomize\n";
}

void test_undo_after_toggle() {
    WiggleRoom::TruthTable tt;
    auto original = tt.serialize();

    tt.pushUndo();  // Manual push before toggle (as done in UI code)
    tt.toggleBit(0, 0);
    assert(tt.serialize() != original);

    tt.undo();
    assert(tt.serialize() == original);
    std::cout << "PASS: test_undo_after_toggle\n";
}

void test_undo_redo_interleaved() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);
    auto state0 = tt.serialize();

    tt.mutate();
    auto state1 = tt.serialize();

    tt.mutate();
    auto state2 = tt.serialize();

    // Undo to state1
    tt.undo();
    assert(tt.serialize() == state1);

    // New action from state1 - should clear redo
    tt.mutate();
    auto state3 = tt.serialize();

    // Can't redo to state2 anymore
    bool result = tt.redo();
    assert(!result);
    assert(tt.serialize() == state3);

    // But can still undo back through state1 to state0
    tt.undo();  // state3 -> state1
    assert(tt.serialize() == state1);

    tt.undo();  // state1 -> state0
    assert(tt.serialize() == state0);

    std::cout << "PASS: test_undo_redo_interleaved\n";
}

void test_undo_after_load_preset() {
    WiggleRoom::TruthTable tt;
    auto original = tt.serialize();

    tt.loadPreset("XOR");
    auto xorState = tt.serialize();
    assert(original != xorState);

    tt.undo();
    assert(tt.serialize() == original);
    std::cout << "PASS: test_undo_after_load_preset\n";
}

void test_toggle_clears_redo() {
    WiggleRoom::TruthTable tt;
    tt.setSeed(42);

    tt.mutate();
    tt.undo();

    // Manual toggle (as in UI) should clear redo
    tt.pushUndo();
    tt.toggleBit(0, 0);

    bool result = tt.redo();
    assert(!result);
    std::cout << "PASS: test_toggle_clears_redo\n";
}

int main() {
    test_undo_after_mutate();
    test_undo_multiple();
    test_undo_empty_noop();
    test_redo_basic();
    test_redo_multiple();
    test_redo_cleared_on_new_action();
    test_redo_empty_noop();
    test_undo_after_randomize();
    test_undo_after_toggle();
    test_undo_redo_interleaved();
    test_undo_after_load_preset();
    test_toggle_clears_redo();
    std::cout << "\nAll TruthTable tests passed!\n";
    return 0;
}
