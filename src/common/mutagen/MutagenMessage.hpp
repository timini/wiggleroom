#pragma once
/******************************************************************************
 * MUTAGEN EXPANDER MESSAGE
 * Transport and addressing passed rightward along a Mutagen chain.
 *
 * No VCV Rack dependencies, so Intersect can produce one without pulling in
 * any Mutagen module code.
 *
 * Producers:
 *   Intersect -> Mutagen    transport only; stepAddress and bankIndex are -1
 *   Mutagen   -> MutagenX   transport, resolved address, bank, save request
 *   MutagenX  -> MutagenX   relayed unchanged, so chains of any length work
 *
 * `valid` is the load-bearing field. Every consumer in this plugin checks
 * both that the neighbour exists and that the message is valid, and every
 * producer clears before populating so `valid` is never true over a
 * half-written struct.
 ******************************************************************************/

namespace WiggleRoom {

struct MutagenExpanderMessage {
    // True for the single sample on which the chain should advance a step.
    bool clockTick;
    bool resetPulse;
    bool running;

    // The step the master resolved this tick. -1 means "no address on the
    // bus, advance your own counter", which is what Intersect sends.
    int stepAddress;

    // Currently selected slot, or -1 when the bus carries no bank info.
    int bankIndex;

    // True for the single sample on which every unit should save its own
    // values into bankIndex.
    bool saveRequest;

    bool valid;

    MutagenExpanderMessage() { clear(); }

    void clear() {
        clockTick = false;
        resetPulse = false;
        running = true;
        stepAddress = -1;
        bankIndex = -1;
        saveRequest = false;
        valid = false;
    }
};

} // namespace WiggleRoom
