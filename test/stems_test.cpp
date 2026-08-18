/******************************************************************************
 * Stems Test Executable
 *
 * Standalone tests for the framework-free Stems core in src/common/stems/.
 * Includes those headers directly: they contain no rack.hpp, so no stubbing is
 * required and no Rack SDK linkage is needed.
 *
 * Deliberately does NOT duplicate module logic into this file. octolfo_test.cpp
 * does that and has already drifted from OctoLFO.cpp.
 *
 * Output: one JSON object per line on stdout. Exit code is non-zero on failure.
 *
 * Commands:
 *   --self-test                 Run every check and report a summary
 *   --test-fft-roundtrip        inverse(forward(x)) == x
 *   --test-fft-impulse          Impulse has flat unit magnitude across all bins
 *   --test-fft-sine             A bin-centred sine lands in exactly that bin
 *   --test-fft-sizes            Round-trip holds across supported sizes
 *   --test-buffer-roundtrip     Written samples read back bit-exact
 *   --test-buffer-wraparound    Writing past capacity overwrites oldest first
 *   --test-buffer-no-alloc      No allocation after construction
 *   --test-buffer-interpolate   Fractional reads interpolate correctly
 *   --test-buffer-capacity      Capacity honours the duration cap
 *   --test-buffer-non-finite    inf/NaN positions give silence, not NaN audio
 *   --test-buffer-clear-cheap   clear() is O(1) and does not touch storage
 *   --test-transport-lock       Phase stays locked to the clock over 1000 bars
 *   --test-transport-reset      Reset returns to the downbeat within one sample
 *   --test-transport-division   Clock division and multiplication scale the rate
 *   --test-transport-loop       Loop start and length map the playhead correctly
 *   --test-transport-no-clock   Free-runs safely with no clock present
 ******************************************************************************/

#include "common/stems/FftBackend.hpp"
#include "common/stems/ReferenceFft.hpp"
#include "common/stems/RingBuffer.hpp"
#include "common/stems/Diffusion.hpp"
#include "common/stems/GrainEngine.hpp"
#include "common/stems/Hpss.hpp"
#include "common/stems/SeparationWorker.hpp"
#include "common/stems/StemMixer.hpp"
#include "common/stems/WavetableExtract.hpp"
#include "common/LowpassGate.hpp"
#include "common/stems/WavetableOsc.hpp"
#include "common/stems/Quantizer.hpp"
#include "common/stems/ScaleDetect.hpp"
#include "common/stems/Yin.hpp"
#include "common/stems/Stft.hpp"
#include "common/stems/Transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <atomic>
#include <chrono>
#include <random>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

using WiggleRoom::stems::ReferenceFft;

namespace {

constexpr double kPi = 3.14159265358979323846;

int parseIntArg(int argc, char** argv, const char* prefix, int defaultVal) {
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], prefix, std::strlen(prefix)) == 0) {
            return std::stoi(argv[i] + std::strlen(prefix));
        }
    }
    return defaultVal;
}

void emit(const std::string& test, bool pass, const std::string& extra = "") {
    std::cout << "{\"test\": \"" << test << "\""
              << ", \"passed\": " << (pass ? 1 : 0)
              << ", \"failed\": " << (pass ? 0 : 1);
    if (!extra.empty()) std::cout << ", " << extra;
    std::cout << "}" << std::endl;
}

std::string num(const char* key, double value) {
    std::ostringstream oss;
    oss << "\"" << key << "\": " << std::setprecision(10) << value;
    return oss.str();
}

// ---------------------------------------------------------------------------

/** inverse(forward(x)) must reproduce x. */
bool fftRoundtrip(std::size_t n, double& maxErrOut) {
    ReferenceFft fft(n);
    std::vector<float> input(n), output(n);
    std::vector<float> spectrum(fft.spectrumLength());

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (auto& v : input) v = dist(rng);

    fft.forward(input.data(), spectrum.data());
    fft.inverse(spectrum.data(), output.data());

    double maxErr = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        maxErr = std::max(maxErr, std::abs(static_cast<double>(output[i] - input[i])));
    }
    maxErrOut = maxErr;
    return maxErr < 1e-5;
}

/** A unit impulse has magnitude 1 in every bin. */
bool fftImpulse(std::size_t n, double& maxErrOut) {
    ReferenceFft fft(n);
    std::vector<float> input(n, 0.f);
    input[0] = 1.f;
    std::vector<float> spectrum(fft.spectrumLength());
    fft.forward(input.data(), spectrum.data());

    double maxErr = 0.0;
    for (std::size_t bin = 0; bin < fft.numBins(); bin++) {
        const double re = spectrum[2 * bin];
        const double im = spectrum[2 * bin + 1];
        maxErr = std::max(maxErr, std::abs(std::sqrt(re * re + im * im) - 1.0));
    }
    maxErrOut = maxErr;
    return maxErr < 1e-5;
}

/** A sine at exactly bin k puts all its energy in bin k. */
bool fftSine(std::size_t n, std::size_t bin, double& leakageOut) {
    ReferenceFft fft(n);
    std::vector<float> input(n);
    for (std::size_t i = 0; i < n; i++) {
        input[i] = static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(bin) *
                                               static_cast<double>(i) / static_cast<double>(n)));
    }
    std::vector<float> spectrum(fft.spectrumLength());
    fft.forward(input.data(), spectrum.data());

    double target = 0.0, leakage = 0.0;
    for (std::size_t b = 0; b < fft.numBins(); b++) {
        const double re = spectrum[2 * b];
        const double im = spectrum[2 * b + 1];
        const double mag = std::sqrt(re * re + im * im);
        if (b == bin) target = mag;
        else leakage = std::max(leakage, mag);
    }
    leakageOut = (target > 0.0) ? (leakage / target) : 1.0;
    return leakageOut < 1e-6;
}

// ---------------------------------------------------------------------------
// RingBuffer
// ---------------------------------------------------------------------------

using WiggleRoom::stems::RingBuffer;

/** Every written frame must read back bit-exact. */
bool bufferRoundtrip(std::string& detail) {
    RingBuffer buf(48000, /*maxSeconds=*/1.0f, /*channels=*/2);
    const std::size_t n = 1000;
    for (std::size_t i = 0; i < n; i++) {
        buf.write(static_cast<float>(i) * 0.001f, static_cast<float>(i) * -0.001f);
    }
    if (buf.framesWritten() != n) { detail = "framesWritten mismatch"; return false; }

    for (std::size_t i = 0; i < n; i++) {
        float l = 0.f, r = 0.f;
        buf.readFrame(i, l, r);
        if (l != static_cast<float>(i) * 0.001f || r != static_cast<float>(i) * -0.001f) {
            detail = "sample " + std::to_string(i) + " differs";
            return false;
        }
    }
    return true;
}

/** Writing past capacity must overwrite the oldest frames, not corrupt or grow. */
bool bufferWraparound(std::string& detail) {
    RingBuffer buf(1000, /*maxSeconds=*/1.0f, /*channels=*/1);  // capacity 1000
    const std::size_t cap = buf.capacityFrames();
    // Write 1.5x capacity. The last `cap` values must survive.
    const std::size_t total = cap + cap / 2;
    for (std::size_t i = 0; i < total; i++) buf.write(static_cast<float>(i), 0.f);

    if (buf.framesStored() != cap) { detail = "framesStored should saturate at capacity"; return false; }

    // readFrame(0) is the oldest surviving frame.
    for (std::size_t i = 0; i < cap; i++) {
        float l = 0.f, r = 0.f;
        buf.readFrame(i, l, r);
        const float expected = static_cast<float>(total - cap + i);
        if (l != expected) {
            detail = "wrapped frame " + std::to_string(i) + " expected " +
                     std::to_string(expected) + " got " + std::to_string(l);
            return false;
        }
    }
    return true;
}

/** Capacity must be fixed at construction and never change. */
bool bufferNoAlloc(std::string& detail) {
    RingBuffer buf(48000, 2.0f, 2);
    const std::size_t capBefore = buf.capacityFrames();
    const void* dataBefore = buf.rawData();

    for (std::size_t i = 0; i < capBefore * 3; i++) buf.write(0.5f, -0.5f);
    buf.clear();
    for (std::size_t i = 0; i < capBefore; i++) buf.write(0.25f, -0.25f);

    if (buf.capacityFrames() != capBefore) { detail = "capacity changed"; return false; }
    if (buf.rawData() != dataBefore) { detail = "storage was reallocated"; return false; }
    return true;
}

/** Fractional reads must interpolate linearly between neighbouring frames. */
bool bufferInterpolate(std::string& detail) {
    RingBuffer buf(48000, 1.0f, 1);
    for (std::size_t i = 0; i < 100; i++) buf.write(static_cast<float>(i), 0.f);

    struct Case { double pos; float expect; };
    const Case cases[] = {{0.0, 0.f}, {0.5, 0.5f}, {10.25, 10.25f}, {98.75, 98.75f}};
    for (const auto& c : cases) {
        float l = 0.f, r = 0.f;
        buf.readFrameInterpolated(c.pos, l, r);
        if (std::abs(l - c.expect) > 1e-4f) {
            detail = "pos " + std::to_string(c.pos) + " expected " +
                     std::to_string(c.expect) + " got " + std::to_string(l);
            return false;
        }
    }
    return true;
}

/** Capacity must follow sampleRate * maxSeconds, which is how the spec's
    32 second cap is enforced. */
bool bufferCapacity(std::string& detail) {
    struct Case { int rate; float seconds; };
    const Case cases[] = {{44100, 32.f}, {48000, 32.f}, {96000, 32.f}, {48000, 4.f}};
    for (const auto& c : cases) {
        RingBuffer buf(c.rate, c.seconds, 2);
        const std::size_t expected = static_cast<std::size_t>(c.rate * c.seconds);
        if (buf.capacityFrames() != expected) {
            detail = "rate " + std::to_string(c.rate) + " expected " +
                     std::to_string(expected) + " got " + std::to_string(buf.capacityFrames());
            return false;
        }
    }
    return true;
}

/**
 * Non-finite positions must yield silence, not undefined behaviour.
 *
 * static_cast<size_t> of inf or NaN is UB, and NaN slips past a plain
 * `position < 0.0` guard because every comparison with NaN is false. Left
 * unchecked this emits NaN audio, which then propagates through the whole
 * graph.
 */
bool bufferNonFinite(std::string& detail) {
    RingBuffer buf(48000, 1.0f, 2);
    for (std::size_t i = 0; i < 100; i++) buf.write(1.f, 1.f);

    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double cases[] = {inf, -inf, nan, -1.0, 1e30, 100.0, 1000.0};

    for (double pos : cases) {
        float l = -1.f, r = -1.f;
        buf.readFrameInterpolated(pos, l, r);
        if (std::isnan(l) || std::isnan(r)) {
            detail = "position produced NaN audio";
            return false;
        }
        if (!(l == 0.f && r == 0.f)) {
            detail = "out-of-range position did not give silence, got " +
                     std::to_string(l) + "," + std::to_string(r);
            return false;
        }
    }
    return true;
}

/**
 * clear() must not touch the storage.
 *
 * Filling the whole allocation is up to 24.6 MB at 96 kHz stereo over the
 * 32 second cap. Doing that synchronously when a new take starts would stall
 * the audio thread exactly when recording begins. Resetting the counters
 * already makes every old sample unreachable, so the fill buys nothing.
 *
 * Verified by observing that old samples remain physically present in storage
 * while being unreachable through the public API.
 */
bool bufferClearIsCheap(std::string& detail) {
    RingBuffer buf(48000, 1.0f, 1);
    for (std::size_t i = 0; i < 500; i++) buf.write(0.75f, 0.f);

    const float sentinel = buf.rawData()[0];
    if (sentinel != 0.75f) { detail = "setup failed, storage not written"; return false; }

    buf.clear();

    if (buf.framesStored() != 0)  { detail = "framesStored not reset"; return false; }
    if (buf.framesWritten() != 0) { detail = "framesWritten not reset"; return false; }

    float l = -1.f, r = -1.f;
    buf.readFrame(0, l, r);
    if (l != 0.f || r != 0.f) { detail = "cleared buffer did not read as silence"; return false; }

    if (buf.rawData()[0] != sentinel) {
        detail = "clear() wrote to storage; it should only reset counters";
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Transport;

namespace {
/** Drive a transport with a synthetic clock. Returns worst downbeat drift in frames. */
double runClock(Transport& t, int sampleRate, double bpm, int bars,
                int clocksPerLoop, double* driftAccum = nullptr) {
    const double beatsPerBar = 4.0;
    const double samplesPerBeat = 60.0 / bpm * sampleRate;
    const int totalBeats = static_cast<int>(bars * beatsPerBar);

    double worstDrift = 0.0;
    int sampleIndex = 0;
    for (int beat = 0; beat < totalBeats; beat++) {
        const int nextEdge = static_cast<int>((beat + 1) * samplesPerBeat);
        // 1ms high pulse at the start of the beat, then low
        const int pulseSamples = std::max(1, sampleRate / 1000);
        for (int i = sampleIndex; i < nextEdge; i++) {
            const bool high = (i - sampleIndex) < pulseSamples;
            t.process(high ? 10.f : 0.f, 0.f);

            // On each loop downbeat the phase must be at 0.
            // Scale the normalised phase error by frames-per-loop so the
            // threshold is genuinely in audio frames. Multiplying only by
            // clocksPerLoop gives clock intervals, which at 120 BPM / 48 kHz
            // would let ~24000 frames of drift pass a "1 frame" assertion.
            if (t.downbeat()) {
                const double framesPerLoop =
                    t.clockPeriodSeconds() * clocksPerLoop * sampleRate;
                const double drift =
                    std::min(t.phase(), 1.0 - t.phase()) * framesPerLoop;
                worstDrift = std::max(worstDrift, drift);
                if (driftAccum) *driftAccum += drift;
            }
        }
        sampleIndex = nextEdge;
    }
    return worstDrift;
}
}  // namespace

/** Phase must stay locked to the incoming clock over a long run. */
bool transportLock(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate * 8);

    const double worstDrift = runClock(t, sampleRate, 120.0, 1000, 16);
    if (worstDrift > 1.0) {
        detail = "worst downbeat drift " + std::to_string(worstDrift) + " frames";
        return false;
    }
    if (!t.clockDetected()) { detail = "clock not detected"; return false; }
    return true;
}

/** Reset must return the playhead to the downbeat immediately. */
bool transportReset(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate * 4);

    runClock(t, sampleRate, 120.0, 3, 16);
    if (t.phase() == 0.0) { detail = "setup: phase should be mid-loop"; return false; }

    t.process(0.f, 10.f);  // reset edge
    if (t.phase() != 0.0) {
        detail = "phase after reset is " + std::to_string(t.phase());
        return false;
    }
    if (t.playheadFrames() != 0.0) {
        detail = "playhead after reset is " + std::to_string(t.playheadFrames());
        return false;
    }
    return true;
}

/** Division and multiplication must scale the advance rate. */
bool transportDivision(std::string& detail) {
    const int sampleRate = 48000;
    struct Case { float div; double expectRatio; };
    const Case cases[] = {{1.f, 1.0}, {2.f, 2.0}, {0.5f, 0.5}};

    double baseline = 0.0;
    for (const auto& c : cases) {
        Transport t(sampleRate);
        t.setClocksPerLoop(16);
        t.setBufferFrames(sampleRate * 4);
        t.setClockDivision(c.div);
        runClock(t, sampleRate, 120.0, 1, 16);

        const double advanced = t.totalPhaseAdvanced();
        if (c.div == 1.f) baseline = advanced;
        else {
            const double ratio = advanced / baseline;
            if (std::abs(ratio - c.expectRatio) > 0.05) {
                detail = "div " + std::to_string(c.div) + " ratio " + std::to_string(ratio) +
                         " expected " + std::to_string(c.expectRatio);
                return false;
            }
        }
    }
    return true;
}

/** Loop start and length must window the playhead into the buffer. */
bool transportLoop(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t frames = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(frames);

    t.setLoopBounds(0.25f, 0.5f);   // start quarter in, half the buffer long
    t.setPhaseForTest(0.0);
    if (std::abs(t.playheadFrames() - 12000.0) > 1.0) {
        detail = "phase 0 gave playhead " + std::to_string(t.playheadFrames()) + " expected 12000";
        return false;
    }
    t.setPhaseForTest(0.5);
    if (std::abs(t.playheadFrames() - 24000.0) > 1.0) {
        detail = "phase 0.5 gave playhead " + std::to_string(t.playheadFrames()) + " expected 24000";
        return false;
    }
    t.setPhaseForTest(0.999);
    const double end = t.playheadFrames();
    if (end < 12000.0 || end >= 36000.0) {
        detail = "phase ~1 gave playhead " + std::to_string(end) + " outside the loop window";
        return false;
    }
    return true;
}

/** With no clock the transport must free-run safely, never producing NaN. */
bool transportNoClock(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate * 2);

    for (int i = 0; i < sampleRate * 5; i++) t.process(0.f, 0.f);

    if (!std::isfinite(t.phase()) || !std::isfinite(t.playheadFrames())) {
        detail = "non-finite phase or playhead with no clock";
        return false;
    }
    if (t.phase() < 0.0 || t.phase() >= 1.0) {
        detail = "phase out of range: " + std::to_string(t.phase());
        return false;
    }
    if (t.clockDetected()) { detail = "clockDetected true with no clock"; return false; }
    return true;
}

/** Clock division must apply to the edge snap, not just the free-run rate. */
bool transportDivisionSnap(std::string& detail) {
    const int sampleRate = 48000;
    // At x2, two loops complete per 16 clocks, so after 8 clocks phase must be
    // back at 0, not at 8/16.
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate);
    t.setClockDivision(2.f);

    const int pulseSamples = 48;
    const int gap = 1000;
    // Feed exactly 8 clocks. At x2 that is one whole loop, so the phase
    // immediately after the 8th edge must be back at 0, not at 8/16.
    for (int pulse = 0; pulse < 8; pulse++) {
        for (int i = 0; i < gap; i++) t.process(i < pulseSamples ? 10.f : 0.f, 0.f);
    }
    // Sample the phase on the 8th edge itself.
    Transport t2(sampleRate);
    t2.setClocksPerLoop(16);
    t2.setBufferFrames(sampleRate);
    t2.setClockDivision(2.f);
    double phaseAtEighth = -1.0;
    for (int pulse = 0; pulse < 8; pulse++) {
        for (int i = 0; i < gap; i++) {
            const bool high = i < pulseSamples;
            t2.process(high ? 10.f : 0.f, 0.f);
            if (pulse == 7 && i == 0) phaseAtEighth = t2.phase();
        }
    }
    const double distanceFromZero = std::min(phaseAtEighth, 1.0 - phaseAtEighth);
    if (distanceFromZero > 0.02) {
        detail = "at x2 the phase on the 8th clock edge is " + std::to_string(phaseAtEighth) +
                 ", expected ~0; the snap is using the x1 grid";
        return false;
    }
    return true;
}

/** A reset arriving on the same sample as a clock edge must consume that edge.
 *
 *  PreFlightClock in this repo fires its master clock and reset together on the
 *  downbeat, so this is the normal integration, not an edge case.
 */
bool transportResetCoincident(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate);

    // Establish a clock first.
    for (int pulse = 0; pulse < 4; pulse++) {
        for (int i = 0; i < 1000; i++) t.process(i < 48 ? 10.f : 0.f, 0.f);
    }

    // Clock and reset rise on the same sample.
    t.process(10.f, 10.f);
    if (t.phase() != 0.0) {
        detail = "phase on the reset sample is " + std::to_string(t.phase());
        return false;
    }

    // Hold both high, then drop reset while the clock stays high. The still
    // high clock must NOT register as a fresh edge. If it did, the phase would
    // snap to one clock step (1/16 = 0.0625). Normal free-run advance over
    // these few samples is far smaller, so the two are easy to tell apart.
    for (int i = 0; i < 47; i++) t.process(10.f, 10.f);
    for (int i = 0; i < 10; i++) t.process(10.f, 0.f);

    const double oneClockStep = 1.0 / 16.0;
    if (t.phase() > oneClockStep * 0.5) {
        detail = "phase jumped to " + std::to_string(t.phase()) +
                 " after reset, near one clock step (" + std::to_string(oneClockStep) +
                 "); the coincident clock edge was not consumed";
        return false;
    }
    return true;
}

/** Loop window must stay inside the buffer even at maximum start. */
bool transportLoopMaxStart(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t frames = 48000;
    Transport t(sampleRate);
    t.setBufferFrames(frames);
    t.setLoopBounds(1.0f, 0.25f);   // start at the very end

    for (double p : {0.0, 0.5, 0.999}) {
        t.setPhaseForTest(p);
        const double head = t.playheadFrames();
        if (!(head >= 0.0 && head < static_cast<double>(frames))) {
            detail = "phase " + std::to_string(p) + " gave playhead " +
                     std::to_string(head) + " outside buffer of " + std::to_string(frames);
            return false;
        }
    }
    return true;
}

/** A sample-rate change must not discard the measured tempo. */
bool transportSampleRateChange(std::string& detail) {
    Transport t(48000);
    t.setClocksPerLoop(16);
    t.setBufferFrames(48000);

    // Clock at 30 BPM: 2 second period.
    const int gap = 48000 * 2;
    for (int pulse = 0; pulse < 3; pulse++) {
        for (int i = 0; i < gap; i++) t.process(i < 48 ? 10.f : 0.f, 0.f);
    }
    const double before = t.clockPeriodSeconds();
    if (std::abs(before - 2.0) > 0.01) {
        detail = "setup: measured period " + std::to_string(before) + " expected 2.0";
        return false;
    }

    t.setSampleRate(96000);
    const double after = t.clockPeriodSeconds();
    if (std::abs(after - before) > 1e-9) {
        detail = "period changed from " + std::to_string(before) + " to " +
                 std::to_string(after) + " across a sample-rate change";
        return false;
    }
    return true;
}

/** The first edge after an idle gap must set the origin, not record the gap. */
bool transportClockRestart(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate);

    // Long unclocked idle.
    for (int i = 0; i < sampleRate * 6; i++) t.process(0.f, 0.f);

    // The very first edge after idle must only set the measurement origin.
    // If it records the 6 second gap as the period, playback crawls until the
    // second edge arrives and then jumps.
    for (int i = 0; i < 48; i++) t.process(10.f, 0.f);
    const double afterFirstEdge = t.clockPeriodSeconds();
    if (afterFirstEdge > 3.0) {
        detail = "first edge after idle recorded a period of " +
                 std::to_string(afterFirstEdge) + " seconds";
        return false;
    }

    // After a real interval the measured period must be correct.
    const int gap = sampleRate / 2;
    for (int pulse = 0; pulse < 3; pulse++) {
        for (int i = 0; i < gap; i++) t.process(i < 48 ? 10.f : 0.f, 0.f);
    }
    const double period = t.clockPeriodSeconds();
    if (std::abs(period - 0.5) > 0.01) {
        detail = "period after restart is " + std::to_string(period) + ", expected ~0.5";
        return false;
    }
    return true;
}

/** Clock jitter must not manufacture extra downbeats.
 *
 *  A late edge corrects the phase backwards. Treating any backward correction
 *  as a wrap fires downbeat_out spuriously throughout the loop.
 */
bool transportDownbeatJitter(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate);

    const int loops = 10;
    const int nominalGap = 1000;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> jitter(-60, 60);

    int downbeats = 0;
    for (int pulse = 0; pulse < loops * 16; pulse++) {
        const int gap = nominalGap + jitter(rng);
        for (int i = 0; i < gap; i++) {
            t.process(i < 48 ? 10.f : 0.f, 0.f);
            if (t.downbeat()) downbeats++;
        }
    }
    if (downbeats != loops) {
        detail = "expected " + std::to_string(loops) + " downbeats over " +
                 std::to_string(loops) + " loops, got " + std::to_string(downbeats);
        return false;
    }
    return true;
}

/** A reset between clock edges must not corrupt the next period measurement. */
bool transportResetMidInterval(std::string& detail) {
    const int sampleRate = 48000;
    Transport t(sampleRate);
    t.setClocksPerLoop(16);
    t.setBufferFrames(sampleRate);

    const int gap = sampleRate / 2;   // 0.5 s between edges
    const int pulseHigh = 48;

    auto edge = [&]() { for (int i = 0; i < pulseHigh; i++) t.process(10.f, 0.f); };
    auto low  = [&](int n) { for (int i = 0; i < n; i++) t.process(0.f, 0.f); };

    // Three edges, ending exactly on the third so the elapsed timer is at zero.
    edge(); low(gap - pulseHigh);
    edge(); low(gap - pulseHigh);
    edge();

    const double before = t.clockPeriodSeconds();
    if (std::abs(before - 0.5) > 0.01) {
        detail = "setup: period " + std::to_string(before) + " expected 0.5";
        return false;
    }

    // Reset halfway to the next edge, then complete the interval normally.
    low(gap / 2 - pulseHigh);
    t.process(0.f, 10.f);
    low(gap / 2 - 1);
    edge();

    const double after = t.clockPeriodSeconds();
    if (std::abs(after - 0.5) > 0.02) {
        detail = "period after a mid-interval reset is " + std::to_string(after) +
                 ", expected ~0.5; the reset restarted the clock timer";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// STFT
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Stft;

/**
 * Analysis then synthesis with no processing must reconstruct the input.
 *
 * This is the property everything downstream depends on: if the STFT does not
 * round-trip, every HPSS mask is applied to a signal that was already wrong.
 * Tested at lengths that are NOT multiples of the hop, because that is where
 * overlap-add framing bugs actually live.
 */
bool stftReconstruct(std::size_t inputLength, double& maxErrOut) {
    ReferenceFft fft(2048);
    Stft stft(fft, /*hop=*/512);

    std::vector<float> input(inputLength);
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> dist(-0.8f, 0.8f);
    for (auto& v : input) v = dist(rng);

    std::vector<float> output(inputLength, 0.f);
    stft.process(input.data(), output.data(), inputLength,
                 [](float*, std::size_t) { /* identity */ });

    // Assert over the WHOLE signal, including the first and last samples.
    // Skipping a frame at each end conceals boundary gaps: an unpadded
    // implementation leaves the tail after the last complete frame silent.
    double maxErr = 0.0;
    for (std::size_t i = 0; i < inputLength; i++) {
        maxErr = std::max(maxErr, std::abs(static_cast<double>(output[i] - input[i])));
    }
    maxErrOut = maxErr;
    return maxErr < 1e-4;
}

bool stftReconstructDefault(std::string& detail) {
    double err = 0.0;
    const bool ok = stftReconstruct(48000, err);
    if (!ok) detail = "max error " + std::to_string(err);
    return ok;
}

/** Framing must be correct at lengths that are not multiples of the hop. */
bool stftReconstructOddLengths(std::string& detail) {
    const std::size_t lengths[] = {4096, 4097, 5000, 6143, 8191, 12345, 20000};
    for (std::size_t len : lengths) {
        double err = 0.0;
        if (!stftReconstruct(len, err)) {
            detail = "length " + std::to_string(len) + " max error " + std::to_string(err);
            return false;
        }
    }
    return true;
}

/** Zeroing every bin must yield silence, proving the mask path is wired up. */
bool stftZeroMask(std::string& detail) {
    ReferenceFft fft(2048);
    Stft stft(fft, 512);

    std::vector<float> input(20000, 0.5f);
    std::vector<float> output(20000, 1.f);
    stft.process(input.data(), output.data(), input.size(),
                 [](float* spectrum, std::size_t len) {
                     for (std::size_t i = 0; i < len; i++) spectrum[i] = 0.f;
                 });

    for (std::size_t i = 0; i < output.size(); i++) {
        if (std::abs(output[i]) > 1e-6f) {
            detail = "sample " + std::to_string(i) + " is " + std::to_string(output[i]);
            return false;
        }
    }
    return true;
}

/** An empty or very short input must not read out of bounds or emit NaN. */
bool stftShortInput(std::string& detail) {
    ReferenceFft fft(2048);
    Stft stft(fft, 512);

    for (std::size_t len : {std::size_t(0), std::size_t(1), std::size_t(100), std::size_t(2047)}) {
        std::vector<float> input(len, 0.3f);
        std::vector<float> output(len, 0.f);
        stft.process(input.empty() ? nullptr : input.data(),
                     output.empty() ? nullptr : output.data(),
                     len, [](float*, std::size_t) {});
        for (std::size_t i = 0; i < len; i++) {
            if (!std::isfinite(output[i])) {
                detail = "non-finite output at length " + std::to_string(len);
                return false;
            }
        }
    }
    return true;
}

/** The window must satisfy COLA at hop = frameSize/4, which is what makes
 *  overlap-add reconstruct rather than amplitude-modulate the signal. */
bool stftCola(std::string& detail) {
    ReferenceFft fft(2048);
    Stft stft(fft, 512);

    const std::size_t n = stft.frameSize();
    const std::size_t hop = stft.hopSize();
    std::vector<double> sum(n * 4, 0.0);
    for (std::size_t start = 0; start + n <= sum.size(); start += hop) {
        for (std::size_t i = 0; i < n; i++) sum[start + i] += stft.windowGain(i);
    }
    // Inspect the steady-state region only.
    double lo = 1e9, hi = -1e9;
    for (std::size_t i = n; i < sum.size() - n; i++) {
        lo = std::min(lo, sum[i]);
        hi = std::max(hi, sum[i]);
    }
    if (std::abs(hi - lo) > 1e-6) {
        detail = "window sum ripples between " + std::to_string(lo) + " and " + std::to_string(hi);
        return false;
    }
    return true;
}

/** A tiny FFT must not derive a zero hop and hang the worker forever. */
bool stftTinyFft(std::string& detail) {
    for (std::size_t n : {std::size_t(2), std::size_t(4), std::size_t(8)}) {
        ReferenceFft fft(n);
        Stft stft(fft);
        if (stft.hopSize() < 1) {
            detail = "fft size " + std::to_string(n) + " derived hop " +
                     std::to_string(stft.hopSize()) + "; process() would never advance";
            return false;
        }
        // Must terminate.
        std::vector<float> in(64, 0.25f), out(64, 0.f);
        stft.process(in.data(), out.data(), in.size(), [](float*, std::size_t) {});
        for (float v : out) {
            if (!std::isfinite(v)) { detail = "non-finite output at fft size " + std::to_string(n); return false; }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// HPSS (Tier 0 separation)
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Hpss;
using WiggleRoom::stems::StemLayer;

namespace {
/** Energy of a signal. */
double energy(const std::vector<float>& x) {
    double e = 0.0;
    for (float v : x) e += static_cast<double>(v) * v;
    return e;
}

/** A click train: broadband transients, sparse in time. */
void makeClicks(std::vector<float>& out, int sampleRate, int count) {
    std::fill(out.begin(), out.end(), 0.f);
    const std::size_t spacing = out.size() / static_cast<std::size_t>(count);
    for (int c = 0; c < count; c++) {
        const std::size_t at = c * spacing;
        for (std::size_t i = 0; i < 24 && at + i < out.size(); i++) {
            out[at + i] = (i % 2 == 0 ? 1.f : -1.f) * (1.f - static_cast<float>(i) / 24.f);
        }
    }
    (void)sampleRate;
}

/** A steady sine: narrowband, sustained in time. */
void makeSine(std::vector<float>& out, int sampleRate, double freq) {
    for (std::size_t i = 0; i < out.size(); i++) {
        out[i] = 0.5f * static_cast<float>(
            std::sin(2.0 * kPi * freq * static_cast<double>(i) / sampleRate));
    }
}
}  // namespace

/**
 * The core separation claim: percussive material lands in the percussive
 * layer and sustained material in the harmonic layer.
 *
 * Measured as an energy ratio rather than judged by ear, so the threshold is
 * falsifiable.
 */
bool hpssSeparates(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t n = 48000;

    std::vector<float> clicks(n), sine(n), mix(n);
    makeClicks(clicks, sampleRate, 8);
    makeSine(sine, sampleRate, 440.0);
    for (std::size_t i = 0; i < n; i++) mix[i] = clicks[i] + sine[i];

    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(mix.data(), n, sampleRate, out);

    // The sine sits at 440 Hz, above the default low split, so it should land
    // in the harmonic layer rather than the low layer.
    const double percEnergy = energy(out.layer[(int)StemLayer::Percussive]);
    const double harmEnergy = energy(out.layer[(int)StemLayer::Harmonic]);

    // Correlate each layer against the two sources to see where they went.
    auto correlate = [&](const std::vector<float>& a, const std::vector<float>& b) {
        double num = 0.0;
        for (std::size_t i = 0; i < n; i++) num += static_cast<double>(a[i]) * b[i];
        return num / std::sqrt(energy(a) * energy(b) + 1e-20);
    };

    const double percVsClicks = correlate(out.layer[(int)StemLayer::Percussive], clicks);
    const double percVsSine   = correlate(out.layer[(int)StemLayer::Percussive], sine);
    const double harmVsSine   = correlate(out.layer[(int)StemLayer::Harmonic], sine);
    const double harmVsClicks = correlate(out.layer[(int)StemLayer::Harmonic], clicks);

    if (percVsClicks < 0.5 || std::abs(percVsSine) > 0.2) {
        detail = "percussive layer: clicks corr " + std::to_string(percVsClicks) +
                 ", sine corr " + std::to_string(percVsSine);
        return false;
    }
    if (harmVsSine < 0.5 || std::abs(harmVsClicks) > 0.3) {
        detail = "harmonic layer: sine corr " + std::to_string(harmVsSine) +
                 ", clicks corr " + std::to_string(harmVsClicks);
        return false;
    }
    if (percEnergy <= 0.0 || harmEnergy <= 0.0) {
        detail = "a layer is empty";
        return false;
    }
    return true;
}

/**
 * The four layers must be DISJOINT so unity sum reconstructs the source.
 *
 * The spec requires this explicitly: an earlier draft defined Harmonic without
 * excluding the Low band, so summing all four at unity double-counted bass.
 */
bool hpssLayersSumToSource(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t n = 24000;

    std::vector<float> mix(n);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
    for (std::size_t i = 0; i < n; i++) {
        mix[i] = dist(rng) + 0.3f * static_cast<float>(
            std::sin(2.0 * kPi * 110.0 * static_cast<double>(i) / sampleRate));
    }

    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(mix.data(), n, sampleRate, out);

    std::vector<float> sum(n, 0.f);
    for (int L = 0; L < Hpss::kNumLayers; L++) {
        for (std::size_t i = 0; i < n; i++) sum[i] += out.layer[L][i];
    }

    const double srcE = energy(mix);
    double errE = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        const double d = static_cast<double>(sum[i]) - mix[i];
        errE += d * d;
    }
    const double errDb = 10.0 * std::log10((errE + 1e-20) / (srcE + 1e-20));
    if (errDb > -20.0) {
        detail = "layer sum differs from source by " + std::to_string(errDb) +
                 " dB relative; layers are not disjoint";
        return false;
    }
    return true;
}

/** The low layer must actually capture low-frequency content. */
bool hpssLowBand(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t n = 24000;

    std::vector<float> low(n), high(n), mix(n);
    makeSine(low, sampleRate, 60.0);
    makeSine(high, sampleRate, 3000.0);
    for (std::size_t i = 0; i < n; i++) mix[i] = low[i] + high[i];

    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(mix.data(), n, sampleRate, out);

    auto correlate = [&](const std::vector<float>& a, const std::vector<float>& b) {
        double num = 0.0;
        for (std::size_t i = 0; i < n; i++) num += static_cast<double>(a[i]) * b[i];
        return num / std::sqrt(energy(a) * energy(b) + 1e-20);
    };

    const double lowVs60 = correlate(out.layer[(int)StemLayer::Low], low);
    const double lowVs3k = correlate(out.layer[(int)StemLayer::Low], high);
    if (lowVs60 < 0.5) {
        detail = "low layer correlates only " + std::to_string(lowVs60) + " with 60 Hz";
        return false;
    }
    if (std::abs(lowVs3k) > 0.2) {
        detail = "low layer leaked 3 kHz, corr " + std::to_string(lowVs3k);
        return false;
    }
    return true;
}

/** Empty, silent and very short inputs must produce defined output. */
bool hpssDegenerate(std::string& detail) {
    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;

    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(500), std::size_t(4096)}) {
        std::vector<float> in(n, 0.f);
        hpss.separate(n ? in.data() : nullptr, n, 48000, out);
        for (int L = 0; L < Hpss::kNumLayers; L++) {
            if (out.layer[L].size() != n) {
                detail = "layer " + std::to_string(L) + " size " +
                         std::to_string(out.layer[L].size()) + " for input " + std::to_string(n);
                return false;
            }
            for (float v : out.layer[L]) {
                if (!std::isfinite(v)) { detail = "non-finite output at n=" + std::to_string(n); return false; }
            }
        }
    }
    return true;
}

/**
 * Dump the magnitude spectrogram and both median-filtered versions, so a
 * reference implementation can recompute the medians from the SAME input and
 * compare. Comparing filtered output rather than final audio isolates the
 * median filter from any STFT or FFT differences between implementations.
 */
int dumpHpssMedians() {
    const int sampleRate = 48000;
    const std::size_t n = 8192;
    std::vector<float> mix(n);
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (std::size_t i = 0; i < n; i++) {
        mix[i] = dist(rng) + 0.4f * static_cast<float>(
            std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / sampleRate));
    }

    ReferenceFft fft(256);            // small, so the dump stays manageable
    Hpss hpss(fft);
    hpss.setKernelSize(7);
    Hpss::Result out;
    hpss.separate(mix.data(), n, sampleRate, out);

    const std::size_t F = hpss.debugFrames();
    const std::size_t B = hpss.debugBins();

    auto emitArray = [](const char* name, const std::vector<float>& v,
                        std::size_t count, bool last) {
        std::cout << "\"" << name << "\": [";
        for (std::size_t i = 0; i < count; i++) {
            std::cout << std::setprecision(9) << v[i];
            if (i + 1 < count) std::cout << ",";
        }
        std::cout << "]" << (last ? "" : ",");
    };

    std::cout << "{\"frames\": " << F << ", \"bins\": " << B
              << ", \"kernel\": " << hpss.debugKernel() << ", ";
    emitArray("magnitude", hpss.debugMagnitude(), F * B, false);
    emitArray("harmonic_median", hpss.debugHarmonicMedian(), F * B, false);
    emitArray("percussive_median", hpss.debugPercussiveMedian(), F * B, true);
    std::cout << "}" << std::endl;
    return 0;
}

/** The Residual layer must actually carry energy for ordinary dense audio.
 *
 *  With pure soft masks the harmonic and percussive shares sum to exactly 1,
 *  leaving Residual permanently silent and the four-layer interface a fiction.
 */
bool hpssResidualPopulated(std::string& detail) {
    const int sampleRate = 48000;
    const std::size_t n = 24000;

    // Dense material: noise plus a sustained tone, the ambiguous case.
    std::vector<float> mix(n);
    std::mt19937 rng(31337);
    std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
    for (std::size_t i = 0; i < n; i++) {
        mix[i] = dist(rng) + 0.4f * static_cast<float>(
            std::sin(2.0 * kPi * 700.0 * static_cast<double>(i) / sampleRate));
    }

    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(mix.data(), n, sampleRate, out);

    const double total = energy(mix);
    const double res = energy(out.layer[(int)StemLayer::Residual]);
    const double frac = res / (total + 1e-20);
    if (frac < 0.001) {
        detail = "residual holds " + std::to_string(frac * 100.0) +
                 "% of source energy; the layer is effectively silent";
        return false;
    }
    return true;
}

/** Bins whose centre is below the split must land in the Low layer.
 *
 *  Rounding the split to the nearest bin excludes a bin whose centre is still
 *  below the split: at 44.1 kHz with a 2048-point FFT and a 200 Hz split, bin 9
 *  is centred at about 193.8 Hz and was being left out.
 */
bool hpssLowSplitBoundary(std::string& detail) {
    const int sampleRate = 44100;
    const std::size_t n = 24000;
    const std::size_t fftSize = 2048;
    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(fftSize);
    const double bin9Hz = 9.0 * binHz;   // about 193.8 Hz, below the 200 Hz split

    std::vector<float> tone(n);
    makeSine(tone, sampleRate, bin9Hz);

    ReferenceFft fft(fftSize);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(tone.data(), n, sampleRate, out);

    const double lowE = energy(out.layer[(int)StemLayer::Low]);
    const double totalE = energy(tone);
    const double frac = lowE / (totalE + 1e-20);
    if (frac < 0.5) {
        detail = "a tone at " + std::to_string(bin9Hz) + " Hz (below the 200 Hz split) "
                 "put only " + std::to_string(frac * 100.0) + "% of its energy in the Low layer";
        return false;
    }
    return true;
}

/** The margin must scale the competing median BEFORE the soft-mask exponent.
 *
 *  The reference computes h^p / (h^p + (margin*p)^p). Applying the margin
 *  afterwards, as h^p / (h^p + margin*p^p), gives a weaker effective margin
 *  than configured and leaves less residual than the algorithm specifies.
 */
bool hpssMarginBeforeExponent(std::string& detail) {
    ReferenceFft fft(256);
    Hpss hpss(fft);
    hpss.setPower(2.f);
    hpss.setMargin(2.f);

    // Equal medians: the maximally ambiguous case, where the margin bites most.
    float harm = 0.f, perc = 0.f, res = 0.f;
    hpss.debugShares(1.f, 1.f, harm, perc, res);

    // h^2 / (h^2 + (2p)^2) = 1 / (1 + 4) = 0.2
    const float expected = 1.f / 5.f;
    if (std::abs(harm - expected) > 1e-4f) {
        detail = "equal medians gave share " + std::to_string(harm) +
                 ", expected " + std::to_string(expected) +
                 " (margin applied after the exponent gives 0.333)";
        return false;
    }
    if (std::abs(res - (1.f - 2.f * expected)) > 1e-4f) {
        detail = "residual " + std::to_string(res) + " inconsistent with shares";
        return false;
    }
    return true;
}

/** Input shorter than one FFT frame must be copied intact to Residual.
 *
 *  Stft pads by a whole frame on both sides, so analyse() always produces
 *  frames and the sub-frame branch was unreachable; short recordings were
 *  being spectrally smeared across all four layers instead.
 */
bool hpssSubFrameInput(std::string& detail) {
    const std::size_t n = 500;          // well under a 2048 frame
    std::vector<float> in(n);
    for (std::size_t i = 0; i < n; i++) in[i] = 0.25f + 0.01f * static_cast<float>(i % 7);

    ReferenceFft fft(2048);
    Hpss hpss(fft);
    Hpss::Result out;
    hpss.separate(in.data(), n, 48000, out);

    for (std::size_t i = 0; i < n; i++) {
        if (std::abs(out.layer[(int)StemLayer::Residual][i] - in[i]) > 1e-6f) {
            detail = "residual sample " + std::to_string(i) + " is " +
                     std::to_string(out.layer[(int)StemLayer::Residual][i]) +
                     ", expected " + std::to_string(in[i]);
            return false;
        }
    }
    for (int L : {(int)StemLayer::Low, (int)StemLayer::Percussive, (int)StemLayer::Harmonic}) {
        for (std::size_t i = 0; i < n; i++) {
            if (std::abs(out.layer[L][i]) > 1e-6f) {
                detail = "layer " + std::to_string(L) + " should be silent for sub-frame input";
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SeparationWorker
// ---------------------------------------------------------------------------

using WiggleRoom::stems::SeparationWorker;
using WiggleRoom::stems::StemSet;

namespace {
/** Deterministic test signal. */
std::vector<float> makeJobInput(std::size_t n, unsigned seed) {
    std::vector<float> v(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
    for (auto& x : v) x = dist(rng);
    return v;
}

/** Spin until pred() or the deadline passes. Returns whether pred() held. */
template <typename Pred>
bool waitFor(Pred pred, int milliseconds = 15000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}
}  // namespace

/** A submitted job must eventually publish a stem set the audio side can see. */
bool workerPublishes(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    auto input = makeJobInput(16384, 1);
    const uint64_t gen = worker.submit(input.data(), input.size(), 48000);
    if (gen == 0) { detail = "submit returned generation 0"; worker.stop(); return false; }

    const bool got = waitFor([&] {
        const StemSet* s = worker.acquire();
        const bool ok = (s != nullptr && s->generation == gen);
        worker.release(s);
        return ok;
    });
    if (!got) { detail = "no stem set published within the timeout"; worker.stop(); return false; }

    const StemSet* s = worker.acquire();
    bool sized = s && s->layer[0].channel[0].size() == input.size();
    worker.release(s);
    worker.stop();
    if (!sized) { detail = "published layers are the wrong size"; return false; }
    return true;
}

/** A superseded job must never publish over a newer take. */
bool workerDiscardsStale(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    // Big enough that separation takes real time, so the second job genuinely
    // arrives mid-flight. Submitting back to back does NOT exercise this path:
    // the pending slot is overwritten and the worker never starts the first job
    // at all, so the post-separation generation check is never reached.
    auto first  = makeJobInput(262144, 2);
    auto second = makeJobInput(8192, 3);

    const uint64_t genA = worker.submit(first.data(), first.size(), 48000);

    // Wait until the worker has actually BEGUN genA before superseding it.
    if (!waitFor([&] { return worker.debugJobsStarted() >= 1; }, 5000)) {
        detail = "worker never started the first job";
        worker.stop();
        return false;
    }

    const uint64_t genB = worker.submit(second.data(), second.size(), 48000);
    if (genB <= genA) { detail = "generations did not advance"; worker.stop(); return false; }

    const bool got = waitFor([&] {
        const StemSet* s = worker.acquire();
        const bool ok = (s != nullptr && s->generation == genB);
        worker.release(s);
        return ok;
    });
    if (!got) { detail = "newer job never published"; worker.stop(); return false; }

    // The in-flight genA must have been discarded rather than published.
    if (worker.debugJobsDiscarded() == 0) {
        detail = "no job was discarded; the superseded take was not detected";
        worker.stop();
        return false;
    }

    // Give any straggler a chance to overwrite, then confirm it did not.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const StemSet* s = worker.acquire();
    const bool stillNew = (s != nullptr && s->generation == genB &&
                           s->layer[0].channel[0].size() == second.size());
    worker.release(s);
    worker.stop();
    if (!stillNew) { detail = "a stale job published over the newer take"; return false; }
    return true;
}

/** Retired sets must be destroyed by the worker, never by the audio side. */
bool workerRetiresOffAudioThread(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    auto input = makeJobInput(8192, 4);
    for (int i = 0; i < 6; i++) {
        const uint64_t gen = worker.submit(input.data(), input.size(), 48000);
        if (!waitFor([&] {
                const StemSet* s = worker.acquire();
                const bool ok = (s != nullptr && s->generation == gen);
                worker.release(s);
                return ok;
            })) {
            detail = "job " + std::to_string(i) + " never published";
            worker.stop();
            return false;
        }
    }

    // Every set but the live one must have been reclaimed on the worker.
    const bool reclaimed = waitFor([&] { return worker.liveSetCount() <= 1; });
    const std::size_t freedOnAudio = worker.debugFreedOnAcquireThread();
    worker.stop();

    if (!reclaimed) { detail = "retired sets were not reclaimed"; return false; }
    if (freedOnAudio != 0) {
        detail = std::to_string(freedOnAudio) + " sets were freed on the acquiring thread";
        return false;
    }
    return true;
}

/** acquire() must be safe before anything has ever been published. */
bool workerEmptyAcquire(std::string& detail) {
    SeparationWorker worker;
    const StemSet* s = worker.acquire();
    if (s != nullptr) { detail = "acquire returned non-null before any job"; worker.release(s); return false; }
    worker.release(s);

    worker.start();
    const StemSet* s2 = worker.acquire();
    const bool ok = (s2 == nullptr);
    worker.release(s2);
    worker.stop();
    if (!ok) { detail = "acquire returned non-null before any job was submitted"; return false; }
    return true;
}

/** Hammer submit and acquire concurrently, looking for races and leaks. */
bool workerConcurrentHammer(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    auto input = makeJobInput(4096, 5);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};

    // Stand in for the audio thread: acquire, read, release, forever.
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const StemSet* s = worker.acquire();
            if (s) {
                volatile float sink = 0.f;
                for (int L = 0; L < 4; L++) {
                    if (!s->layer[L].channel[0].empty()) sink += s->layer[L].channel[0][0];
                }
                (void)sink;
                reads.fetch_add(1, std::memory_order_relaxed);
            }
            worker.release(s);
        }
    });

    for (int i = 0; i < 40; i++) {
        worker.submit(input.data(), input.size(), 48000);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    stop.store(true);
    reader.join();

    const std::size_t freedOnAudio = worker.debugFreedOnAcquireThread();
    worker.stop();

    if (reads.load() == 0) { detail = "reader never saw a published set"; return false; }
    if (freedOnAudio != 0) {
        detail = std::to_string(freedOnAudio) + " sets freed on the reader thread";
        return false;
    }
    return true;
}

/** stop() must terminate promptly and free everything. */
bool workerCleanShutdown(std::string& detail) {
    SeparationWorker worker;
    worker.start();
    auto input = makeJobInput(32768, 6);
    worker.submit(input.data(), input.size(), 48000);

    const auto begin = std::chrono::steady_clock::now();
    worker.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();

    if (elapsed > 5000) {
        detail = "stop() took " + std::to_string(elapsed) + " ms";
        return false;
    }
    if (worker.liveSetCount() != 0) {
        detail = std::to_string(worker.liveSetCount()) + " sets still live after stop()";
        return false;
    }
    return true;
}

/**
 * Stereo input must produce four stereo stems, not one channel silently
 * dropped and not the two sides interleaved into one signal.
 */
bool workerStereo(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    // Deliberately UNRELATED channels. If the worker interleaved them, or
    // copied one over the other, the two sides would come out identical.
    auto left  = makeJobInput(16384, 11);
    auto right = makeJobInput(16384, 12);

    const uint64_t gen = worker.submit(left.data(), right.data(), left.size(), 48000);
    if (!waitFor([&] {
            const StemSet* s = worker.acquire();
            const bool ok = (s != nullptr && s->generation == gen);
            worker.release(s);
            return ok;
        })) {
        detail = "stereo job never published";
        worker.stop();
        return false;
    }

    const StemSet* s = worker.acquire();
    std::string why;
    if (s->channels != 2) {
        why = "published set reports " + std::to_string(s->channels) + " channels";
    } else {
        double worstDiff = 0.0;
        double sumLeftEnergy = 0.0;
        double sumRightEnergy = 0.0;
        for (int L = 0; L < StemSet::kNumLayers && why.empty(); L++) {
            const auto& lc = s->layer[L].channel[0];
            const auto& rc = s->layer[L].channel[1];
            if (lc.size() != left.size() || rc.size() != right.size()) {
                why = "layer " + std::to_string(L) + " has the wrong length";
                break;
            }
            for (std::size_t i = 0; i < lc.size(); i++) {
                worstDiff = std::max(worstDiff, std::fabs((double)lc[i] - (double)rc[i]));
                sumLeftEnergy  += (double)lc[i] * lc[i];
                sumRightEnergy += (double)rc[i] * rc[i];
            }
        }
        if (why.empty() && worstDiff < 1e-4) {
            why = "left and right layers are identical; one channel was dropped or copied";
        }
        if (why.empty() && sumRightEnergy < 1e-6) {
            why = "right channel is silent";
        }
        if (why.empty() && sumLeftEnergy < 1e-6) {
            why = "left channel is silent";
        }
    }
    worker.release(s);
    worker.stop();
    if (!why.empty()) { detail = why; return false; }
    return true;
}

namespace {
/** An FFT backend that always throws, to drive the failure path. */
struct ThrowingFft : WiggleRoom::stems::FftBackend {
    explicit ThrowingFft(std::size_t n) : n_(n) {}
    std::size_t size() const override { return n_; }
    void forward(const float*, float*) override { throw std::runtime_error("fft failed"); }
    void inverse(const float*, float*) override { throw std::runtime_error("fft failed"); }
    std::size_t n_;
};

/** Counts how many backends were built, so injection can be proved. */
std::atomic<int> g_injectedBackends{0};
}  // namespace

/**
 * A separation that throws must be recorded and abandoned, not allowed to
 * escape the thread entry point and call std::terminate.
 */
bool workerSeparationFailureIsNonFatal(std::string& detail) {
    SeparationWorker worker;
    worker.setFftFactory([](std::size_t n) {
        return std::unique_ptr<WiggleRoom::stems::FftBackend>(new ThrowingFft(n));
    });
    worker.start();

    auto input = makeJobInput(8192, 13);
    worker.submit(input.data(), input.size(), 48000);

    if (!waitFor([&] { return worker.debugJobsFailed() >= 1; }, 5000)) {
        detail = "the failing job was never recorded as failed";
        worker.stop();
        return false;
    }

    // Nothing must have been published, and the worker must still be alive and
    // able to take another job.
    const StemSet* s = worker.acquire();
    const bool publishedNothing = (s == nullptr);
    worker.release(s);

    worker.submit(input.data(), input.size(), 48000);
    const bool stillAlive = waitFor([&] { return worker.debugJobsFailed() >= 2; }, 5000);
    worker.stop();

    if (!publishedNothing) { detail = "a failed separation published a set"; return false; }
    if (!stillAlive) { detail = "the worker stopped accepting jobs after a failure"; return false; }
    return true;
}

/** The injected FFT backend must actually be the one the worker uses. */
bool workerUsesInjectedFft(std::string& detail) {
    g_injectedBackends.store(0);
    SeparationWorker worker;
    worker.setFftFactory([](std::size_t n) {
        g_injectedBackends.fetch_add(1);
        return std::unique_ptr<WiggleRoom::stems::FftBackend>(new ReferenceFft(n));
    });
    worker.start();

    auto input = makeJobInput(8192, 14);
    const uint64_t gen = worker.submit(input.data(), input.size(), 48000);
    const bool got = waitFor([&] {
        const StemSet* s = worker.acquire();
        const bool ok = (s != nullptr && s->generation == gen);
        worker.release(s);
        return ok;
    });
    worker.stop();

    if (!got) { detail = "job never published with the injected backend"; return false; }
    if (g_injectedBackends.load() != 1) {
        detail = "factory was called " + std::to_string(g_injectedBackends.load()) +
                 " times; the worker is not using the injected backend";
        return false;
    }
    return true;
}

/**
 * stop() must interrupt a separation already in flight.
 *
 * The plain shutdown test uses a small buffer that finishes almost instantly,
 * so it passes whether or not cancellation exists. This one submits a job large
 * enough that running it to completion takes many seconds, waits until the
 * worker has definitely begun it, and only then calls stop().
 */
bool workerAbortsInFlightSeparation(std::string& detail) {
    // Sized from a measured run: separating this uncancelled takes several
    // seconds through ReferenceFft, well past the budget asserted below.
    auto input = makeJobInput(600000, 15);

    SeparationWorker worker;
    worker.start();
    worker.submit(input.data(), input.size(), 48000);

    if (!waitFor([&] { return worker.debugJobsStarted() >= 1; }, 5000)) {
        detail = "worker never started the long job";
        worker.stop();
        return false;
    }
    // Let it get properly into the separation rather than stopping at the door.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto begin = std::chrono::steady_clock::now();
    worker.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();

    // Measured latency is 14 ms, because cancellation is checked once per STFT
    // frame. The budget is set two orders of magnitude above that so a slow or
    // loaded CI runner cannot turn this into a timing flake, and it is still far
    // below the uncancelled cost: unwiring the abort flag makes this same job
    // take 7898 ms locally and longer on a runner.
    //
    // An earlier version checked only between whole layers and came in at
    // roughly a second locally, which failed on CI at 1630 ms. Raising the
    // threshold would have hidden that; the granularity was the real problem.
    if (elapsed > 2000) {
        detail = "stop() waited " + std::to_string(elapsed) +
                 " ms for the in-flight separation";
        return false;
    }
    if (worker.liveSetCount() != 0) {
        detail = "sets still live after an aborted shutdown";
        return false;
    }
    return true;
}

/**
 * A set retired while the reader held it must be reclaimed once the reader
 * lets go, WITHOUT waiting for another job.
 *
 * Reclaiming only at publication time meant that if every publication happened
 * to coincide with an active audio block, retired sets accumulated and the last
 * one stayed allocated until shutdown.
 */
bool workerReclaimsAfterReaderLeaves(std::string& detail) {
    SeparationWorker worker;
    worker.start();

    auto input = makeJobInput(8192, 16);
    const uint64_t genA = worker.submit(input.data(), input.size(), 48000);
    if (!waitFor([&] {
            const StemSet* s = worker.acquire();
            const bool ok = (s != nullptr && s->generation == genA);
            worker.release(s);
            return ok;
        })) {
        detail = "first job never published";
        worker.stop();
        return false;
    }

    // Pin the live set and hold it across the next publication.
    const StemSet* pinned = worker.acquire();
    if (!pinned) { detail = "acquire returned null for a published set"; worker.stop(); return false; }

    const uint64_t genB = worker.submit(input.data(), input.size(), 48000);
    if (!waitFor([&] { return worker.currentGeneration() == genB &&
                              worker.debugRetiredCount() >= 1; }, 15000)) {
        detail = "the pinned set was never retired";
        worker.release(pinned);
        worker.stop();
        return false;
    }

    // While still pinned it must NOT be freed.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (worker.debugRetiredCount() == 0) {
        detail = "a set the reader was still holding was reclaimed";
        worker.release(pinned);
        worker.stop();
        return false;
    }

    // Let go, and submit nothing further. Reclamation must still happen.
    worker.release(pinned);
    const bool reclaimed = waitFor([&] { return worker.debugRetiredCount() == 0; }, 5000);
    const std::size_t freedOnAudio = worker.debugFreedOnAcquireThread();
    worker.stop();

    if (!reclaimed) {
        detail = "the retired set was never reclaimed after the reader released it";
        return false;
    }
    if (freedOnAudio != 0) {
        detail = std::to_string(freedOnAudio) + " sets were freed on the reader thread";
        return false;
    }
    return true;
}

}  // namespace

/**
 * Every runnable subcommand, declared once.
 *
 * The coverage guard reads this via --list-commands rather than scraping the
 * source with a regex. Scraping has already gone wrong twice: once on a
 * lowercase-only character class that missed a mixed-case name, and once on
 * table-driven dispatch that does not look like `cmd == "..."` at all. An
 * executable declaring its own commands cannot drift from itself.
 */

// ---------------------------------------------------------------------------
// StemMixer
// ---------------------------------------------------------------------------

using WiggleRoom::stems::StemMixer;

namespace {
/** Four distinct layers whose straight sum is a known source signal. */
struct MixerFixture {
    StemSet set;
    std::vector<float> source;

    explicit MixerFixture(std::size_t n = 4096, bool stereo = false) {
        source.assign(n, 0.f);
        set.channels = stereo ? 2 : 1;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(n, 0.f);
            if (stereo) set.layer[L].channel[1].assign(n, 0.f);
        }
        for (std::size_t i = 0; i < n; i++) {
            const double t = (double)i / 48000.0;
            // Four unrelated components, so a mixer that double-counted or
            // dropped one would not cancel out by accident.
            const float parts[4] = {
                (float)(0.30 * std::sin(2 * M_PI * 110.0 * t)),
                (float)(0.20 * std::sin(2 * M_PI * 523.0 * t)),
                (float)(0.15 * std::sin(2 * M_PI * 1400.0 * t)),
                (float)(0.05 * std::sin(2 * M_PI * 3300.0 * t)),
            };
            float sum = 0.f;
            for (int L = 0; L < StemSet::kNumLayers; L++) {
                set.layer[L].channel[0][i] = parts[L];
                if (stereo) set.layer[L].channel[1][i] = parts[L] * 0.5f;
                sum += parts[L];
            }
            source[i] = sum;
        }
    }
};

double rms(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (float x : v) acc += (double)x * x;
    return std::sqrt(acc / (double)v.size());
}

double maxStep(const std::vector<float>& v) {
    double worst = 0.0;
    for (std::size_t i = 1; i < v.size(); i++) {
        worst = std::max(worst, std::fabs((double)v[i] - (double)v[i - 1]));
    }
    return worst;
}
}  // namespace

/** Four unity faders must reconstruct the source, not lift or attenuate it. */
bool mixerUnitySum(std::string& detail) {
    MixerFixture fx;
    StemMixer mixer(48000);
    mixer.snapToTargets(/*haveStems=*/true);

    std::vector<float> out;
    out.reserve(fx.source.size());
    for (std::size_t i = 0; i < fx.source.size(); i++) {
        out.push_back(mixer.process(&fx.set, (double)i, 0.f, 0.f).left);
    }

    const double refRms = rms(fx.source);
    const double outRms = rms(out);
    if (refRms < 1e-9) { detail = "reference signal is silent"; return false; }
    const double dB = 20.0 * std::log10(outRms / refRms);
    if (std::fabs(dB) > 0.5) {
        detail = "unity sum is " + std::to_string(dB) + " dB from the source";
        return false;
    }

    // Level is not enough on its own: four layers scaled to the right total
    // energy but in the wrong proportions would pass an RMS check. Compare
    // sample by sample.
    double worst = 0.0;
    for (std::size_t i = 0; i < out.size(); i++) {
        worst = std::max(worst, std::fabs((double)out[i] - (double)fx.source[i]));
    }
    if (worst > 1e-5) {
        detail = "unity sum differs from the source by up to " + std::to_string(worst);
        return false;
    }
    return true;
}

/** Muting must ramp, not step. */
bool mixerMuteIsClickFree(std::string& detail) {
    MixerFixture fx;
    StemMixer mixer(48000);
    mixer.snapToTargets(true);

    // A DC layer makes a step unmistakable: the signal itself contributes no
    // sample-to-sample change, so any jump in the output is the gain.
    const std::size_t n = fx.set.layer[0].channel[0].size();
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        fx.set.layer[L].channel[0].assign(n, 0.25f);
    }

    std::vector<float> out;
    for (std::size_t i = 0; i < 4000; i++) {
        if (i == 1000) mixer.setMute(0, true);
        if (i == 2500) mixer.setMute(0, false);
        out.push_back(mixer.process(&fx.set, (double)i, 0.f, 0.f).left);
    }

    // A hard mute would step by a full 0.25 in one sample. The ramp covers
    // 10 ms, so the per-sample change is 0.25 / 480.
    const double worst = maxStep(out);
    if (worst > 0.002) {
        detail = "mute stepped by " + std::to_string(worst) + " in one sample";
        return false;
    }
    // And it must actually reach silence on that channel, not merely ramp.
    if (std::fabs(out[2400] - 0.75f) > 1e-3) {
        detail = "muted channel did not reach silence; output was " +
                 std::to_string(out[2400]);
        return false;
    }
    return true;
}

/** Changing the analyser tap must not step. */
bool mixerSelectIsContinuous(std::string& detail) {
    MixerFixture fx;
    const std::size_t n = fx.set.layer[0].channel[0].size();
    // Two constant, maximally different stems, so a hard switch is a step of 2.
    fx.set.layer[0].channel[0].assign(n, 1.f);
    fx.set.layer[3].channel[0].assign(n, -1.f);

    StemMixer mixer(48000);
    mixer.snapToTargets(true);

    std::vector<float> taps;
    for (std::size_t i = 0; i < 3000; i++) {
        if (i == 500) mixer.setStemSelect(0);
        mixer.process(&fx.set, (double)i, 0.f, 0.f);
        taps.push_back(mixer.tap());
    }

    const double worst = maxStep(taps);
    if (worst > 0.005) {
        detail = "stem select stepped the tap by " + std::to_string(worst);
        return false;
    }
    if (std::fabs(taps[2500] - 1.f) > 1e-3) {
        detail = "tap did not settle on the selected stem; got " +
                 std::to_string(taps[2500]);
        return false;
    }
    return true;
}

/**
 * Switching the tap again before the previous crossfade finishes must still be
 * continuous.
 *
 * This is the case a crossfade between two stem INDICES gets wrong. Halfway
 * through a fade from A to B the emitted value is half of each, but the
 * outgoing index still names A, so restarting the fade towards C begins from
 * A's current sample and steps by half the distance between A and B.
 */
bool mixerRapidSelectIsContinuous(std::string& detail) {
    MixerFixture fx;
    const std::size_t n = fx.set.layer[0].channel[0].size();
    // Four constants spread as far apart as possible.
    const float values[4] = {1.f, -1.f, 0.75f, -0.75f};
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        fx.set.layer[L].channel[0].assign(n, values[L]);
    }

    StemMixer mixer(48000);
    mixer.snapToTargets(true);

    std::vector<float> taps;
    // Switch every 200 samples. The fade is 20 ms, which is 960 samples, so
    // every switch lands in the middle of the previous one.
    int next = 0;
    for (std::size_t i = 0; i < 4000; i++) {
        if (i % 200 == 0) {
            mixer.setStemSelect(next % StemSet::kNumLayers);
            next++;
        }
        mixer.process(&fx.set, (double)i, 0.f, 0.f);
        taps.push_back(mixer.tap());
    }

    const double worst = maxStep(taps);
    if (worst > 0.01) {
        detail = "rapid stem select stepped the tap by " + std::to_string(worst);
        return false;
    }
    return true;
}

/**
 * Withdrawing the stems must fade out rather than drop.
 *
 * The stems cannot be read once the set is gone, so zeroing them would collapse
 * that side of the mix in a single sample while the fallback was still fading
 * in, leaving a dip almost to silence at the exact moment the module is
 * supposed to be degrading gracefully.
 */
bool mixerStemsWithdrawalFadesOut(std::string& detail) {
    MixerFixture fx(24000);
    const std::size_t n = fx.source.size();
    const std::size_t switchAt = n / 2;

    StemMixer mixer(48000);
    mixer.snapToTargets(true);

    std::vector<float> out;
    for (std::size_t i = 0; i < n; i++) {
        // Fallback is silent here, so any dip is unmistakable.
        const bool haveStems = (i < switchAt);
        out.push_back(mixer.process(haveStems ? &fx.set : nullptr, (double)i, 0.f, 0.f).left);
    }

    const double material = maxStep(fx.source);
    const double worst = maxStep(out);
    if (worst > material * 1.5 + 1e-4) {
        detail = "withdrawal stepped by " + std::to_string(worst) +
                 " against a material step of " + std::to_string(material);
        return false;
    }
    return true;
}

/** While separating, the unseparated buffer goes to channel 1 alone. */
bool mixerFallbackIsSingleChannel(std::string& detail) {
    StemMixer mixer(48000);
    mixer.snapToTargets(/*haveStems=*/false);

    float out = 0.f;
    for (int i = 0; i < 500; i++) {
        out = mixer.process(nullptr, 0.0, 0.5f, 0.5f).left;
    }
    // Routing to all four unity channels would give 2.0, a 12 dB lift.
    if (std::fabs(out - 0.5f) > 1e-4) {
        detail = "fallback output was " + std::to_string(out) + ", expected 0.5";
        return false;
    }
    return true;
}

/**
 * The level before and after stems arrive must match, with no jump.
 *
 * This is spec scenario 15. It is the test that catches routing the fallback to
 * all four channels, which reads as a 12 dB step at the transition.
 */
bool mixerFallbackLevelIsPreserved(std::string& detail) {
    // Long enough that the settled window after the 50 ms crossfade is a real
    // measurement. The default fixture is 4096 samples, which is shorter than
    // the fade plus a useful tail.
    MixerFixture fx(24000);
    StemMixer mixer(48000);
    mixer.snapToTargets(false);

    std::vector<float> during, after, all;
    const std::size_t n = fx.source.size();
    const std::size_t switchAt = n / 2;

    // The playhead runs CONTINUOUSLY across the two phases. Restarting it at
    // zero for the second phase puts a discontinuity in the test signal itself,
    // right where the measurement is taken, which has nothing to do with the
    // mixer.
    for (std::size_t i = 0; i < n; i++) {
        const bool separated = (i >= switchAt);
        const float v = mixer.process(separated ? &fx.set : nullptr, (double)i,
                                      fx.source[i], fx.source[i]).left;
        all.push_back(v);
        if (!separated) during.push_back(v);
        // Skip the crossfade itself when measuring the settled level: 50 ms is
        // 2400 samples at 48 kHz.
        else if (i > switchAt + 4800) after.push_back(v);
    }

    const double a = rms(during);
    const double b = rms(after);
    if (a < 1e-9 || b < 1e-9) { detail = "one phase was silent"; return false; }
    const double dB = 20.0 * std::log10(b / a);
    if (std::fabs(dB) > 1.0) {
        detail = "level changed by " + std::to_string(dB) + " dB across the transition";
        return false;
    }

    // No jump either: the largest sample-to-sample change across the whole run
    // must be no worse than the source material's own.
    const double sourceStep = maxStep(fx.source);
    const double worst = maxStep(all);
    if (worst > sourceStep * 1.5 + 1e-4) {
        detail = "transition stepped by " + std::to_string(worst) +
                 " against a source step of " + std::to_string(sourceStep);
        return false;
    }
    return true;
}

/**
 * The transition between the fallback and the stems must RAMP.
 *
 * The level test above cannot show this. There the fallback is the same
 * recording the four layers sum to, so switching hard between them is already
 * continuous and removing the crossfade changes nothing. Here the fallback is a
 * different signal of the same RMS, which is the realistic case once the
 * playhead, the separation residual and the loop window are taken into account.
 * A hard switch then steps by up to the full amplitude of both signals.
 */
bool mixerSourceCrossfadeRamps(std::string& detail) {
    MixerFixture fx(24000);
    const std::size_t n = fx.source.size();

    // Same RMS as the stem sum, entirely different sample values.
    std::vector<float> fallback(n, 0.f);
    for (std::size_t i = 0; i < n; i++) {
        const double t = (double)i / 48000.0;
        fallback[i] = (float)(0.35 * std::sin(2 * M_PI * 77.0 * t + 1.1));
    }
    const double ratio = rms(fx.source) / std::max(1e-12, rms(fallback));
    for (auto& x : fallback) x = (float)(x * ratio);

    StemMixer mixer(48000);
    mixer.snapToTargets(false);

    // Continuous playhead across the switch, for the reason given in the level
    // test: restarting it would put a step in the material rather than in the
    // mixer.
    const std::size_t switchAt = n / 2;
    std::vector<float> out;
    for (std::size_t i = 0; i < n; i++) {
        const bool separated = (i >= switchAt);
        out.push_back(mixer.process(separated ? &fx.set : nullptr, (double)i,
                                    fallback[i], fallback[i]).left);
    }

    // The transition must not step by more than the material itself does.
    const double material = std::max(maxStep(fx.source), maxStep(fallback));
    const double worst = maxStep(out);
    if (worst > material * 1.5 + 1e-4) {
        detail = "transition stepped by " + std::to_string(worst) +
                 " against a material step of " + std::to_string(material);
        return false;
    }
    // And it must actually complete: the tail is the stems, not the fallback.
    double tailDiff = 0.0;
    for (std::size_t i = switchAt + 6000; i < out.size(); i++) {
        tailDiff = std::max(tailDiff, std::fabs((double)out[i] - (double)fx.source[i]));
    }
    if (tailDiff > 1e-4) {
        detail = "the crossfade never reached the stems; tail differs by " +
                 std::to_string(tailDiff);
        return false;
    }
    return true;
}

/** A stereo set must not collapse to mono, and a mono set must not read past its end. */
bool mixerStereoAndMono(std::string& detail) {
    MixerFixture stereo(2048, /*stereo=*/true);
    StemMixer mixer(48000);
    mixer.snapToTargets(true);
    bool sawDifference = false;
    for (std::size_t i = 0; i < 2048; i++) {
        const auto f = mixer.process(&stereo.set, (double)i, 0.f, 0.f);
        if (std::fabs(f.left - f.right) > 1e-4) sawDifference = true;
    }
    if (!sawDifference) { detail = "stereo set produced identical channels"; return false; }

    // A set flagged stereo but missing its right channel must fall back to the
    // left rather than index out of range.
    MixerFixture broken(2048, true);
    for (int L = 0; L < StemSet::kNumLayers; L++) broken.set.layer[L].channel[1].clear();
    StemMixer m2(48000);
    m2.snapToTargets(true);
    for (std::size_t i = 0; i < 2048; i++) {
        const auto f = m2.process(&broken.set, (double)i, 0.f, 0.f);
        if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
            detail = "a stereo set with no right channel produced a non-finite sample";
            return false;
        }
    }
    return true;
}

/**
 * A non-finite or out-of-range playhead must give silence.
 *
 * Casting a NaN or an infinity to size_t is undefined, and NaN slips past a
 * bare `< 0` comparison. The RingBuffer had exactly this defect.
 */
bool mixerNonFinitePosition(std::string& detail) {
    MixerFixture fx;
    StemMixer mixer(48000);
    mixer.snapToTargets(true);

    const double bad[] = {std::nan(""), std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(), -1.0, -0.5,
                          (double)fx.source.size(), (double)fx.source.size() + 100.0,
                          1e300};
    for (double p : bad) {
        const auto f = mixer.process(&fx.set, p, 0.f, 0.f);
        if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
            detail = "non-finite output for position " + std::to_string(p);
            return false;
        }
        if (std::fabs(f.left) > 1e-6f || std::fabs(f.right) > 1e-6f) {
            detail = "expected silence for position " + std::to_string(p);
            return false;
        }
    }
    return true;
}

/** An empty set must be silent rather than reading out of range. */
bool mixerEmptySet(std::string& detail) {
    StemSet empty;
    StemMixer mixer(48000);
    mixer.snapToTargets(true);
    for (int i = 0; i < 100; i++) {
        const auto f = mixer.process(&empty, (double)i, 0.f, 0.f);
        if (f.left != 0.f || f.right != 0.f) {
            detail = "an empty stem set produced output";
            return false;
        }
    }
    return true;
}

/** Fade durations are in seconds, so they must not change with sample rate. */
bool mixerFadeIsSampleRateInvariant(std::string& detail) {
    auto measureMuteFade = [](int sampleRate) {
        MixerFixture fx;
        const std::size_t n = fx.set.layer[0].channel[0].size();
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            fx.set.layer[L].channel[0].assign(n, 0.f);
        }
        fx.set.layer[0].channel[0].assign(n, 1.f);

        StemMixer mixer(sampleRate);
        mixer.snapToTargets(true);
        mixer.setMute(0, true);
        int samples = 0;
        for (std::size_t i = 0; i < n; i++) {
            const float v = mixer.process(&fx.set, (double)i, 0.f, 0.f).left;
            samples++;
            if (v <= 1e-6f) break;
        }
        return (double)samples / sampleRate;
    };

    const double at48 = measureMuteFade(48000);
    const double at96 = measureMuteFade(96000);
    if (at48 < 1e-6) { detail = "fade at 48 kHz took no time"; return false; }
    const double ratio = at96 / at48;
    if (ratio < 0.9 || ratio > 1.1) {
        detail = "fade took " + std::to_string(at48) + " s at 48 kHz and " +
                 std::to_string(at96) + " s at 96 kHz";
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Yin
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Yin;

namespace {
Yin makeYin(int sampleRate, std::size_t window) {
    Yin yin(window);
    yin.setSampleRate(sampleRate);
    yin.setFrequencyRange(50.f, 2200.f);
    return yin;
}

std::vector<float> sineWindow(double hz, int sampleRate, std::size_t n, double phase = 0.0) {
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; i++) {
        w[i] = (float)std::sin(2 * M_PI * hz * (double)i / sampleRate + phase);
    }
    return w;
}

double centsBetween(double a, double b) {
    if (a <= 0.0 || b <= 0.0) return 1e9;
    return 1200.0 * std::log2(a / b);
}
}  // namespace

/**
 * Sines from 55 Hz to 2 kHz within one cent.
 *
 * The frequencies deliberately do not divide the sample rate evenly, so the
 * true period is never a whole number of samples and the sub-sample refinement
 * is genuinely exercised. Integer periods would let a detector that only ever
 * returns whole lags pass.
 */
bool yinSineRange(std::string& detail) {
    const int sampleRate = 48000;
    const double targets[] = {55.0,   61.735, 82.407, 110.0,  138.591, 220.0,
                              329.63, 440.0,  554.37, 880.0,  1174.66, 1567.98,
                              1760.0, 1975.53, 2000.0};
    double worst = 0.0;
    double worstAt = 0.0;
    for (double f : targets) {
        // Two phases, because a window that happens to start at a zero crossing
        // is the easy case.
        for (double phase : {0.0, 1.234}) {
            auto w = sineWindow(f, sampleRate, 4096, phase);
            Yin yin = makeYin(sampleRate, 4096);
            const auto r = yin.analyse(w.data(), w.size());
            if (!r.voiced) {
                detail = "a pure sine at " + std::to_string(f) + " Hz was not voiced";
                return false;
            }
            const double err = std::fabs(centsBetween(r.frequency, f));
            if (err > worst) { worst = err; worstAt = f; }
        }
    }
    if (worst > 1.0) {
        detail = "worst error " + std::to_string(worst) + " cents at " +
                 std::to_string(worstAt) + " Hz";
        return false;
    }
    return true;
}

/** White noise must not look pitched. */
bool yinNoiseConfidence(std::string& detail) {
    std::mt19937 rng(20260818);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    double worst = 0.0;
    for (int trial = 0; trial < 20; trial++) {
        std::vector<float> w(4096);
        for (auto& x : w) x = dist(rng);
        Yin yin = makeYin(48000, 4096);
        const auto r = yin.analyse(w.data(), w.size());
        if (r.voiced) {
            detail = "white noise was reported as voiced on trial " + std::to_string(trial);
            return false;
        }
        worst = std::max(worst, (double)r.confidence);
    }
    // A pure tone scores about 1.0, so the gap is the whole point.
    if (worst > 0.4) {
        detail = "white noise reached confidence " + std::to_string(worst);
        return false;
    }
    return true;
}

/**
 * Harmonically rich waveforms must not report an octave error.
 *
 * A square wave repeats every period AND every two periods, so the global
 * minimum of the CMNDF frequently sits at twice the true lag. Taking the first
 * dip below the threshold rather than the global minimum is what prevents the
 * octave-down report, and this is the check that holds that rule in place.
 */
bool yinNoOctaveError(std::string& detail) {
    const int sampleRate = 48000;
    const double targets[] = {82.407, 110.0, 220.0, 329.63, 440.0, 880.0};
    for (double f : targets) {
        for (int shape = 0; shape < 3; shape++) {
            std::vector<float> w(4096);
            for (std::size_t i = 0; i < w.size(); i++) {
                const double t = 2 * M_PI * f * (double)i / sampleRate;
                if (shape == 0) {
                    w[i] = (std::sin(t) >= 0.0) ? 1.f : -1.f;              // square
                } else if (shape == 1) {
                    const double ph = std::fmod(f * (double)i / sampleRate, 1.0);
                    w[i] = (float)(2.0 * ph - 1.0);                        // saw
                } else {
                    // Weak fundamental, strong second harmonic. This is the
                    // shape that tempts an estimator into reporting the octave
                    // above, and a plain autocorrelation peak-pick fails it.
                    w[i] = (float)(0.2 * std::sin(t) + 1.0 * std::sin(2 * t) +
                                   0.5 * std::sin(3 * t));
                }
            }
            Yin yin = makeYin(sampleRate, 4096);
            const auto r = yin.analyse(w.data(), w.size());
            if (!r.voiced) {
                detail = "shape " + std::to_string(shape) + " at " + std::to_string(f) +
                         " Hz was not voiced";
                return false;
            }
            const double cents = centsBetween(r.frequency, f);
            if (std::fabs(cents) > 20.0) {
                detail = "shape " + std::to_string(shape) + " at " + std::to_string(f) +
                         " Hz reported " + std::to_string(r.frequency) + " Hz (" +
                         std::to_string(cents) + " cents, ratio " +
                         std::to_string(r.frequency / f) + ")";
                return false;
            }
        }
    }
    return true;
}

/** Silence must report nothing, not a confident-looking frequency. */
bool yinSilence(std::string& detail) {
    std::vector<float> w(4096, 0.f);
    Yin yin = makeYin(48000, 4096);
    auto r = yin.analyse(w.data(), w.size());
    if (r.frequency != 0.f || r.confidence != 0.f || r.voiced) {
        detail = "silence reported " + std::to_string(r.frequency) + " Hz at confidence " +
                 std::to_string(r.confidence);
        return false;
    }

    // Denormal-level noise is silence too, and is what an empty buffer that has
    // been through a filter actually contains.
    for (std::size_t i = 0; i < w.size(); i++) w[i] = (i % 2) ? 1e-9f : -1e-9f;
    r = yin.analyse(w.data(), w.size());
    if (r.voiced || r.frequency != 0.f) {
        detail = "near-silence reported " + std::to_string(r.frequency) + " Hz";
        return false;
    }
    return true;
}

/** A NaN anywhere in the window must not produce a result. */
bool yinNonFinite(std::string& detail) {
    Yin yin = makeYin(48000, 4096);
    for (double bad : {std::nan(""), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        auto w = sineWindow(440.0, 48000, 4096);
        w[2000] = (float)bad;
        const auto r = yin.analyse(w.data(), w.size());
        if (r.voiced || !std::isfinite(r.frequency) || !std::isfinite(r.confidence)) {
            detail = "a non-finite sample produced frequency " +
                     std::to_string(r.frequency) + " confidence " +
                     std::to_string(r.confidence);
            return false;
        }
    }
    // Short and null windows must be safe too.
    const auto tiny = yin.analyse(nullptr, 4096);
    if (tiny.voiced) { detail = "a null window was reported as voiced"; return false; }
    std::vector<float> four(4, 0.5f);
    const auto shortWindow = yin.analyse(four.data(), four.size());
    if (shortWindow.voiced) { detail = "a four-sample window was reported as voiced"; return false; }
    return true;
}

/** The same tone must read the same at any sample rate. */
bool yinSampleRateInvariant(std::string& detail) {
    for (double f : {110.0, 440.0, 1500.0}) {
        double first = 0.0;
        for (int sampleRate : {44100, 48000, 96000}) {
            auto w = sineWindow(f, sampleRate, 8192);
            Yin yin = makeYin(sampleRate, 8192);
            const auto r = yin.analyse(w.data(), w.size());
            if (!r.voiced) {
                detail = std::to_string(f) + " Hz was not voiced at " +
                         std::to_string(sampleRate) + " Hz";
                return false;
            }
            const double err = std::fabs(centsBetween(r.frequency, f));
            if (err > 1.0) {
                detail = std::to_string(f) + " Hz at sample rate " +
                         std::to_string(sampleRate) + " was off by " +
                         std::to_string(err) + " cents";
                return false;
            }
            if (first == 0.0) first = r.frequency;
        }
    }
    return true;
}

/**
 * The frequency range must actually bound the search.
 *
 * Narrowing the range is the only lever on the cost of this, so a range that is
 * ignored would be a silent performance regression as well as letting an
 * out-of-band lag be selected.
 */
bool yinRespectsRange(std::string& detail) {
    auto w = sineWindow(110.0, 48000, 4096);
    Yin yin(4096);
    yin.setSampleRate(48000);
    // 110 Hz is below the floor, so the fundamental cannot be selected.
    yin.setFrequencyRange(300.f, 2000.f);
    const auto r = yin.analyse(w.data(), w.size());
    if (r.frequency != 0.f && (r.frequency < 300.f || r.frequency > 2000.f)) {
        detail = "reported " + std::to_string(r.frequency) +
                 " Hz outside the configured 300 to 2000 Hz range";
        return false;
    }
    return true;
}

/**
 * The reported frequency must stay inside the range the caller asked for,
 * including when the bounds do not divide the sample rate evenly.
 *
 * The integer search bounds are deliberately widened to whole lags so that
 * interpolation has a point on each side of a minimum near a boundary. Letting
 * that widening leak into the result puts out-of-band pitches in front of the
 * scale detector. The earlier range test used 300 and 2000 Hz, which happen to
 * be integer-friendly at 48 kHz, and so never exercised this.
 */
bool yinRangeIsExact(std::string& detail) {
    const int sampleRate = 48000;
    struct Case { float low, high, tone; };
    const Case cases[] = {
        {300.5f, 1100.f, 1110.f},   // tone just above the ceiling
        {300.5f, 1100.f, 299.f},    // tone just below the floor
        {77.3f,  909.1f, 1500.f},
        {77.3f,  909.1f, 60.f},
        {123.45f, 678.9f, 678.9f},  // exactly on the ceiling
        {123.45f, 678.9f, 123.45f}, // exactly on the floor
    };
    for (const auto& c : cases) {
        auto w = sineWindow(c.tone, sampleRate, 4096);
        Yin yin(4096);
        yin.setSampleRate(sampleRate);
        yin.setFrequencyRange(c.low, c.high);
        const auto r = yin.analyse(w.data(), w.size());
        if (r.frequency == 0.f) continue;  // nothing found is a legitimate answer
        // A small tolerance for the float/double round trip on the bounds
        // themselves; the defect this catches is off by tens of Hz.
        const float slack = 0.01f;
        if (r.frequency < c.low - slack || r.frequency > c.high + slack) {
            detail = "a " + std::to_string(c.tone) + " Hz tone with range " +
                     std::to_string(c.low) + " to " + std::to_string(c.high) +
                     " reported " + std::to_string(r.frequency) + " Hz";
            return false;
        }
    }
    return true;
}

/**
 * A range spanning only two adjacent integer lags must still work.
 *
 * At 48 kHz a 1000 to 1020 Hz range is lags 47 and 48. The fractional
 * refinement resolves between them, so rejecting the range outright returned
 * nothing at all for a clean tone sitting inside it.
 */
bool yinNarrowRange(std::string& detail) {
    const int sampleRate = 48000;
    struct Case { float low, high, tone; };
    const Case cases[] = {
        {1000.f, 1020.f, 1010.f},
        {1900.f, 1960.f, 1930.f},
        {600.f,  610.f,  605.f},
    };
    for (const auto& c : cases) {
        auto w = sineWindow(c.tone, sampleRate, 4096);
        Yin yin(4096);
        yin.setSampleRate(sampleRate);
        yin.setFrequencyRange(c.low, c.high);
        const auto r = yin.analyse(w.data(), w.size());
        if (!r.voiced) {
            detail = "a clean " + std::to_string(c.tone) + " Hz tone inside a " +
                     std::to_string(c.low) + " to " + std::to_string(c.high) +
                     " Hz range was not voiced";
            return false;
        }
        const double err = std::fabs(centsBetween(r.frequency, c.tone));
        if (err > 25.0) {
            detail = "narrow range detected " + std::to_string(r.frequency) +
                     " Hz for a " + std::to_string(c.tone) + " Hz tone (" +
                     std::to_string(err) + " cents)";
            return false;
        }
    }
    return true;
}

/**
 * Non-finite parameters must be rejected, not clamped.
 *
 * std::min and std::max propagate NaN rather than clamping it, because every
 * comparison against NaN is false. A NaN threshold makes every `cmndf >=
 * threshold` test false, so the first lag examined is accepted as voiced
 * whatever the signal is, and white noise comes back as a confident pitch. A
 * NaN frequency bound is worse: it reaches a cast to size_t, which is undefined.
 */
bool yinNonFiniteParams(std::string& detail) {
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> noise(4096);
    for (auto& x : noise) x = dist(rng);

    for (double bad : {std::nan(""), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        Yin yin = makeYin(48000, 4096);
        const float before = yin.threshold();
        yin.setThreshold((float)bad);
        if (yin.threshold() != before) {
            detail = "a non-finite threshold changed the configured value";
            return false;
        }
        const auto r = yin.analyse(noise.data(), noise.size());
        if (r.voiced) {
            detail = "white noise was voiced after setting a non-finite threshold";
            return false;
        }

        Yin y2 = makeYin(48000, 4096);
        y2.setFrequencyRange((float)bad, 2000.f);
        y2.setFrequencyRange(50.f, (float)bad);
        auto tone = sineWindow(440.0, 48000, 4096);
        const auto r2 = y2.analyse(tone.data(), tone.size());
        if (!std::isfinite(r2.frequency) || !std::isfinite(r2.confidence)) {
            detail = "a non-finite frequency bound produced a non-finite result";
            return false;
        }
        // The range should be unchanged, so 440 Hz is still found.
        if (std::fabs(centsBetween(r2.frequency, 440.0)) > 1.0) {
            detail = "a rejected frequency bound still disturbed detection: got " +
                     std::to_string(r2.frequency) + " Hz";
            return false;
        }
    }
    return true;
}

/** analyse() must not allocate. */
bool yinNoAlloc(std::string& detail) {
    Yin yin = makeYin(48000, 4096);
    auto w = sineWindow(440.0, 48000, 4096);
    // Prime it once, then check the working buffers never move or resize.
    yin.analyse(w.data(), w.size());
    const void* before = yin.debugCmndf().data();
    const std::size_t sizeBefore = yin.debugCmndf().size();

    for (double f : {55.0, 220.0, 880.0, 1900.0}) {
        auto v = sineWindow(f, 48000, 4096);
        yin.analyse(v.data(), v.size());
        // Shorter windows too, which is where a naive implementation would
        // resize to fit.
        yin.analyse(v.data(), 1024);
    }
    if (yin.debugCmndf().data() != before || yin.debugCmndf().size() != sizeBefore) {
        detail = "the working buffer was reallocated during analysis";
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// ScaleDetect
// ---------------------------------------------------------------------------

using WiggleRoom::stems::ScaleDetect;

namespace {
using Mode = ScaleDetect::Mode;

float midiHz(int midi) {
    return 440.f * std::pow(2.f, (float)(midi - 69) / 12.f);
}

const char* modeName(Mode m) { return (m == Mode::Major) ? "major" : "minor"; }

std::string keyName(const ScaleDetect::Result& r) {
    return std::string(ScaleDetect::noteName(r.root)) + " " + modeName(r.mode);
}

/** Key plus the two fields that explain a failure the key alone cannot. */
std::string keyDetail(const ScaleDetect::Result& r) {
    return keyName(r) + " (detected=" + (r.detected ? "true" : "false") +
           ", confidence=" + std::to_string(r.confidence) + ")";
}

/** Scale degrees, as semitone offsets from the tonic. */
const int kMajorDegrees[7] = {0, 2, 4, 5, 7, 9, 11};
const int kMinorDegrees[7] = {0, 2, 3, 5, 7, 8, 10};

/**
 * Feed a scale with the tonic and dominant emphasised.
 *
 * Real music emphasises the tonic, and that emphasis is exactly what the
 * Krumhansl-Schmuckler profiles encode. A run of the scale with every note
 * weighted equally carries much less information about which degree is the
 * tonic, which is why the relative major and minor test below needs this.
 */
void feedKey(ScaleDetect& detector, int tonicMidi, Mode mode, int repeats = 4) {
    const int* degrees = (mode == Mode::Major) ? kMajorDegrees : kMinorDegrees;
    for (int rep = 0; rep < repeats; rep++) {
        for (int d = 0; d < 7; d++) detector.addPitch(midiHz(tonicMidi + degrees[d]), 1.f);
        for (int k = 0; k < 3; k++) {
            detector.addPitch(midiHz(tonicMidi), 1.f);
            detector.addPitch(midiHz(tonicMidi + degrees[4]), 1.f);
        }
    }
}
}  // namespace

/** The headline case: a C major scale must detect C major. */
bool scaleCMajor(std::string& detail) {
    ScaleDetect detector;
    // Equal weights, no tonic emphasis at all. The profiles are asymmetric
    // enough to carry this on their own.
    for (int rep = 0; rep < 4; rep++) {
        for (int d = 0; d < 7; d++) detector.addPitch(midiHz(60 + kMajorDegrees[d]), 1.f);
    }
    const auto r = detector.detect();
    if (!r.detected || r.root != 0 || r.mode != Mode::Major) {
        detail = "a C major scale gave " + keyDetail(r);
        return false;
    }
    return true;
}

/** Every transposition of both modes must land on the transposed key. */
bool scaleTranspositions(std::string& detail) {
    for (int mode = 0; mode < 2; mode++) {
        const Mode m = (mode == 0) ? Mode::Major : Mode::Minor;
        for (int t = 0; t < 12; t++) {
            ScaleDetect detector;
            feedKey(detector, 60 + t, m);
            const auto r = detector.detect();
            const int expected = t % 12;
            if (!r.detected || r.root != expected || r.mode != m) {
                detail = std::string("expected ") + ScaleDetect::noteName(expected) + " " +
                         modeName(m) + " but got " + keyDetail(r);
                return false;
            }
        }
    }
    return true;
}

/**
 * Relative major and minor must be told apart.
 *
 * C major and A minor contain exactly the same seven pitch classes, so nothing
 * but the WEIGHTING of those classes can separate them. This is the case that
 * fails outright if the correlation is replaced by a plain dot product or if
 * only one profile is consulted.
 */
bool scaleRelativeMinor(std::string& detail) {
    struct Case { int tonic; Mode mode; int expectedRoot; };
    const Case cases[] = {
        {60, Mode::Major, 0},   // C major
        {57, Mode::Minor, 9},   // A minor, the same seven notes
        {67, Mode::Major, 7},   // G major
        {64, Mode::Minor, 4},   // E minor, the same seven notes
    };
    for (const auto& c : cases) {
        ScaleDetect detector;
        feedKey(detector, c.tonic, c.mode);
        const auto r = detector.detect();
        if (!r.detected || r.root != c.expectedRoot || r.mode != c.mode) {
            detail = std::string("expected ") + ScaleDetect::noteName(c.expectedRoot) + " " +
                     modeName(c.mode) + " but got " + keyDetail(r);
            return false;
        }
    }
    return true;
}

/**
 * A key must survive chromatic bleed.
 *
 * A real recording puts some weight in every pitch class, not just the seven in
 * the key. That constant floor is exactly what separates a correlation from a
 * dot product: the dot product is then dominated by the profile sums, and the
 * minor profile sums higher (44.51 against 41.79), so every key comes back
 * minor. Subtracting both means is what makes the twenty-four candidates
 * comparable.
 */
bool scaleChromaticFloor(std::string& detail) {
    struct Case { int tonic; Mode mode; int expectedRoot; };
    const Case cases[] = {
        {60, Mode::Major, 0},
        {67, Mode::Major, 7},
        {57, Mode::Minor, 9},
        {62, Mode::Minor, 2},
    };
    for (const auto& c : cases) {
        ScaleDetect detector;
        const int* degrees = (c.mode == Mode::Major) ? kMajorDegrees : kMinorDegrees;
        for (int rep = 0; rep < 6; rep++) {
            for (int pc = 0; pc < 12; pc++) detector.addPitch(midiHz(60 + pc), 1.f);
            for (int d = 0; d < 7; d++) detector.addPitch(midiHz(c.tonic + degrees[d]), 1.f);
            detector.addPitch(midiHz(c.tonic), 1.f);
            detector.addPitch(midiHz(c.tonic + degrees[4]), 1.f);
        }
        const auto r = detector.detect();
        if (!r.detected || r.root != c.expectedRoot || r.mode != c.mode) {
            detail = std::string("with a chromatic floor, expected ") +
                     ScaleDetect::noteName(c.expectedRoot) + " " + modeName(c.mode) +
                     " but got " + keyDetail(r);
            return false;
        }
    }
    return true;
}

/**
 * Low-confidence pitches must be ignored even when they all agree.
 *
 * The unpitched test above is caught by the key confidence gate, because random
 * frequencies make a flat histogram with no tonal centre. This one is not: a
 * percussive layer has spectral peaks, so YIN latches onto the same wrong
 * pitches repeatedly and the histogram is strongly biased rather than flat.
 * Only the per-pitch confidence gate stops that from deciding the key.
 */
bool scaleLowConfidenceIsIgnored(std::string& detail) {
    ScaleDetect detector;
    feedKey(detector, 60, Mode::Major);  // C major
    const auto established = detector.detect();
    if (!established.detected || established.root != 0) {
        detail = "setup failed: expected C major, got " + keyName(established);
        return false;
    }

    // A great many detections, all agreeing on F sharp major, all just under the
    // voicing threshold. Unfiltered they would swamp the real key several times
    // over.
    for (int rep = 0; rep < 200; rep++) {
        for (int d = 0; d < 7; d++) detector.addPitch(midiHz(66 + kMajorDegrees[d]), 0.45f);
    }

    const auto after = detector.detect();
    if (after.root != 0 || after.mode != Mode::Major) {
        detail = "low-confidence pitches moved the key from C major to " + keyName(after);
        return false;
    }
    return true;
}

/**
 * An unpitched stem must not move the key.
 *
 * Pitches arrive carrying YIN's confidence, and on a drum layer that confidence
 * is low, so nothing is counted and the held result stands. This is the
 * behaviour the spec calls for and the source concept assumed away.
 */
bool scaleUnpitchedHolds(std::string& detail) {
    ScaleDetect detector;
    // Establish a real key first, so there is something to lose.
    feedKey(detector, 65, Mode::Major);  // F major
    const auto established = detector.detect();
    if (!established.detected || established.root != 5) {
        detail = "setup failed: expected F major, got " + keyName(established);
        return false;
    }

    // Now a drum layer: frequencies all over the place, all below the voicing
    // threshold.
    std::mt19937 rng(20260818);
    std::uniform_real_distribution<float> freq(60.f, 3000.f);
    std::uniform_real_distribution<float> conf(0.f, 0.45f);
    for (int i = 0; i < 2000; i++) detector.addPitch(freq(rng), conf(rng));

    const auto after = detector.detect();
    if (after.root != established.root || after.mode != established.mode) {
        detail = "an unpitched stem moved the key from " + keyName(established) + " to " +
                 keyName(after);
        return false;
    }
    return true;
}

/**
 * A handful of confident detections must not decide a key.
 *
 * Correlation alone will not stop this: a histogram with two or three bins
 * filled correlates with something, and correlates well. The minimum
 * accumulated weight is the gate that does it.
 */
bool scaleWeightGate(std::string& detail) {
    ScaleDetect detector;
    detector.setSeed(0, Mode::Major);
    // Three confident pitches, which is far less evidence than a key needs.
    detector.addPitch(midiHz(63), 1.f);
    detector.addPitch(midiHz(66), 1.f);
    detector.addPitch(midiHz(70), 1.f);

    const auto r = detector.detect();
    if (r.detected) {
        detail = "three pitches were enough to detect " + keyName(r) + " at confidence " +
                 std::to_string(r.confidence);
        return false;
    }
    if (r.root != 0 || r.mode != Mode::Major) {
        detail = "the seed was not held; got " + keyName(r);
        return false;
    }
    return true;
}

/**
 * A fresh detector reports the manual seed, and stops doing so once a real
 * detection lands.
 */
bool scaleSeeding(std::string& detail) {
    ScaleDetect detector;
    auto r = detector.detect();
    if (r.detected || r.root != 0 || r.mode != Mode::Major) {
        detail = "a fresh detector reported " + keyName(r) + " rather than the C major seed";
        return false;
    }

    // Changing the manual setting before any detection must take effect.
    detector.setSeed(7, Mode::Minor);
    r = detector.detect();
    if (r.detected || r.root != 7 || r.mode != Mode::Minor) {
        detail = "the seed did not follow the manual setting; got " + keyName(r);
        return false;
    }

    // Once a real key is detected the seed no longer overrides it.
    feedKey(detector, 62, Mode::Major);  // D major
    r = detector.detect();
    if (!r.detected || r.root != 2 || r.mode != Mode::Major) {
        detail = "expected D major after feeding it; got " + keyName(r);
        return false;
    }
    detector.setSeed(11, Mode::Minor);
    r = detector.detect();
    if (r.root != 2 || r.mode != Mode::Major) {
        detail = "the manual seed overrode a live detection; got " + keyName(r);
        return false;
    }

    // reset() returns to the seed, which is by then B minor.
    detector.reset();
    r = detector.detect();
    if (r.detected || r.root != 11 || r.mode != Mode::Minor) {
        detail = "reset did not return to the seed; got " + keyName(r);
        return false;
    }
    return true;
}

/**
 * With decay, a key change in the material is followed. Without it, the first
 * thing recorded outvotes everything after it forever.
 */
bool scaleFollowsAKeyChange(std::string& detail) {
    ScaleDetect withDecay;
    withDecay.setDecay(0.99f);
    ScaleDetect withoutDecay;
    withoutDecay.setDecay(1.f);

    // A LONG stretch of C major, so that without decay it cannot be outvoted.
    for (auto* d : {&withDecay, &withoutDecay}) feedKey(*d, 60, Mode::Major, 40);
    if (withDecay.detect().root != 0 || withoutDecay.detect().root != 0) {
        detail = "setup failed: neither detector started in C major";
        return false;
    }

    // Then a much SHORTER stretch of F sharp major, the furthest key away.
    for (auto* d : {&withDecay, &withoutDecay}) feedKey(*d, 66, Mode::Major, 12);

    const auto moved = withDecay.detect();
    if (moved.root != 6 || moved.mode != Mode::Major) {
        detail = "with decay the key did not follow the material; got " + keyName(moved);
        return false;
    }

    // The contrast is the point. Without decay the opening outvotes everything
    // that follows it, however long the module runs, so a test that only checked
    // the decaying detector would pass with the decay removed entirely.
    const auto stuck = withoutDecay.detect();
    if (stuck.root != 0 || stuck.mode != Mode::Major) {
        detail = "without decay the key still moved, to " + keyName(stuck) +
                 "; this test no longer isolates the decay";
        return false;
    }
    return true;
}

/**
 * While holding, the result must say it is holding.
 *
 * The spec requires the UI to show that analysis is inactive when a percussive
 * stem is selected. Returning the stored result unchanged kept reporting the
 * confidence and the detected flag from whenever the key was last found, so a
 * caller could not tell a live detection from one made minutes ago on entirely
 * different material.
 */
bool scaleReportsInactivity(std::string& detail) {
    ScaleDetect detector;
    feedKey(detector, 62, Mode::Major);  // D major
    const auto live = detector.detect();
    if (!live.detected || live.root != 2) {
        detail = "setup failed: expected a live D major, got " + keyDetail(live);
        return false;
    }

    // Switch to a percussive stem: everything below the voicing gate.
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> freq(60.f, 3000.f);
    std::uniform_real_distribution<float> conf(0.f, 0.5f);
    for (int i = 0; i < 500; i++) detector.addPitch(freq(rng), conf(rng));

    const auto held = detector.detect();
    if (held.root != 2 || held.mode != Mode::Major) {
        detail = "the held key changed: " + keyDetail(held);
        return false;
    }
    if (held.detected) {
        detail = "a held result still reported detected=true: " + keyDetail(held);
        return false;
    }
    return true;
}

/**
 * The evidence gate must stay reachable however the decay is set.
 *
 * The histogram is a decaying accumulator, so the total weight is bounded above
 * by 1 / (1 - decay). At decay 0.875 that ceiling is exactly the default
 * minimum of 8, and below it the gate can never open however many pitches
 * arrive, leaving the detector on its seed forever.
 */
bool scaleDecayGateIsReachable(std::string& detail) {
    // Decay is floored at 0.9, because below that the histogram remembers fewer
    // than ten observations and cannot hold a scale's worth of distinct pitch
    // classes at all. Check the floor is applied rather than silently accepting
    // a setting that would make the detector useless.
    {
        ScaleDetect floored;
        floored.setDecay(0.1f);
        feedKey(floored, 60, Mode::Major, 40);
        const auto r = floored.detect();
        if (!r.detected || r.root != 0) {
            detail = "an absurdly low decay was not floored: " + keyDetail(r) +
                     ", weight " + std::to_string(floored.totalWeight());
            return false;
        }
    }

    for (float decay : {0.9f, 0.95f, 0.99f, 0.999f, 1.f}) {
        ScaleDetect detector;
        detector.setDecay(decay);
        // Plenty of material, well past any steady state.
        feedKey(detector, 60, Mode::Major, 40);
        const auto r = detector.detect();
        if (!r.detected) {
            detail = "at decay " + std::to_string(decay) +
                     " a long stretch of C major never opened the gate: " + keyDetail(r) +
                     ", weight " + std::to_string(detector.totalWeight());
            return false;
        }
        if (r.root != 0 || r.mode != Mode::Major) {
            detail = "at decay " + std::to_string(decay) + " detected " + keyDetail(r);
            return false;
        }
    }
    return true;
}

/**
 * A sustained note is not a scale.
 *
 * The weight gate counts observations and says nothing about whether they
 * contain enough distinct pitches to imply a key. Eight observations of ONE
 * note reach the default weight and correlate at about 0.68 with that note's
 * major key, which is enough to replace a real key on no evidence for either a
 * root or a mode. A repeatedly latched transient does the same.
 */
bool scaleSustainedNoteIsNotAKey(std::string& detail) {
    // A single pitch, from a fresh detector.
    for (int midi : {60, 63, 67, 70}) {
        ScaleDetect detector;
        detector.setSeed(5, Mode::Minor);
        for (int i = 0; i < 40; i++) detector.addPitch(midiHz(midi), 1.f);
        const auto r = detector.detect();
        if (r.detected) {
            detail = "a single sustained pitch declared " + keyDetail(r);
            return false;
        }
        if (r.root != 5 || r.mode != Mode::Minor) {
            detail = "a single sustained pitch did not hold the seed: " + keyDetail(r);
            return false;
        }
    }

    // And it must not displace an established key either.
    ScaleDetect detector;
    feedKey(detector, 60, Mode::Major);
    const auto established = detector.detect();
    for (int i = 0; i < 400; i++) detector.addPitch(midiHz(66), 1.f);
    const auto after = detector.detect();
    if (after.root != established.root || after.mode != established.mode) {
        detail = "a sustained pitch displaced " + keyName(established) + " with " +
                 keyDetail(after);
        return false;
    }

    // A root and fifth is still not a key. Two classes score 0.28 spread.
    ScaleDetect pair;
    for (int i = 0; i < 40; i++) {
        pair.addPitch(midiHz(60), 1.f);
        pair.addPitch(midiHz(67), 1.f);
    }
    if (pair.detect().detected) {
        detail = "a root and fifth declared " + keyDetail(pair.detect());
        return false;
    }

    // A triad IS enough evidence to offer a key, so the guard must not be so
    // strict that ordinary material is rejected.
    ScaleDetect triad;
    for (int i = 0; i < 40; i++) {
        triad.addPitch(midiHz(60), 1.f);
        triad.addPitch(midiHz(64), 1.f);
        triad.addPitch(midiHz(67), 1.f);
    }
    if (!triad.detect().detected) {
        detail = "a C major triad was rejected: " + keyDetail(triad.detect()) +
                 ", spread " + std::to_string(triad.spread());
        return false;
    }
    return true;
}

/**
 * The pitch gate must agree with YIN rather than guess at it.
 *
 * YIN marks a lag voiced when its CMNDF falls below its threshold and reports
 * confidence as 1 - CMNDF, so at the default 0.12 the equivalent cutoff is
 * 0.88. A lower cutoff here accepts estimates YIN itself classified as
 * unvoiced, which is how moderately periodic percussion accumulates enough
 * weight to replace a real key.
 */
bool scaleGateMatchesYin(std::string& detail) {
    const int sampleRate = 48000;
    std::mt19937 rng(31337);
    std::uniform_real_distribution<float> nz(-1.f, 1.f);

    Yin yin(4096);
    yin.setSampleRate(sampleRate);
    yin.setFrequencyRange(50.f, 2200.f);

    // Material that lands in the awkward band. White noise alone will not do:
    // it scores under 0.07, so a cutoff of 0.5 rejects it too and the test would
    // pass whatever the threshold was. These are noisy tones and damped
    // percussive hits, which YIN marks unvoiced while still reporting
    // confidence between 0.5 and 0.88, usually on the WRONG frequency.
    std::vector<std::vector<float>> windows;
    for (double noiseAmp : {0.6, 0.9, 1.2}) {
        std::vector<float> w(4096);
        for (std::size_t i = 0; i < w.size(); i++) {
            w[i] = (float)(std::sin(2 * M_PI * 220.0 * (double)i / sampleRate) +
                           noiseAmp * nz(rng));
        }
        windows.push_back(std::move(w));
    }
    for (double decay : {0.9990, 0.9995, 0.9998}) {
        std::vector<float> w(4096);
        double env = 1.0;
        for (std::size_t i = 0; i < w.size(); i++) {
            if (i % 400 == 0) env = 1.0;
            env *= decay;
            w[i] = (float)(env * (std::sin(2 * M_PI * 180.0 * (double)i / sampleRate) +
                                  0.7 * nz(rng)));
        }
        windows.push_back(std::move(w));
    }

    ScaleDetect viaFlag;
    ScaleDetect viaConfidence;
    int inBand = 0;
    for (int rep = 0; rep < 10; rep++) {
        for (const auto& w : windows) {
            const auto r = yin.analyse(w.data(), w.size());
            if (!r.voiced && r.confidence > 0.5f) inBand++;
            viaFlag.addPitch(r.frequency, r.confidence, r.voiced);
            viaConfidence.addPitch(r.frequency, r.confidence);
        }
    }
    if (inBand == 0) {
        detail = "setup failed: no unvoiced results above confidence 0.5 to test with";
        return false;
    }
    if (viaFlag.totalWeight() != 0.0) {
        detail = "the voiced flag let " + std::to_string(viaFlag.totalWeight()) +
                 " of unpitched material through";
        return false;
    }
    if (viaConfidence.totalWeight() != 0.0) {
        detail = "the default confidence cutoff let " +
                 std::to_string(viaConfidence.totalWeight()) +
                 " of unpitched material through; it does not match YIN's voicing gate";
        return false;
    }

    // The flag must win even when the confidence would have passed, so a caller
    // that changes YIN's threshold gets the right behaviour without changing
    // anything here.
    ScaleDetect flagged;
    for (int i = 0; i < 40; i++) flagged.addPitch(midiHz(60 + (i % 7)), 0.99f, false);
    if (flagged.totalWeight() != 0.0) {
        detail = "voiced=false was ignored for a high-confidence pitch";
        return false;
    }
    return true;
}

/** A flat histogram has no key and must not produce a NaN that wins. */
bool scaleFlatHistogram(std::string& detail) {
    ScaleDetect detector;
    detector.setSeed(3, Mode::Minor);
    // Every pitch class equally weighted: zero variance, no tonal centre.
    for (int rep = 0; rep < 4; rep++) {
        for (int pc = 0; pc < 12; pc++) detector.addPitch(midiHz(60 + pc), 1.f);
    }
    const auto r = detector.detect();
    if (!std::isfinite(r.confidence)) {
        detail = "a flat histogram produced a non-finite confidence";
        return false;
    }
    if (r.detected) {
        detail = "a flat histogram detected " + keyName(r) + " at confidence " +
                 std::to_string(r.confidence);
        return false;
    }
    if (r.root != 3 || r.mode != Mode::Minor) {
        detail = "a flat histogram did not hold the seed; got " + keyName(r);
        return false;
    }
    return true;
}

/** Non-finite and out-of-range input must be ignored, not accumulated. */
bool scaleBadInput(std::string& detail) {
    ScaleDetect detector;
    feedKey(detector, 60, Mode::Major);
    const auto before = detector.detect();
    const double weightBefore = detector.totalWeight();

    // A bad FREQUENCY must contribute nothing at all.
    const float badHz[] = {std::nanf(""), std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), 0.f, -100.f, 1e9f};
    for (float f : badHz) detector.addPitch(f, 1.f);
    if (detector.totalWeight() != weightBefore) {
        detail = "a bad frequency changed the accumulated weight from " +
                 std::to_string(weightBefore) + " to " +
                 std::to_string(detector.totalWeight());
        return false;
    }

    // A non-finite or negative CONFIDENCE must contribute nothing either.
    const float badConf[] = {std::nanf(""), -std::numeric_limits<float>::infinity(),
                             -1.f, 0.f};
    for (float c : badConf) detector.addPitch(440.f, c);
    if (detector.totalWeight() != weightBefore) {
        detail = "a bad confidence changed the accumulated weight from " +
                 std::to_string(weightBefore) + " to " +
                 std::to_string(detector.totalWeight());
        return false;
    }

    // An absurdly large confidence must be CLAMPED, not trusted. Untrusted, one
    // such call swamps every other bin and pins the key to that single pitch.
    const int hugeCalls = 5;
    for (int i = 0; i < hugeCalls; i++) detector.addPitch(midiHz(61), 1e9f);
    const double grew = detector.totalWeight() - weightBefore;
    if (grew > (double)hugeCalls + 1e-6) {
        detail = "five calls at confidence 1e9 added " + std::to_string(grew) +
                 " to the weight";
        return false;
    }
    if (detector.detect().root == 1) {
        detail = "a single swamping pitch decided the key";
        return false;
    }

    // Non-finite settings must be rejected rather than clamped, for the same
    // reason as in Yin: std::min and std::max propagate NaN.
    detector.setPitchConfidenceThreshold(std::nanf(""));
    detector.setKeyConfidenceThreshold(std::nanf(""));
    detector.setMinimumWeight(std::nanf(""));
    detector.setDecay(std::nanf(""));

    const auto after = detector.detect();
    if (!std::isfinite(after.confidence) || after.root != before.root ||
        after.mode != before.mode) {
        detail = "non-finite settings disturbed the result: " + keyName(before) + " became " +
                 keyName(after);
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Quantizer
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Quantizer;

namespace {
using QScale = Quantizer::Scale;

/** True when @p volts lands on a degree of @p scale rooted at @p root. */
bool isScaleDegree(double volts, int root, QScale scale) {
    const double semitones = volts * 12.0;
    const double nearest = std::round(semitones);
    if (std::fabs(semitones - nearest) > 1e-4) return false;   // not even a semitone
    int pitchClass = (int)std::fmod(nearest - root, 12.0);
    if (pitchClass < 0) pitchClass += 12;
    return (Quantizer::scaleMask(scale) & (1u << pitchClass)) != 0;
}

Quantizer makeQuantizer(int root, QScale scale, float glide = 0.f) {
    Quantizer q(48000);
    q.setManualOverride(true);
    q.setManualKey(root, scale);
    q.setGlideSeconds(glide);
    return q;
}
}  // namespace

/**
 * A slow ramp must give a monotonic staircase restricted to scale degrees.
 *
 * Monotonicity is the criterion that catches a wandering tie-break. Every
 * non-scale semitone in a seven-note scale sits exactly between two degrees, so
 * an implementation that resolves those ties by remembering what it chose last
 * time steps backwards at some boundaries, which is an audible wrong note.
 */
bool quantStaircase(std::string& detail) {
    const struct { int root; QScale scale; } keys[] = {
        {0, QScale::Major}, {7, QScale::NaturalMinor}, {3, QScale::Dorian},
        {10, QScale::PentatonicMinor}, {5, QScale::Blues},
    };
    for (const auto& k : keys) {
        Quantizer q = makeQuantizer(k.root, k.scale);
        double previous = -1e9;
        int steps = 0;
        // Five octaves, finely enough to land either side of every boundary.
        for (int i = 0; i <= 60000; i++) {
            const float in = -2.5f + (float)i * (5.0f / 60000.f);
            const double out = q.process(in);
            if (out < previous - 1e-9) {
                detail = std::string("output stepped backwards in ") +
                         Quantizer::scaleName(k.scale) + " at input " +
                         std::to_string(in) + ": " + std::to_string(previous) +
                         " then " + std::to_string(out);
                return false;
            }
            if (out > previous + 1e-9) steps++;
            if (!isScaleDegree(out, k.root, k.scale)) {
                detail = std::to_string(out) + " V is not a degree of " +
                         Quantizer::scaleName(k.scale);
                return false;
            }
            previous = out;
        }
        if (steps < 10) {
            detail = "only " + std::to_string(steps) + " steps over five octaves of " +
                     Quantizer::scaleName(k.scale) + "; the ramp is not being quantised";
            return false;
        }
    }
    return true;
}

/**
 * The same degree an octave apart must differ by one volt.
 *
 * "Exactly" has a floor, and it is the output type rather than the arithmetic.
 * process() returns float because that is what a CV output is, and float
 * rounding at these magnitudes is about 1.2e-7 V, which is 1.4e-5 cents. The
 * internal degrees really are exactly twelve semitones apart; it is the cast
 * that loses it. A tighter tolerance here would be asserting something the
 * signal type cannot deliver.
 *
 * This does mean the choice to compute in double is NOT observable through this
 * interface: float accumulation would leave 2.98e-8 V, smaller than the output
 * rounding that follows it. The double is there to keep the glide state clean
 * over long runs, not to widen the output precision.
 */
bool quantOctaveExact(std::string& detail) {
    for (int root = 0; root < 12; root++) {
        Quantizer q = makeQuantizer(root, QScale::Major);
        for (int octave = -4; octave < 4; octave++) {
            const double a = q.process((float)octave + (float)root / 12.f);
            const double b = q.process((float)(octave + 1) + (float)root / 12.f);
            const double interval = b - a;
            if (std::fabs(interval - 1.0) > 1e-6) {
                detail = "octave interval at root " + std::to_string(root) +
                         " octave " + std::to_string(octave) + " was " +
                         std::to_string(interval);
                return false;
            }
        }
    }
    return true;
}

/** Zero glide must arrive in one sample, not merely quickly. */
bool quantGlideZero(std::string& detail) {
    Quantizer q = makeQuantizer(0, QScale::Major, 0.f);
    q.process(0.f);
    const double jumped = q.process(1.f);
    if (jumped != 1.0) {
        detail = "with zero glide a one octave jump landed on " + std::to_string(jumped);
        return false;
    }
    // And the result must be an exact scale degree, not a near miss. An
    // exponential approach never truly arrives, so a "very short" glide leaves
    // the output a fraction of a cent flat forever and the 1 V/octave guarantee
    // quietly fails.
    for (int i = 0; i < 100; i++) {
        const double v = q.process(0.4f);
        if (v != q.target()) {
            detail = "zero glide did not sit exactly on the target";
            return false;
        }
    }
    return true;
}

/**
 * A configured glide must take about that long, and must arrive exactly.
 */
bool quantGlideTiming(std::string& detail) {
    for (float glide : {0.05f, 0.2f, 1.0f}) {
        Quantizer q = makeQuantizer(0, QScale::Major, glide);
        q.process(0.f);
        q.snapToTarget();

        const int samples = (int)(glide * 48000);
        double atQuarter = 0.0, atFull = 0.0;
        for (int i = 0; i < samples; i++) {
            const double v = q.process(1.f);
            if (i == samples / 4) atQuarter = v;
            atFull = v;
        }
        // Most of the way after the full time, and clearly not there yet at a
        // quarter of it. Both halves matter: without the second, a glide that
        // was really instantaneous would pass.
        if (atFull < 0.99) {
            detail = "a " + std::to_string(glide) + " s glide reached only " +
                     std::to_string(atFull) + " of one octave";
            return false;
        }
        if (atQuarter > 0.95) {
            detail = "a " + std::to_string(glide) + " s glide was already at " +
                     std::to_string(atQuarter) + " after a quarter of its time";
            return false;
        }

        // It must ARRIVE, within a bounded multiple of its own time, not merely
        // converge.
        //
        // Asserting only that it lands on the target eventually is not a test:
        // a bare exponential reaches it anyway once the increment falls below an
        // ULP, so "run for three seconds then check" passes with the settle snap
        // removed. The snap puts arrival at 2.34 times the glide time whatever
        // the glide is set to; without it, arrival depends on where the
        // increment underflows and is never sooner than 3 times. Two and a half
        // is the bound that separates them.
        const int bound = (int)(glide * 48000 * 2.5) - samples;
        for (int i = 0; i < bound; i++) q.process(1.f);
        const double arrived = q.process(1.f);
        if (arrived != 1.0) {
            detail = "a " + std::to_string(glide) +
                     " s glide had not arrived after 2.5 times its time; short by " +
                     std::to_string(1.0 - arrived);
            return false;
        }
    }
    return true;
}

/** Glide time is in seconds, so it must not change with the sample rate. */
bool quantGlideSampleRate(std::string& detail) {
    auto timeToReach = [](int sampleRate) {
        Quantizer q(sampleRate);
        q.setManualOverride(true);
        q.setManualKey(0, QScale::Major);
        q.setGlideSeconds(0.2f);
        q.process(0.f);
        q.snapToTarget();
        int n = 0;
        while (n < sampleRate * 5) {
            if (q.process(1.f) >= 0.99) break;
            n++;
        }
        return (double)n / sampleRate;
    };
    const double at48 = timeToReach(48000);
    const double at96 = timeToReach(96000);
    if (at48 <= 0.0) { detail = "glide completed instantly at 48 kHz"; return false; }
    const double ratio = at96 / at48;
    if (ratio < 0.9 || ratio > 1.1) {
        detail = "glide took " + std::to_string(at48) + " s at 48 kHz and " +
                 std::to_string(at96) + " s at 96 kHz";
        return false;
    }
    return true;
}

/** Manual override must win over whatever the detector reports. */
bool quantManualOverride(std::string& detail) {
    Quantizer q(48000);
    q.setDetectedKey(0, QScale::Major);       // C major from the detector
    q.setManualKey(6, QScale::PentatonicMinor);  // F sharp pentatonic minor by hand

    // Auto mode: the detector wins.
    q.setManualOverride(false);
    if (q.activeRoot() != 0 || q.activeScale() != QScale::Major) {
        detail = "auto mode did not follow the detector";
        return false;
    }
    for (int i = 0; i <= 240; i++) {
        const double out = q.process(-1.f + (float)i / 120.f);
        if (!isScaleDegree(out, 0, QScale::Major)) {
            detail = "auto mode produced " + std::to_string(out) + " V, not a C major degree";
            return false;
        }
    }

    // Manual mode: the override wins, and keeps winning when the detector
    // changes its mind underneath it.
    q.setManualOverride(true);
    for (int i = 0; i <= 240; i++) {
        q.setDetectedKey(i % 12, QScale::Major);  // detector thrashing
        const double out = q.process(-1.f + (float)i / 120.f);
        if (!isScaleDegree(out, 6, QScale::PentatonicMinor)) {
            detail = "manual mode produced " + std::to_string(out) +
                     " V, not an F# pentatonic minor degree";
            return false;
        }
    }
    return true;
}

/** Every scale must emit only its own degrees, in every key. */
bool quantAllScales(std::string& detail) {
    for (int s = 0; s < Quantizer::kNumScales; s++) {
        const QScale scale = static_cast<QScale>(s);
        if (Quantizer::scaleMask(scale) == 0) {
            detail = std::string(Quantizer::scaleName(scale)) + " has an empty mask";
            return false;
        }
        for (int root = 0; root < 12; root++) {
            Quantizer q = makeQuantizer(root, scale);
            for (int i = 0; i <= 600; i++) {
                const float in = -2.f + (float)i / 150.f;
                const double out = q.process(in);
                if (!isScaleDegree(out, root, scale)) {
                    detail = std::string(Quantizer::scaleName(scale)) + " at root " +
                             std::to_string(root) + " produced " + std::to_string(out) + " V";
                    return false;
                }
                // Never further than half the largest gap in any scale here.
                if (std::fabs(out - in) > 3.0 / 12.0 + 1e-6) {
                    detail = std::string(Quantizer::scaleName(scale)) + " moved " +
                             std::to_string(in) + " V to " + std::to_string(out) + " V";
                    return false;
                }
            }
        }
    }
    return true;
}

/**
 * Quantisation must depend only on the input, never on what came before.
 *
 * This is what makes the staircase monotonic. An implementation that resolves
 * ties by preferring the degree it chose last time gives a different answer for
 * the same input depending on the approach direction, and the ramp then steps
 * backwards.
 */
bool quantIsStateless(std::string& detail) {
    Quantizer rising = makeQuantizer(0, QScale::Major);
    Quantizer falling = makeQuantizer(0, QScale::Major);
    Quantizer jumping = makeQuantizer(0, QScale::Major);

    const int n = 2000;
    std::vector<double> up(n), down(n), jump(n);
    for (int i = 0; i < n; i++) up[i] = rising.process(-1.f + (float)i / 1000.f);
    for (int i = n - 1; i >= 0; i--) down[i] = falling.process(-1.f + (float)i / 1000.f);
    std::mt19937 rng(7);
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);
    for (int idx : order) jump[idx] = jumping.process(-1.f + (float)idx / 1000.f);

    for (int i = 0; i < n; i++) {
        if (up[i] != down[i] || up[i] != jump[i]) {
            detail = "the same input gave different results by approach direction at index " +
                     std::to_string(i) + ": rising " + std::to_string(up[i]) + ", falling " +
                     std::to_string(down[i]) + ", random " + std::to_string(jump[i]);
            return false;
        }
    }
    return true;
}

/**
 * Large but finite input must be bounded, not just non-finite input.
 *
 * Rejecting NaN and infinity is not enough. A value like 1e20 or FLT_MAX from a
 * misbehaving upstream module puts floor(semitones / 12) outside long long, and
 * converting it is undefined; the octave index either side of it overflows too.
 * That is three separate UndefinedBehaviorSanitizer reports, and in a release
 * build it silently produced 0 V, which is a note rather than a refusal.
 *
 * The earlier non-finite test passed against all of this, and so did every
 * sanitiser run, because nothing fed a large finite value.
 */
bool quantExtremeInput(std::string& detail) {
    Quantizer q = makeQuantizer(0, QScale::Major);
    const float extremes[] = {1e20f, 1e30f, std::numeric_limits<float>::max(),
                              -1e20f, -std::numeric_limits<float>::max(),
                              1e9f, -1e9f, 100.f, -100.f};
    for (float v : extremes) {
        const double out = q.process(v);
        if (!std::isfinite(out)) {
            detail = "input " + std::to_string(v) + " gave a non-finite output";
            return false;
        }
        // Bounded, and on the correct side of zero rather than collapsing to it.
        if (std::fabs(out) > 20.0 + 1e-6) {
            detail = "input " + std::to_string(v) + " gave " + std::to_string(out) +
                     " V, outside the clamped range";
            return false;
        }
        if (v > 1.f && out <= 0.0) {
            detail = "a large positive input gave " + std::to_string(out) + " V";
            return false;
        }
        if (v < -1.f && out >= 0.0) {
            detail = "a large negative input gave " + std::to_string(out) + " V";
            return false;
        }
        if (!isScaleDegree(out, 0, QScale::Major)) {
            detail = "input " + std::to_string(v) + " gave " + std::to_string(out) +
                     " V, not a C major degree";
            return false;
        }
    }
    return true;
}

/** Non-finite input must not corrupt the output or the glide state. */
bool quantNonFinite(std::string& detail) {
    Quantizer q = makeQuantizer(0, QScale::Major, 0.1f);
    q.process(0.5f);
    q.snapToTarget();
    const double before = q.target();

    for (float bad : {std::nanf(""), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
        const double out = q.process(bad);
        if (!std::isfinite(out)) {
            detail = "a non-finite input produced a non-finite output";
            return false;
        }
        if (q.target() != before) {
            detail = "a non-finite input moved the target from " + std::to_string(before) +
                     " to " + std::to_string(q.target());
            return false;
        }
    }

    // Non-finite settings must be rejected rather than clamped.
    q.setGlideSeconds(std::nanf(""));
    const double out = q.process(0.5f);
    if (!std::isfinite(out)) {
        detail = "a non-finite glide setting produced a non-finite output";
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Cross-module extreme input sweep
// ---------------------------------------------------------------------------

/**
 * Feed every public entry point values that are hostile but legal.
 *
 * Written after review found that Quantizer rejected NaN and infinity but not
 * large FINITE input: 1e20 volts put floor(semitones / 12) outside long long,
 * and converting it is undefined. Three UndefinedBehaviorSanitizer reports, and
 * a release build silently produced 0 V.
 *
 * The gap was not that the guard was missing, it was that nothing in the suite
 * ever supplied such a value, so every sanitiser run came back clean. This
 * sweep exists so the whole class stays covered rather than the one instance
 * that was found. It asserts little on its own; its value is being run under
 * AddressSanitizer and UndefinedBehaviorSanitizer, where the failure is the
 * sanitiser aborting.
 */
bool extremeInputSweep(std::string& detail) {
    const float ext[] = {1e20f, 1e30f, std::numeric_limits<float>::max(),
                         -std::numeric_limits<float>::max(), -1e20f, 1e9f, -1e9f,
                         std::nanf(""), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), 0.f, -0.f};
    const double dext[] = {1e20, 1e300, -1e20, 1e18, -1e18, std::nan(""),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()};

    {
        RingBuffer buffer(48000, 0.25f, 2);
        for (float v : ext) buffer.write(v, v);
        float l = 0.f, r = 0.f;
        for (double p : dext) buffer.readFrameInterpolated(p, l, r);
        buffer.readFrame(0, l, r);
        if (!std::isfinite(l) && buffer.framesStored() == 0) {
            detail = "RingBuffer returned a non-finite sample from an empty buffer";
            return false;
        }
    }

    {
        Transport transport(48000);
        transport.setBufferFrames(48000);
        for (float v : ext) {
            transport.setClockDivision(v);
            transport.setLoopBounds(v, v);
            transport.process(v, v);
            if (!std::isfinite(transport.playheadFrames())) {
                detail = "Transport produced a non-finite playhead";
                return false;
            }
        }
        for (double p : dext) transport.setPhaseForTest(p);
    }

    {
        ScaleDetect detector;
        for (float v : ext) {
            detector.setDecay(v);
            detector.setMinimumWeight(v);
            detector.setMinimumSpread(v);
            detector.setKeyConfidenceThreshold(v);
            detector.setPitchConfidenceThreshold(v);
            detector.setMinimumActivity(v);
            detector.addPitch(v, v);
            const auto res = detector.detect();
            if (!std::isfinite(res.confidence)) {
                detail = "ScaleDetect produced a non-finite confidence";
                return false;
            }
        }
    }

    {
        Quantizer q(48000);
        q.setManualOverride(true);
        for (float v : ext) {
            q.setGlideSeconds(v);
            if (!std::isfinite(q.process(v))) {
                detail = "Quantizer produced a non-finite output";
                return false;
            }
        }
    }

    {
        Yin yin(1024);
        yin.setSampleRate(48000);
        for (float v : ext) {
            yin.setThreshold(v);
            yin.setFrequencyRange(v, v);
        }
        std::vector<float> w(1024);
        for (float v : ext) {
            for (auto& x : w) x = v;
            const auto r = yin.analyse(w.data(), w.size());
            if (!std::isfinite(r.frequency) || !std::isfinite(r.confidence)) {
                detail = "Yin produced a non-finite result";
                return false;
            }
        }
    }

    {
        StemSet set;
        set.channels = 2;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(512, 0.5f);
            set.layer[L].channel[1].assign(512, -0.5f);
        }
        StemMixer mixer(48000);
        mixer.snapToTargets(true);
        for (float v : ext) {
            mixer.setLevel(0, v);
            mixer.setStemSelect((v > 0.f) ? 3 : 0);
        }
        for (double p : dext) {
            const auto f = mixer.process(&set, p, 1.f, 1.f);
            if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
                detail = "StemMixer produced a non-finite frame";
                return false;
            }
        }
    }

    {
        ReferenceFft fft(256);
        Hpss hpss(fft);
        for (float v : ext) {
            hpss.setMargin(v);
            hpss.setPower(v);
            hpss.setLowSplitHz(v);
        }
        std::vector<float> in(1024, 0.f);
        Hpss::Result out;
        for (float v : ext) {
            for (auto& x : in) x = v;
            hpss.separate(in.data(), in.size(), 48000, out);
        }
    }

    return true;
}


// ---------------------------------------------------------------------------
// WavetableExtract
// ---------------------------------------------------------------------------

using WiggleRoom::stems::WavetableExtract;

namespace {
/** A stem set whose layers all hold @p hz, at 48 kHz. */
StemSet toneSet(double hz, std::size_t frames = 48000, uint64_t generation = 1) {
    StemSet set;
    set.channels = 1;
    set.generation = generation;
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        set.layer[L].channel[0].assign(frames, 0.f);
        for (std::size_t i = 0; i < frames; i++) {
            set.layer[L].channel[0][i] =
                (float)std::sin(2 * M_PI * hz * (double)i / 48000.0);
        }
    }
    return set;
}

/** Build one complete frame, returning how many calls it took. */
int buildFrame(WavetableExtract& e, const StemSet& set, int layer, double playhead,
               std::size_t* worstCall = nullptr) {
    int calls = 0;
    if (worstCall) *worstCall = 0;
    while (calls < 100000) {
        const bool done = e.process(&set, layer, playhead);
        // WORK, not output samples. At a wide window one output sample costs
        // several source reads, so equal sample counts hide unequal work.
        if (worstCall) *worstCall = std::max(*worstCall, e.debugWorkLastCall());
        calls++;
        if (done) break;
    }
    return calls;
}

double framePeak(const WavetableExtract& e) {
    double peak = 0.0;
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        peak = std::max(peak, std::fabs((double)e.frame()[i]));
    }
    return peak;
}
}  // namespace

/**
 * The frame is the same length whatever wt_window is set to.
 *
 * This is what makes wt_window change how much source material is captured
 * rather than the oscillator's pitch: the fundamental is set by how fast the
 * oscillator reads a frame, so a frame whose length moved with the window would
 * retune the voice every time the control was touched.
 */
bool wtFrameSizeIsFixed(std::string& detail) {
    const auto set = toneSet(100.0);
    for (int window : {256, 400, 512, 1024, 2048, 3000, 4096, 8192}) {
        WavetableExtract e;
        e.setWindowSamples(window);
        buildFrame(e, set, 0, 24000.0);
        if (e.frameSize() != WavetableExtract::kFrameSize) {
            detail = "window " + std::to_string(window) + " gave a frame of " +
                     std::to_string(e.frameSize());
            return false;
        }
        // And the frame must actually be populated, not left at zero.
        if (framePeak(e) < 0.9) {
            detail = "window " + std::to_string(window) + " produced a frame peaking at " +
                     std::to_string(framePeak(e));
            return false;
        }
    }
    return true;
}

/**
 * wt_window must change the captured material, or the control does nothing.
 *
 * The companion to the test above: holding the frame length fixed is only half
 * the requirement. A longer window has to capture more of the source, which
 * shows up as more cycles of a fixed tone inside the frame.
 */
bool wtWindowChangesContent(std::string& detail) {
    const auto set = toneSet(100.0);  // 480 samples per cycle
    int previousCrossings = -1;
    for (int window : {512, 1024, 2048, 4096, 8192}) {
        WavetableExtract e;
        e.setWindowSamples(window);
        buildFrame(e, set, 0, 24000.0);

        int crossings = 0;
        for (std::size_t i = 1; i < e.frameSize(); i++) {
            if ((e.frame()[i - 1] < 0.f) != (e.frame()[i] < 0.f)) crossings++;
        }
        if (previousCrossings >= 0 && crossings <= previousCrossings) {
            detail = "window " + std::to_string(window) + " captured " +
                     std::to_string(crossings) + " zero crossings, no more than the " +
                     std::to_string(previousCrossings) + " of the shorter window";
            return false;
        }
        previousCrossings = crossings;
    }
    return true;
}

/**
 * The work must be spread evenly, never concentrated in one call.
 *
 * Building a whole frame in the call where the playhead crosses a boundary is a
 * spike, and the spike lands on the audio thread.
 */
bool wtWorkIsAmortised(std::string& detail) {
    const auto set = toneSet(100.0);
    // Windows spanning the whole range, because the cost of one output sample
    // depends on the window: at 8192 each costs four source reads. Charging
    // only output samples let the reading phase do four times the work of the
    // finalising phase while both reported the same number, so the cost still
    // jumped at the boundary between them.
    for (int window : {2048, 4096, 8192}) {
        for (int budget : {64, 256, 512}) {
            WavetableExtract e;
            e.setWindowSamples(window);
            e.setBudgetPerCall(budget);
            std::size_t worst = 0;
            const int calls = buildFrame(e, set, 0, 24000.0, &worst);

            if (worst > e.effectiveBudget()) {
                detail = "window " + std::to_string(window) + ", budget " +
                         std::to_string(budget) + ": a single call did " +
                         std::to_string(worst) + " units against an effective bound of " +
                         std::to_string(e.effectiveBudget());
                return false;
            }

            // Reading and finalising are BOTH amortised, so a frame costs
            // kFrameSize * (span + 1) units in total.
            const int span = (window + (int)WavetableExtract::kFrameSize - 1) /
                             (int)WavetableExtract::kFrameSize;
            const int units = (int)WavetableExtract::kFrameSize * (span + 1);
            const int expected = units / budget;
            if (calls < expected - 2 || calls > expected + 3) {
                detail = "window " + std::to_string(window) + ", budget " +
                         std::to_string(budget) + ": took " + std::to_string(calls) +
                         " calls, expected about " + std::to_string(expected);
                return false;
            }

            // Flat across several frames, not just the first.
            for (int frame = 0; frame < 4; frame++) {
                std::size_t w = 0;
                buildFrame(e, set, 0, 24000.0 + frame * 1000.0, &w);
                if ((int)w > budget) {
                    detail = "frame " + std::to_string(frame) + " spiked to " +
                             std::to_string(w) + " units";
                    return false;
                }
            }
        }
    }
    return true;
}

/**
 * The default budget must publish the default window in sixteen calls.
 *
 * A frame costs kFrameSize * (span + 1) units, reading then writing. Sizing the
 * default for the reading phase alone doubled the real latency: 32 calls rather
 * than 16, which at one call per 256 sample block is 171 ms instead of 85, and
 * that is the age of the snapshot before any morphing is applied.
 */
bool wtDefaultLatency(std::string& detail) {
    const auto set = toneSet(100.0);
    WavetableExtract e;   // defaults throughout
    const int calls = buildFrame(e, set, 0, 24000.0);
    if (calls < 14 || calls > 18) {
        detail = "the default configuration published after " + std::to_string(calls) +
                 " calls, not the sixteen documented";
        return false;
    }
    return true;
}

/**
 * A budget below the decimation span must raise the bound, not break it.
 *
 * A slot's source reads are averaged together, so a slot cannot be split across
 * calls and is the smallest indivisible unit of work. Advertising a bound of
 * one and then doing four reads is worse than admitting the floor.
 */
bool wtTinyBudget(std::string& detail) {
    const auto set = toneSet(100.0);
    for (int window : {2048, 4096, 8192}) {
        for (int budget : {1, 2, 3}) {
            WavetableExtract e;
            e.setWindowSamples(window);
            e.setBudgetPerCall(budget);
            const std::size_t span =
                (window + WavetableExtract::kFrameSize - 1) / WavetableExtract::kFrameSize;

            std::size_t worst = 0;
            const int calls = buildFrame(e, set, 0, 24000.0, &worst);
            if (e.effectiveBudget() != std::max<std::size_t>((std::size_t)budget, span)) {
                detail = "window " + std::to_string(window) + ", budget " +
                         std::to_string(budget) + ": effective bound reported as " +
                         std::to_string(e.effectiveBudget());
                return false;
            }
            if (worst > e.effectiveBudget()) {
                detail = "window " + std::to_string(window) + ", budget " +
                         std::to_string(budget) + ": did " + std::to_string(worst) +
                         " units against a bound of " + std::to_string(e.effectiveBudget());
                return false;
            }
            if (calls <= 0 || e.debugFramePeak() < 0.5) {
                detail = "a tiny budget failed to build a frame at all";
                return false;
            }
        }
    }
    return true;
}

/**
 * At a unit ratio no averaging should happen at all.
 *
 * Starting the slot at the mapped position rather than centring on it puts
 * every read half a step late, so at the default window each output sample was
 * the mean of two adjacent source samples. A stem alternating between +1 and -1
 * came out completely silent.
 */
bool wtUnitRatioAlignment(std::string& detail) {
    // The pathological case: full-scale content at exactly Nyquist.
    StemSet alternating;
    alternating.channels = 1;
    alternating.generation = 1;
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        alternating.layer[L].channel[0].assign(8000, 0.f);
        for (std::size_t i = 0; i < 8000; i++) {
            alternating.layer[L].channel[0][i] = (i % 2) ? 1.f : -1.f;
        }
    }
    WavetableExtract e;
    e.setWindowSamples((int)WavetableExtract::kFrameSize);
    buildFrame(e, alternating, 0, 4000.0);
    if (e.debugFramePeak() < 0.9) {
        detail = "an alternating stem at a unit ratio published a frame peaking at " +
                 std::to_string(e.debugFramePeak());
        return false;
    }

    // And ordinary high content must not be attenuated when nothing is being
    // resampled: at a unit ratio the frame should reproduce the source.
    const auto tone = toneSet(9000.0, 8000);
    WavetableExtract t;
    t.setWindowSamples((int)WavetableExtract::kFrameSize);
    buildFrame(t, tone, 0, 4000.0);
    if (t.debugSourcePeak() < 0.9) {
        detail = "a 9 kHz tone at a unit ratio measured a source peak of " +
                 std::to_string(t.debugSourcePeak());
        return false;
    }
    return true;
}

/**
 * The advertised work bound must follow the configured window at once.
 *
 * The span is only recalculated when a build begins, so reading it alone
 * reported the previous window's bound to anyone inspecting straight after
 * changing the setting, which is exactly when a caller looks.
 */
bool wtBudgetFollowsWindow(std::string& detail) {
    WavetableExtract e;
    e.setBudgetPerCall(1);
    e.setWindowSamples(8192);
    const std::size_t advertised = e.effectiveBudget();
    if (advertised < 4) {
        detail = "after setting an 8192 window, the bound was reported as " +
                 std::to_string(advertised) + " before any build";
        return false;
    }

    // And it must never under-report what a call then actually does.
    const auto set = toneSet(100.0);
    for (int window : {256, 2048, 4096, 8192, 512}) {
        e.setWindowSamples(window);
        const std::size_t claimed = e.effectiveBudget();
        std::size_t worst = 0;
        buildFrame(e, set, 0, 24000.0, &worst);
        if (worst > claimed) {
            detail = "window " + std::to_string(window) + ": claimed a bound of " +
                     std::to_string(claimed) + " then did " + std::to_string(worst);
            return false;
        }
    }
    return true;
}

/**
 * The edge taper must not put DC back.
 *
 * Removing the source mean makes the UNTAPERED window zero-mean, but
 * multiplying by a fade that is not symmetric about the content shifts it
 * again. The offset then eats headroom in the oscillator and the lowpass gate
 * and can click.
 */
bool wtTaperDc(std::string& detail) {
    // Zero-mean overall, but deliberately lopsided: the parts the taper
    // attenuates carry the opposite sign to the interior, so tapering cannot
    // leave the mean where it was.
    for (int flip = 0; flip < 2; flip++) {
        const std::size_t n = 8000;
        StemSet set;
        set.channels = 1;
        set.generation = 1;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(n, 0.f);
        }
        // The window that will be read at playhead 4000 with a 2048 window.
        const std::size_t first = 4000 - 1024;
        const std::size_t last = 4000 + 1024;
        const double edgeFraction = 0.05;
        const std::size_t edge = (std::size_t)((last - first) * edgeFraction);
        for (std::size_t i = first; i < last; i++) {
            const bool inEdge = (i < first + edge) || (i >= last - edge);
            float v = inEdge ? 1.f : 0.f;
            set.layer[0].channel[0][i] = flip ? -v : v;
        }
        // Balance it so the untapered window is exactly zero-mean.
        double sum = 0.0;
        for (std::size_t i = first; i < last; i++) sum += set.layer[0].channel[0][i];
        const double correction = sum / (double)((last - first) - 2 * edge);
        for (std::size_t i = first + edge; i < last - edge; i++) {
            set.layer[0].channel[0][i] -= (float)correction;
        }

        WavetableExtract e;
        buildFrame(e, set, 0, 4000.0);
        double mean = 0.0;
        for (std::size_t i = 0; i < e.frameSize(); i++) mean += e.frame()[i];
        mean /= (double)e.frameSize();
        if (std::fabs(mean) > 0.01) {
            detail = "a lopsided zero-mean window published a frame with DC of " +
                     std::to_string(mean);
            return false;
        }
    }
    return true;
}

/**
 * Stereo stems must contribute both channels.
 *
 * The voice is mono and there is no channel selector, so reading only the left
 * channel publishes a silent wavetable for a stem panned hard right and loses
 * half the timbre of every other stereo recording. The worker separates the two
 * sides independently, so the right channel really does carry different
 * material.
 */
bool wtStereoDownmix(std::string& detail) {
    const std::size_t n = 8000;

    // Hard right: left silent, right carrying the tone.
    StemSet hardRight;
    hardRight.channels = 2;
    hardRight.generation = 1;
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        hardRight.layer[L].channel[0].assign(n, 0.f);
        hardRight.layer[L].channel[1].assign(n, 0.f);
        for (std::size_t i = 0; i < n; i++) {
            hardRight.layer[L].channel[1][i] =
                (float)std::sin(2 * M_PI * 200.0 * (double)i / 48000.0);
        }
    }
    WavetableExtract e;
    buildFrame(e, hardRight, 0, 4000.0);
    if (e.debugFramePeak() < 0.5) {
        detail = "a stem panned hard right published a wavetable peaking at " +
                 std::to_string(e.debugFramePeak());
        return false;
    }

    // Two different tones, one per side: the frame must contain both.
    StemSet split;
    split.channels = 2;
    split.generation = 2;
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        split.layer[L].channel[0].assign(n, 0.f);
        split.layer[L].channel[1].assign(n, 0.f);
        for (std::size_t i = 0; i < n; i++) {
            split.layer[L].channel[0][i] =
                (float)std::sin(2 * M_PI * 150.0 * (double)i / 48000.0);
            split.layer[L].channel[1][i] =
                (float)std::sin(2 * M_PI * 950.0 * (double)i / 48000.0);
        }
    }
    WavetableExtract both;
    buildFrame(both, split, 0, 4000.0);

    // A left-only build of the same material differs, so the right channel is
    // genuinely present rather than merely not breaking anything.
    StemSet leftOnly = split;
    leftOnly.channels = 1;
    leftOnly.generation = 3;
    WavetableExtract single;
    buildFrame(single, leftOnly, 0, 4000.0);

    double worst = 0.0;
    for (std::size_t i = 0; i < both.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)both.frame()[i] - (double)single.frame()[i]));
    }
    if (worst < 0.05) {
        detail = "a stereo stem produced the same frame as its left channel alone; "
                 "the right channel is being ignored";
        return false;
    }

    // Mono sets, and stereo sets whose right channel is missing, must still work.
    const auto mono = toneSet(300.0, n);
    WavetableExtract m;
    buildFrame(m, mono, 0, 4000.0);
    if (m.debugFramePeak() < 0.5) {
        detail = "a mono stem published a wavetable peaking at " +
                 std::to_string(m.debugFramePeak());
        return false;
    }
    StemSet broken = split;
    broken.generation = 4;
    for (int L = 0; L < StemSet::kNumLayers; L++) broken.layer[L].channel[1].clear();
    WavetableExtract b;
    buildFrame(b, broken, 0, 4000.0);
    for (std::size_t i = 0; i < b.frameSize(); i++) {
        if (!std::isfinite(b.frame()[i])) {
            detail = "a stereo set with no right channel produced a non-finite frame";
            return false;
        }
    }
    return true;
}

/**
 * No frame may ever exceed full scale.
 *
 * The taper correction is subtracted after scaling, so it shifts every sample:
 * a window whose faded edge leans one way against an interior peak leaning the
 * other pushed that peak past unity, and downstream stages then clip content
 * this class claims to have normalised.
 */
bool wtNeverExceedsFullScale(std::string& detail) {
    // The pathological shape: positive content confined to the faded edge, an
    // opposite-polarity interior peak, and zero mean overall.
    {
        const std::size_t n = 8000;
        const std::size_t first = 4000 - 1024, last = 4000 + 1024;
        StemSet set;
        set.channels = 1;
        set.generation = 1;
        for (int L = 0; L < StemSet::kNumLayers; L++) set.layer[L].channel[0].assign(n, 0.f);

        const std::size_t edge = (last - first) / 20;
        for (std::size_t i = first; i < first + edge; i++) set.layer[0].channel[0][i] = 1.f;
        set.layer[0].channel[0][first + edge + 5] = 1.f;
        double sum = 0.0;
        for (std::size_t i = first; i < last; i++) sum += set.layer[0].channel[0][i];
        const std::size_t negatives = (last - first) - edge - 1;
        for (std::size_t i = last - negatives; i < last; i++) {
            set.layer[0].channel[0][i] -= (float)(sum / (double)negatives);
        }

        WavetableExtract e;
        buildFrame(e, set, 0, 4000.0);
        if (e.debugFramePeak() > 1.0 + 1e-6) {
            detail = "a lopsided window published a frame peaking at " +
                     std::to_string(e.debugFramePeak());
            return false;
        }
    }

    // And a broad sweep of ordinary material, at every window, must stay inside
    // full scale too.
    std::mt19937 rng(31415);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (int trial = 0; trial < 6; trial++) {
        StemSet set;
        set.channels = 1;
        set.generation = 10 + trial;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(20000, 0.f);
            for (auto& x : set.layer[L].channel[0]) x = dist(rng);
        }
        for (int window : {256, 1024, 2048, 4095, 8192}) {
            WavetableExtract e;
            e.setWindowSamples(window);
            for (double playhead : {0.0, 1000.0, 10000.0, 19999.0}) {
                buildFrame(e, set, 0, playhead);
                if (e.debugFramePeak() > 1.0 + 1e-6) {
                    detail = "window " + std::to_string(window) + " at playhead " +
                             std::to_string(playhead) + " published " +
                             std::to_string(e.debugFramePeak());
                    return false;
                }
            }
        }
    }
    return true;
}

/** Frames must follow the playhead. */
bool wtTracksPlayhead(std::string& detail) {
    // Noise, so that successive windows genuinely differ rather than repeating
    // a periodic waveform that would look identical wherever it was sampled.
    StemSet set;
    set.channels = 1;
    set.generation = 1;
    std::mt19937 rng(808);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        set.layer[L].channel[0].assign(48000, 0.f);
        for (auto& x : set.layer[L].channel[0]) x = dist(rng);
    }

    WavetableExtract e;
    e.setWindowSamples(2048);
    buildFrame(e, set, 0, 10000.0);
    std::vector<float> first(e.frame(), e.frame() + e.frameSize());
    const uint64_t countAfterFirst = e.frameCount();

    buildFrame(e, set, 0, 30000.0);
    if (e.frameCount() != countAfterFirst + 1) {
        detail = "the frame counter did not advance";
        return false;
    }

    double worst = 0.0;
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)first[i] - (double)e.frame()[i]));
    }
    if (worst < 0.1) {
        detail = "moving the playhead 20000 frames changed the wavetable by only " +
                 std::to_string(worst);
        return false;
    }

    // The same playhead must give the same frame, so the difference above is
    // the position and not just build-to-build noise.
    buildFrame(e, set, 0, 30000.0);
    std::vector<float> repeat(e.frame(), e.frame() + e.frameSize());
    buildFrame(e, set, 0, 30000.0);
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        if (std::fabs((double)repeat[i] - (double)e.frame()[i]) > 1e-6) {
            detail = "the same playhead gave two different frames";
            return false;
        }
    }
    return true;
}

/**
 * A build must not smear across playhead motion.
 *
 * The position is snapshotted when a build begins. Re-reading it every call
 * would spread one frame over however far the transport moved while it was
 * being built, so the frame would correspond to no actual moment in the
 * material.
 */
bool wtSnapshotsPosition(std::string& detail) {
    StemSet set;
    set.channels = 1;
    set.generation = 1;
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        set.layer[L].channel[0].assign(48000, 0.f);
        for (auto& x : set.layer[L].channel[0]) x = dist(rng);
    }

    // Build one frame with a still playhead.
    WavetableExtract still;
    buildFrame(still, set, 0, 20000.0);
    std::vector<float> reference(still.frame(), still.frame() + still.frameSize());

    // Build another while the playhead sweeps away underneath it. The frame must
    // match the position at the START of the build, not a blur of the sweep.
    WavetableExtract moving;
    double playhead = 20000.0;
    while (!moving.process(&set, 0, playhead)) playhead += 500.0;

    double worst = 0.0;
    for (std::size_t i = 0; i < moving.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)reference[i] - (double)moving.frame()[i]));
    }
    if (worst > 1e-5) {
        detail = "a moving playhead changed the frame by " + std::to_string(worst) +
                 "; the build is re-reading the position instead of snapshotting it";
        return false;
    }
    return true;
}

/** Frames must be normalised, DC free, and silent for silence. */
bool wtNormalises(std::string& detail) {
    for (double amplitude : {0.01, 0.1, 1.0}) {
        StemSet set;
        set.channels = 1;
        set.generation = 1;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(48000, 0.f);
            for (std::size_t i = 0; i < 48000; i++) {
                // Offset on purpose, so DC removal is exercised.
                set.layer[L].channel[0][i] =
                    (float)(0.5 + amplitude * std::sin(2 * M_PI * 200.0 * (double)i / 48000.0));
            }
        }
        WavetableExtract e;
        buildFrame(e, set, 0, 24000.0);

        const double peak = framePeak(e);
        if (std::fabs(peak - 1.0) > 1e-3) {
            detail = "amplitude " + std::to_string(amplitude) + " gave a peak of " +
                     std::to_string(peak);
            return false;
        }
        double mean = 0.0;
        for (std::size_t i = 0; i < e.frameSize(); i++) mean += e.frame()[i];
        mean /= (double)e.frameSize();
        if (std::fabs(mean) > 0.02) {
            detail = "frame DC was " + std::to_string(mean);
            return false;
        }
    }

    // Silence must stay silent.
    //
    // Exact zeros are not the case that matters: any gain applied to zero is
    // still zero, so a missing guard passes. What a real empty buffer holds
    // after a filter has run over it is denormal-level noise, and normalising
    // THAT lifts it to full scale. Both are checked.
    StemSet quiet;
    quiet.channels = 1;
    quiet.generation = 1;
    for (int L = 0; L < StemSet::kNumLayers; L++) quiet.layer[L].channel[0].assign(48000, 0.f);
    WavetableExtract e;
    buildFrame(e, quiet, 0, 24000.0);
    if (framePeak(e) > 1e-6) {
        detail = "digital silence produced a frame peaking at " +
                 std::to_string(framePeak(e));
        return false;
    }

    StemSet nearlyQuiet;
    nearlyQuiet.channels = 1;
    nearlyQuiet.generation = 2;
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> tiny(-1e-9f, 1e-9f);
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        nearlyQuiet.layer[L].channel[0].assign(48000, 0.f);
        for (auto& x : nearlyQuiet.layer[L].channel[0]) x = tiny(rng);
    }
    WavetableExtract q;
    buildFrame(q, nearlyQuiet, 0, 24000.0);
    if (framePeak(q) > 1e-6) {
        detail = "near-silence was normalised up to a peak of " +
                 std::to_string(framePeak(q));
        return false;
    }
    return true;
}

/**
 * The frame is read cyclically, so it has to join up at the wrap point.
 *
 * Source audio has no reason to, and the step is a click at the oscillator's own
 * frequency, which reads as a buzz under every note.
 */
bool wtLoopsCleanly(std::string& detail) {
    const auto set = toneSet(137.0);  // not a divisor of anything here
    for (int window : {256, 1024, 8192}) {
        WavetableExtract e;
        e.setWindowSamples(window);
        buildFrame(e, set, 0, 24000.0);

        const double wrapStep =
            std::fabs((double)e.frame()[0] - (double)e.frame()[e.frameSize() - 1]);
        // Compare against the largest step INSIDE the frame, so the bar scales
        // with how fast the waveform is moving rather than being an absolute.
        double worstInside = 0.0;
        for (std::size_t i = 1; i < e.frameSize(); i++) {
            worstInside = std::max(worstInside,
                                   std::fabs((double)e.frame()[i] - (double)e.frame()[i - 1]));
        }
        if (wrapStep > worstInside + 1e-4) {
            detail = "window " + std::to_string(window) + " wraps with a step of " +
                     std::to_string(wrapStep) + " against a worst interior step of " +
                     std::to_string(worstInside);
            return false;
        }
    }
    return true;
}

/**
 * Decimation must be averaged, not point sampled.
 *
 * At the top of the range the window is four times the frame, so taking every
 * fourth sample folds everything above a quarter of the frame's Nyquist back
 * down. Normalisation hides this completely: an attenuated frame and a
 * full-scale alias both come out peaking at one. The pre-normalisation peak is
 * the only direct evidence.
 */
bool wtAntiAliases(std::string& detail) {
    // Measured on the PUBLISHED frame, not on a diagnostic taken before
    // normalisation. Normalising to the frame's own peak would multiply
    // whatever the filter rejected straight back up to unity, so the
    // anti-aliasing would exist only in the diagnostic and the audible frame
    // would carry a full-scale aliased tone.
    struct Case { double hz; double maxFramePeak; };
    const Case cases[] = {{12000.0, 0.10}, {20000.0, 0.30}, {14000.0, 0.40}};
    for (const auto& c : cases) {
        const auto set = toneSet(c.hz);
        WavetableExtract e;
        e.setWindowSamples(8192);
        buildFrame(e, set, 0, 24000.0);
        const double peak = e.debugFramePeak();
        if (peak > c.maxFramePeak) {
            detail = std::to_string(c.hz) + " Hz was published at a frame peak of " +
                     std::to_string(peak) + ", above the " +
                     std::to_string(c.maxFramePeak) +
                     " an averaged decimation should leave";
            return false;
        }
    }
    // Low frequencies must pass essentially untouched, or the filter is simply
    // destroying everything and the cases above prove nothing.
    const auto set = toneSet(200.0);
    WavetableExtract e;
    e.setWindowSamples(8192);
    buildFrame(e, set, 0, 24000.0);
    if (e.debugFramePeak() < 0.9) {
        detail = "200 Hz was published at only " + std::to_string(e.debugFramePeak());
        return false;
    }
    return true;
}

/**
 * The anti-aliasing must hold at fractional window-to-frame ratios.
 *
 * Nearly every position of a continuous control gives a fractional ratio. A
 * window of 4095 has a step just under two, and truncating that to a single
 * sample turns the filter off exactly where it is still needed, so a test that
 * only covers the exact 4:1 case proves very little.
 */
bool wtFractionalWindow(std::string& detail) {
    // Just below each integer ratio, which is where truncation does its damage.
    const int windows[] = {4095, 4097, 6143, 6145, 8191, 3000, 5000, 7000};
    for (int window : windows) {
        const double ratio = (double)window / (double)WavetableExtract::kFrameSize;
        // A tone above the frame's Nyquist for this ratio must be rejected.
        const double aboveNyquist = 48000.0 / (2.0 * ratio) * 1.4;
        if (aboveNyquist > 22000.0) continue;   // not representable at this rate

        const auto set = toneSet(aboveNyquist);
        WavetableExtract e;
        e.setWindowSamples(window);
        buildFrame(e, set, 0, 24000.0);
        if (e.debugFramePeak() > 0.6) {
            detail = "window " + std::to_string(window) + " (ratio " +
                     std::to_string(ratio) + ") published a " +
                     std::to_string(aboveNyquist) + " Hz tone at " +
                     std::to_string(e.debugFramePeak());
            return false;
        }
    }
    return true;
}

/**
 * A constant source must give silence, not an edge-shaped waveform.
 *
 * Applying the taper before removing DC multiplies the offset by the fade, so a
 * flat input becomes a shape that rises and falls with the window and then
 * normalises to full scale. The DC comes off first.
 */
bool wtDcSource(std::string& detail) {
    for (float dc : {1.f, -0.7f, 0.05f}) {
        StemSet set;
        set.channels = 1;
        set.generation = 1;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0].assign(48000, dc);
        }
        WavetableExtract e;
        buildFrame(e, set, 0, 24000.0);
        if (e.debugFramePeak() > 1e-5) {
            detail = "a constant " + std::to_string(dc) +
                     " source produced a frame peaking at " +
                     std::to_string(e.debugFramePeak());
            return false;
        }
    }

    // And an offset tone must give the tone, not the tone plus an edge shape.
    StemSet offset;
    offset.channels = 1;
    offset.generation = 2;
    for (int L = 0; L < StemSet::kNumLayers; L++) {
        offset.layer[L].channel[0].assign(48000, 0.f);
        for (std::size_t i = 0; i < 48000; i++) {
            offset.layer[L].channel[0][i] =
                (float)(0.9 + 0.1 * std::sin(2 * M_PI * 300.0 * (double)i / 48000.0));
        }
    }
    WavetableExtract e;
    buildFrame(e, offset, 0, 24000.0);
    double mean = 0.0;
    for (std::size_t i = 0; i < e.frameSize(); i++) mean += e.frame()[i];
    mean /= (double)e.frameSize();
    if (std::fabs(mean) > 0.02) {
        detail = "a tone on a 0.9 offset left a frame DC of " + std::to_string(mean);
        return false;
    }
    if (e.debugFramePeak() < 0.5) {
        detail = "a tone on a 0.9 offset was published at only " +
                 std::to_string(e.debugFramePeak());
        return false;
    }
    return true;
}

/**
 * One non-finite sample in a stem must not poison every frame after it.
 *
 * RingBuffer::write stores what it is given, so this is reachable from a
 * misbehaving upstream module, and without a guard the value spreads through
 * the mean and the gain into every value of every frame published afterwards.
 */
bool wtNonFiniteStem(std::string& detail) {
    for (float poison : {std::nanf(""), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()}) {
        auto set = toneSet(220.0);
        // Inside the window that will be read at playhead 24000 with the
        // default 2048 window.
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set.layer[L].channel[0][24000] = poison;
            set.layer[L].channel[0][23500] = poison;
        }
        WavetableExtract e;
        buildFrame(e, set, 0, 24000.0);
        for (std::size_t i = 0; i < e.frameSize(); i++) {
            if (!std::isfinite(e.frame()[i])) {
                detail = "a poisoned stem produced a non-finite frame sample at " +
                         std::to_string(i);
                return false;
            }
        }
        // And the frame must still be usable, not flattened to nothing.
        if (e.debugFramePeak() < 0.5) {
            detail = "a single bad sample flattened the frame to " +
                     std::to_string(e.debugFramePeak());
            return false;
        }

        // The next frame, from clean material, must be unaffected.
        const auto clean = toneSet(220.0, 48000, 7);
        WavetableExtract after;
        buildFrame(after, clean, 0, 24000.0);
        if (!std::isfinite(after.debugFramePeak()) || after.debugFramePeak() < 0.5) {
            detail = "a later clean frame was affected";
            return false;
        }
    }
    return true;
}

/**
 * The ring buffer must not store non-finite input.
 *
 * This is the path by which a bad sample reaches everything else: HPSS carries
 * a NaN through the FFT into all four stems, and from there into every
 * oscillator frame and every value the mixer publishes. Guarding at the point
 * of entry is cheaper than guarding every consumer.
 */
bool bufferRejectsNonFinite(std::string& detail) {
    RingBuffer buffer(48000, 0.1f, 2);
    for (int i = 0; i < 100; i++) buffer.write(0.5f, -0.5f);
    buffer.write(std::nanf(""), 0.25f);
    buffer.write(0.25f, std::numeric_limits<float>::infinity());
    buffer.write(-std::numeric_limits<float>::infinity(), std::nanf(""));
    for (int i = 0; i < 100; i++) buffer.write(0.5f, -0.5f);

    for (std::size_t i = 0; i < buffer.framesStored(); i++) {
        float l = 0.f, r = 0.f;
        buffer.readFrame(i, l, r);
        if (!std::isfinite(l) || !std::isfinite(r)) {
            detail = "frame " + std::to_string(i) + " holds a non-finite sample";
            return false;
        }
    }
    // The good channel of a half-bad frame must survive, so the guard is
    // per-sample rather than dropping whole frames.
    float l = 0.f, r = 0.f;
    buffer.readFrame(100, l, r);
    if (l != 0.f || r != 0.25f) {
        detail = "a half-bad frame stored (" + std::to_string(l) + ", " +
                 std::to_string(r) + ") instead of (0, 0.25)";
        return false;
    }
    return true;
}

/**
 * Reading past either end of the stem must not manufacture a waveform.
 *
 * The window is centred on the playhead, so at the loop start half of it lies
 * before the beginning of the material, and wt_offset can push it out entirely.
 * Zero padding puts artificial silence into the mean and the peak, and after DC
 * removal the padded half and the real half come out equal and opposite: a
 * constant 1.0 stem at playhead 0 produced a full-scale square instead of
 * silence. The loop start is not an edge case; it is where the playhead sits at
 * the top of every bar.
 */
bool wtBoundaryPadding(std::string& detail) {
    // A constant stem has no waveform anywhere in it, so anything the frame
    // contains was manufactured by the padding.
    StemSet flat;
    flat.channels = 1;
    flat.generation = 1;
    for (int L = 0; L < StemSet::kNumLayers; L++) flat.layer[L].channel[0].assign(8000, 1.f);

    struct Case { double playhead; float offset; const char* where; };
    const Case cases[] = {
        {0.0, 0.f, "the very start"},
        {100.0, 0.f, "just after the start"},
        {7999.0, 0.f, "the very end"},
        {7000.0, 0.f, "just before the end"},
        {4000.0, 1.f, "pushed past the end by the offset"},
        {4000.0, -1.f, "pushed before the start by the offset"},
        {4000.0, 0.f, "the middle"},
    };
    for (const auto& c : cases) {
        WavetableExtract e;
        e.setOffset(c.offset);
        buildFrame(e, flat, 0, c.playhead);
        if (e.debugFramePeak() > 1e-5) {
            detail = std::string("a constant stem read at ") + c.where +
                     " produced a frame peaking at " + std::to_string(e.debugFramePeak());
            return false;
        }
    }

    // With real material, a frame taken at the start must not be dominated by
    // the boundary either: compare its shape against one taken well inside.
    const auto tone = toneSet(300.0, 8000);
    WavetableExtract atStart, inside;
    buildFrame(atStart, tone, 0, 0.0);
    buildFrame(inside, tone, 0, 4000.0);
    if (atStart.debugFramePeak() > 1.0 + 1e-5) {
        detail = "a frame at the loop start exceeded full scale at " +
                 std::to_string(atStart.debugFramePeak());
        return false;
    }
    for (std::size_t i = 0; i < atStart.frameSize(); i++) {
        if (!std::isfinite(atStart.frame()[i])) {
            detail = "a frame at the loop start holds a non-finite sample";
            return false;
        }
    }
    return true;
}

/**
 * A stem set too short to interpolate must invalidate the old frame.
 *
 * Returning early on the size, before checking whether the set is stale, left
 * frame() showing the previous recording indefinitely once a short take was
 * published. Hpss::separate sizes every layer to the input length and accepts
 * sub-frame input, so a zero or one frame StemSet really does reach here, and
 * the wavetable has to follow the take rather than keep material that is gone.
 */
bool wtDegenerateSet(std::string& detail) {
    const auto real = toneSet(220.0, 8000, /*generation=*/1);
    WavetableExtract e;
    buildFrame(e, real, 0, 4000.0);
    if (e.debugFramePeak() < 0.5) {
        detail = "setup failed: no frame from real material";
        return false;
    }
    const uint64_t countBefore = e.frameCount();

    for (std::size_t length : {(std::size_t)0, (std::size_t)1}) {
        StemSet tiny;
        tiny.channels = 1;
        tiny.generation = 10 + length;
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            tiny.layer[L].channel[0].assign(length, 0.5f);
        }

        bool published = false;
        for (int i = 0; i < 200; i++) {
            if (e.process(&tiny, 0, 0.0)) published = true;
        }
        if (!published) {
            detail = "a " + std::to_string(length) +
                     " frame stem set never invalidated the old wavetable";
            return false;
        }
        if (e.debugFramePeak() > 1e-6) {
            detail = "a " + std::to_string(length) +
                     " frame stem set left a frame peaking at " +
                     std::to_string(e.debugFramePeak());
            return false;
        }
    }
    if (e.frameCount() <= countBefore) {
        detail = "the frame counter did not advance for the degenerate sets";
        return false;
    }

    // It must issue silence ONCE per take, not spin republishing it. The
    // silent replacement runs through the same amortised path as a real frame,
    // so it takes a full pass of budget-sized calls to land; run it out first.
    StemSet tiny;
    tiny.channels = 1;
    tiny.generation = 99;
    for (int L = 0; L < StemSet::kNumLayers; L++) tiny.layer[L].channel[0].assign(1, 0.5f);
    const int silentBudget = 128;
    e.setBudgetPerCall(silentBudget);
    std::size_t worstSilentCall = 0;
    int silentCalls = 0;
    while (silentCalls < 1000) {
        const bool published = e.process(&tiny, 0, 0.0);
        worstSilentCall = std::max(worstSilentCall, e.debugWorkLastCall());
        silentCalls++;
        if (published) break;
    }
    // Replacing the frame must respect the budget too, or a very short take
    // recreates exactly the boundary spike the reading path avoids.
    if ((int)worstSilentCall > silentBudget) {
        detail = "replacing a frame for a degenerate take wrote " +
                 std::to_string(worstSilentCall) + " samples in one call";
        return false;
    }
    if (silentCalls < 4) {
        detail = "a degenerate take replaced the frame in only " +
                 std::to_string(silentCalls) + " calls; it is not amortised";
        return false;
    }
    const uint64_t settled = e.frameCount();
    for (int i = 0; i < 500; i++) e.process(&tiny, 0, 0.0);
    if (e.frameCount() != settled) {
        detail = "a degenerate set republished " +
                 std::to_string(e.frameCount() - settled) + " times";
        return false;
    }

    // And real material afterwards must build normally again.
    const auto again = toneSet(330.0, 8000, /*generation=*/100);
    buildFrame(e, again, 0, 4000.0);
    if (e.debugFramePeak() < 0.5) {
        detail = "a real take after a degenerate one produced a frame peaking at " +
                 std::to_string(e.debugFramePeak());
        return false;
    }
    return true;
}

/**
 * reset() must re-arm the degenerate-take handling.
 *
 * Leaving the handled flag set while dropping the phase meant a reset partway
 * through replacing a frame satisfied the already-handled test forever
 * afterwards, so finalisation never restarted and the previous audible frame
 * stayed visible. Patch load and sample rate changes both call reset().
 */
bool wtResetRearmsSilence(std::string& detail) {
    const auto real = toneSet(220.0, 8000, /*generation=*/1);
    WavetableExtract e;
    e.setBudgetPerCall(64);
    buildFrame(e, real, 0, 4000.0);
    if (e.debugFramePeak() < 0.5) {
        detail = "setup failed: no frame from real material";
        return false;
    }

    StemSet tiny;
    tiny.channels = 1;
    tiny.generation = 42;
    for (int L = 0; L < StemSet::kNumLayers; L++) tiny.layer[L].channel[0].assign(1, 0.5f);

    // Start replacing the frame, then reset partway through.
    for (int i = 0; i < 3; i++) e.process(&tiny, 0, 0.0);
    e.reset();

    // The same take must still be able to replace the frame.
    bool published = false;
    for (int i = 0; i < 500; i++) {
        if (e.process(&tiny, 0, 0.0)) published = true;
    }
    if (!published) {
        detail = "after a reset mid-replacement, the degenerate take never "
                 "invalidated the old wavetable";
        return false;
    }
    if (e.debugFramePeak() > 1e-6) {
        detail = "after a reset, the old audible frame is still showing at " +
                 std::to_string(e.debugFramePeak());
        return false;
    }
    return true;
}

/** A stale snapshot must restart the build rather than finish a mixed frame. */
bool wtRestartsOnChange(std::string& detail) {
    auto setA = toneSet(100.0, 48000, /*generation=*/1);
    auto setB = toneSet(700.0, 48000, /*generation=*/2);

    // Swap the stem set halfway through a build.
    WavetableExtract e;
    e.setBudgetPerCall(128);
    for (int i = 0; i < 8; i++) e.process(&setA, 0, 24000.0);
    int calls = 0;
    while (!e.process(&setB, 0, 24000.0) && calls < 1000) calls++;

    // The completed frame must match one built entirely from B.
    WavetableExtract clean;
    buildFrame(clean, setB, 0, 24000.0);
    double worst = 0.0;
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)e.frame()[i] - (double)clean.frame()[i]));
    }
    if (worst > 1e-5) {
        detail = "a frame built across a stem change differs from a clean one by " +
                 std::to_string(worst);
        return false;
    }

    // A window change is DIFFERENT: it must not discard the build. Everything
    // the build depends on is snapshotted, so the in-flight frame finishes with
    // the old size and the new size applies to the next one. Restarting on it
    // meant a moving wt_window never let any frame finish.
    WavetableExtract w;
    w.setBudgetPerCall(128);
    w.setWindowSamples(1024);
    for (int i = 0; i < 8; i++) w.process(&setA, 0, 24000.0);
    w.setWindowSamples(4096);
    calls = 0;
    while (!w.process(&setA, 0, 24000.0) && calls < 1000) calls++;

    WavetableExtract atOldWindow;
    atOldWindow.setWindowSamples(1024);
    buildFrame(atOldWindow, setA, 0, 24000.0);
    worst = 0.0;
    for (std::size_t i = 0; i < w.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)w.frame()[i] - (double)atOldWindow.frame()[i]));
    }
    if (worst > 1e-5) {
        detail = "an in-flight frame did not finish at the window it started with; "
                 "differs by " + std::to_string(worst);
        return false;
    }

    // And the NEXT frame must use the new window.
    buildFrame(w, setA, 0, 24000.0);
    WavetableExtract atNewWindow;
    atNewWindow.setWindowSamples(4096);
    buildFrame(atNewWindow, setA, 0, 24000.0);
    worst = 0.0;
    for (std::size_t i = 0; i < w.frameSize(); i++) {
        worst = std::max(worst, std::fabs((double)w.frame()[i] - (double)atNewWindow.frame()[i]));
    }
    if (worst > 1e-5) {
        detail = "the frame after a window change did not use the new window; "
                 "differs by " + std::to_string(worst);
        return false;
    }
    return true;
}

/**
 * A moving wt_window must still produce frames.
 *
 * Restarting the build whenever the window changed meant that automating the
 * control, or simply turning it slowly enough to cross an integer on each call,
 * discarded the progress every time. No frame could ever finish and the
 * oscillator kept the previous wavetable until the control stopped moving,
 * which is the opposite of what a moving control should do.
 */
bool wtWindowAutomation(std::string& detail) {
    const auto set = toneSet(150.0);
    WavetableExtract e;
    buildFrame(e, set, 0, 24000.0);
    const uint64_t before = e.frameCount();

    // A new window value on every single call, sweeping the whole range.
    int window = WavetableExtract::kMinWindow;
    int direction = 7;
    for (int i = 0; i < 4000; i++) {
        window += direction;
        if (window >= WavetableExtract::kMaxWindow || window <= WavetableExtract::kMinWindow) {
            direction = -direction;
        }
        e.setWindowSamples(window);
        e.process(&set, 0, 24000.0 + i);
    }

    const uint64_t published = e.frameCount() - before;
    if (published < 10) {
        detail = "a continuously moving window published only " +
                 std::to_string(published) + " frames in 4000 calls";
        return false;
    }
    if (e.debugFramePeak() < 0.5) {
        detail = "a continuously moving window left a frame peaking at " +
                 std::to_string(e.debugFramePeak());
        return false;
    }
    return true;
}

/** Missing, empty and malformed input must be safe. */
bool wtBadInput(std::string& detail) {
    WavetableExtract e;
    const auto set = toneSet(100.0);

    // No stems yet: the state on patch load.
    for (int i = 0; i < 100; i++) {
        if (e.process(nullptr, 0, 24000.0)) {
            detail = "a null stem set completed a frame";
            return false;
        }
    }
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        if (!std::isfinite(e.frame()[i])) {
            detail = "the frame holds a non-finite sample before anything was built";
            return false;
        }
    }

    // Out-of-range layers, empty stems, non-finite playhead.
    StemSet empty;
    empty.generation = 3;
    for (int layer : {-1, 0, 4, 99}) e.process(&set, layer, 24000.0);
    for (int i = 0; i < 100; i++) e.process(&empty, 0, 24000.0);
    for (double bad : {std::nan(""), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        e.process(&set, 0, bad);
    }
    // Playheads far outside the stem must give silence, not a read out of range.
    for (double far : {-1e9, 1e9, -100000.0, 1e6}) {
        WavetableExtract off;
        buildFrame(off, set, 0, far);
        for (std::size_t i = 0; i < off.frameSize(); i++) {
            if (!std::isfinite(off.frame()[i])) {
                detail = "playhead " + std::to_string(far) + " produced a non-finite frame";
                return false;
            }
        }
    }

    e.setOffset(std::nanf(""));
    e.setWindowSamples(-5);
    e.setWindowSamples(1 << 20);
    e.setBudgetPerCall(-3);
    buildFrame(e, set, 0, 24000.0);
    for (std::size_t i = 0; i < e.frameSize(); i++) {
        if (!std::isfinite(e.frame()[i])) {
            detail = "malformed settings produced a non-finite frame";
            return false;
        }
    }
    return true;
}


// ---------------------------------------------------------------------------
// WavetableOsc
// ---------------------------------------------------------------------------

using WiggleRoom::stems::WavetableOsc;

namespace {
/** A frame holding @p harmonics of a sawtooth, normalised. */
std::vector<float> harmonicFrame(int harmonics) {
    std::vector<float> frame(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < frame.size(); i++) {
        const double phase = (double)i / (double)frame.size();
        double v = 0.0;
        for (int k = 1; k <= harmonics; k++) v += std::sin(2 * M_PI * k * phase) / k;
        frame[i] = (float)(v * 0.5);
    }
    return frame;
}

/** Build the mip chain to completion. */
void primeOsc(WavetableOsc& osc, const std::vector<float>& frame, uint64_t count = 1) {
    for (int i = 0; i < 200; i++) osc.offerFrame(frame.data(), count);
}

/**
 * Alias floor of @p out, in dB relative to the harmonic content.
 *
 * Every significant spectral peak is classified by whether it sits on a
 * multiple of f0. Probing a handful of fixed frequencies instead does not work:
 * aliases land at |k*f0 - m*sr|, which is nowhere near an arbitrary grid, and a
 * first attempt at this measured almost no difference between a mip-mapped and
 * a raw oscillator purely because it was looking in the wrong places.
 */
double aliasFloorDb(const std::vector<float>& out, double f0, int sampleRate) {
    const std::size_t n = 16384;
    ReferenceFft fft(n);
    std::vector<float> in(n, 0.f), spectrum(fft.spectrumLength(), 0.f);
    for (std::size_t i = 0; i < n && i < out.size(); i++) {
        const double w = 0.5 * (1.0 - std::cos(2 * M_PI * (double)i / (double)n));
        in[i] = (float)(out[i] * w);
    }
    fft.forward(in.data(), spectrum.data());

    const double binHz = (double)sampleRate / (double)n;
    double harmonic = 0.0, alias = 0.0;
    for (std::size_t b = 1; b < fft.numBins(); b++) {
        const double re = spectrum[2 * b], im = spectrum[2 * b + 1];
        const double mag = std::sqrt(re * re + im * im);
        if (mag < 1e-5) continue;
        const double f = (double)b * binHz;
        const double k = std::round(f / f0);
        const double distance = (k >= 1.0) ? std::fabs(f - k * f0) / binHz : 1e9;
        if (distance <= 2.0) harmonic += mag * mag;
        else alias += mag * mag;
    }
    return 10.0 * std::log10(alias / std::max(harmonic, 1e-30));
}

/** Dominant frequency of @p out, by parabolic peak interpolation. */
double dominantHz(const std::vector<float>& out, int sampleRate) {
    const std::size_t n = 16384;
    ReferenceFft fft(n);
    std::vector<float> in(n, 0.f), spectrum(fft.spectrumLength(), 0.f);
    for (std::size_t i = 0; i < n && i < out.size(); i++) {
        const double w = 0.5 * (1.0 - std::cos(2 * M_PI * (double)i / (double)n));
        in[i] = (float)(out[i] * w);
    }
    fft.forward(in.data(), spectrum.data());
    std::size_t best = 1;
    double bestMag = 0.0;
    std::vector<double> mags(fft.numBins(), 0.0);
    for (std::size_t b = 1; b < fft.numBins(); b++) {
        const double re = spectrum[2 * b], im = spectrum[2 * b + 1];
        mags[b] = std::sqrt(re * re + im * im);
        if (mags[b] > bestMag) { bestMag = mags[b]; best = b; }
    }
    double offset = 0.0;
    if (best > 1 && best + 1 < fft.numBins()) {
        const double a = mags[best - 1], b2 = mags[best], c = mags[best + 1];
        const double denom = 2.0 * (2.0 * b2 - a - c);
        if (std::fabs(denom) > 1e-18) offset = (c - a) / denom;
        if (!(offset > -0.5 && offset < 0.5)) offset = 0.0;
    }
    return ((double)best + offset) * (double)sampleRate / (double)n;
}

/**
 * Phase-independent distance between two oscillator outputs, 0 to 1.
 *
 * Comparing sample by sample does not work: the two runs are at different
 * points in the cycle, so a signal identical in every way that matters can
 * differ by twice its amplitude. Harmonic magnitudes carry the timbre and
 * ignore the phase, which is exactly the comparison wanted here.
 */
double spectralDistance(const std::vector<float>& a, const std::vector<float>& b,
                        int sampleRate) {
    const std::size_t n = 8192;
    ReferenceFft fft(n);
    auto magnitudes = [&](const std::vector<float>& x) {
        std::vector<float> in(n, 0.f), spectrum(fft.spectrumLength(), 0.f);
        for (std::size_t i = 0; i < n && i < x.size(); i++) {
            const double w = 0.5 * (1.0 - std::cos(2 * M_PI * (double)i / (double)n));
            in[i] = (float)(x[i] * w);
        }
        fft.forward(in.data(), spectrum.data());
        std::vector<double> mags(fft.numBins(), 0.0);
        for (std::size_t k = 1; k < fft.numBins(); k++) {
            const double re = spectrum[2 * k], im = spectrum[2 * k + 1];
            mags[k] = std::sqrt(re * re + im * im);
        }
        return mags;
    };
    (void)sampleRate;
    const auto ma = magnitudes(a);
    const auto mb = magnitudes(b);
    double num = 0.0, den = 0.0;
    for (std::size_t k = 1; k < ma.size(); k++) {
        num += std::fabs(ma[k] - mb[k]);
        den += std::max(ma[k], mb[k]);
    }
    return (den > 1e-12) ? (num / den) : 0.0;
}

std::vector<float> renderOsc(WavetableOsc& osc, float volts, std::size_t n = 16384) {
    std::vector<float> out(n, 0.f);
    for (auto& s : out) s = osc.process(volts);
    return out;
}
}  // namespace

/**
 * The alias floor must be low AND must not rise with pitch.
 *
 * A frame repeats exactly, so folded content lands on fixed inharmonic partials
 * rather than spreading into noise. That reads as a metallic ring under the
 * note, and a floor that climbs with pitch is exactly what makes it noticeable.
 */
bool oscAliasFloor(std::string& detail) {
    const int sampleRate = 48000;
    const auto frame = harmonicFrame(512);
    double worst = -1e9, best = 1e9;
    for (double volts : {-1.0, 0.0, 1.0, 2.0, 3.0}) {
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts);
        const double f0 = 261.6255653005986 * std::exp2(volts);
        const double floorDb = aliasFloorDb(out, f0, sampleRate);
        worst = std::max(worst, floorDb);
        best = std::min(best, floorDb);
        if (floorDb > -25.0) {
            detail = "alias floor at " + std::to_string(volts) + " V was " +
                     std::to_string(floorDb) + " dB";
            return false;
        }
    }
    // Flat across the range, not just low at the bottom. Without the mip chain
    // the floor climbs from -25.9 dB to -12.6 dB over these five octaves.
    if (worst - best > 8.0) {
        detail = "the alias floor varies by " + std::to_string(worst - best) +
                 " dB across the range, from " + std::to_string(best) + " to " +
                 std::to_string(worst);
        return false;
    }
    return true;
}

/** Pitch must track 1 V/octave. */
bool oscPitchTracking(std::string& detail) {
    const int sampleRate = 48000;
    // A single harmonic, so the dominant peak is unambiguous.
    const auto frame = harmonicFrame(1);
    for (double volts : {-2.0, -1.0, 0.0, 1.0, 2.0}) {
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts);
        const double expected = 261.6255653005986 * std::exp2(volts);
        const double measured = dominantHz(out, sampleRate);
        const double cents = 1200.0 * std::log2(measured / expected);
        if (std::fabs(cents) > 15.0) {
            detail = std::to_string(volts) + " V gave " + std::to_string(measured) +
                     " Hz, expected " + std::to_string(expected) + " (" +
                     std::to_string(cents) + " cents)";
            return false;
        }
    }

    // Coarse and fine must add on top, in semitones.
    WavetableOsc osc;
    osc.setSampleRate(sampleRate);
    osc.setLevel(1.f);
    osc.setMorph(1.f);
    primeOsc(osc, frame);
    osc.setCoarse(12.f);
    const double up = dominantHz(renderOsc(osc, 0.f), sampleRate);
    osc.setCoarse(0.f);
    const double base = dominantHz(renderOsc(osc, 0.f), sampleRate);
    const double ratio = up / std::max(base, 1e-9);
    if (std::fabs(ratio - 2.0) > 0.05) {
        detail = "twelve semitones of coarse gave a ratio of " + std::to_string(ratio);
        return false;
    }
    return true;
}

/** Pitch must not depend on the sample rate. */
bool oscSampleRate(std::string& detail) {
    const auto frame = harmonicFrame(1);
    for (double volts : {0.0, 1.0}) {
        double reference = 0.0;
        for (int sampleRate : {44100, 48000, 96000}) {
            WavetableOsc osc;
            osc.setSampleRate(sampleRate);
            osc.setLevel(1.f);
            osc.setMorph(1.f);
            primeOsc(osc, frame);
            const double measured = dominantHz(renderOsc(osc, (float)volts), sampleRate);
            if (reference == 0.0) reference = measured;
            const double cents = 1200.0 * std::log2(measured / reference);
            if (std::fabs(cents) > 15.0) {
                detail = "sample rate " + std::to_string(sampleRate) + " shifted " +
                         std::to_string(volts) + " V by " + std::to_string(cents) +
                         " cents";
                return false;
            }
        }
    }
    return true;
}

/**
 * Swapping frames must not click, and morph must not zipper.
 *
 * The oscillator crossfades between two complete chains. Fading into one that
 * is still being built plays whatever the previous frame left in the unwritten
 * part, which is a burst of the wrong waveform.
 */
bool oscFrameChangeIsClickFree(std::string& detail) {
    const int sampleRate = 48000;
    const auto first = harmonicFrame(8);
    // A very different frame, so a hard switch would be unmistakable.
    std::vector<float> second(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < second.size(); i++) {
        second[i] = (i < second.size() / 2) ? 0.9f : -0.9f;
    }

    // The bar is the steady-state step of EACH frame played on its own.
    // Comparing the transition against only the first frame's step is not a
    // test: the second frame here is a square, whose own edge is four times
    // larger than anything in a smooth frame, so the measurement failed on the
    // material rather than on any click.
    auto steadyStep = [&](const std::vector<float>& frame) {
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        return maxStep(renderOsc(osc, 0.f, 8000));
    };
    const double bar = std::max(steadyStep(first), steadyStep(second));

    for (float morph : {0.f, 0.3f, 0.7f, 1.f}) {
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(morph);
        primeOsc(osc, first, 1);

        std::vector<float> out;
        out.reserve(48000);
        for (int i = 0; i < 8000; i++) {
            osc.offerFrame(first.data(), 1);
            out.push_back(osc.process(0.f));
        }
        for (int i = 0; i < 40000; i++) {
            osc.offerFrame(second.data(), 2);
            out.push_back(osc.process(0.f));
        }

        const double worst = maxStep(out);
        if (worst > bar * 1.2 + 1e-3) {
            detail = "morph " + std::to_string(morph) + ": swapping frames stepped by " +
                     std::to_string(worst) + " against a steady-state step of " +
                     std::to_string(bar);
            return false;
        }

        // A slow morph must actually be slow, and a fast one fast, or the
        // control does nothing and this test proves only that nothing changed.
        if (morph <= 0.f) {
            // Two thirds of a second in, a one second fade is still moving.
            const double early = out[8000 + 100];
            const double late = out[8000 + 30000];
            if (std::fabs(early - late) < 1e-6) {
                detail = "at morph 0 the output did not change across the fade";
                return false;
            }
        }
    }
    return true;
}

/**
 * A slow morph must smear toward newer frames, not freeze on the first one.
 *
 * While a crossfade runs, the incoming side is being sounded. Rebuilding it
 * because a newer frame arrived cancels the fade before the sides can swap, and
 * with a morph slower than the extractor's publish interval that happens on
 * every publish. The default morph publishes roughly every 85 ms while morph 0
 * fades for a second, so this is the ordinary case rather than an extreme.
 */
bool oscSlowMorphStillTracks(std::string& detail) {
    const auto first = harmonicFrame(4);
    std::vector<float> second(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < second.size(); i++) {
        second[i] = (i < second.size() / 2) ? 0.8f : -0.8f;
    }

    WavetableOsc osc;
    osc.setSampleRate(48000);
    osc.setLevel(1.f);
    osc.setMorph(0.f);            // the slowest fade available
    primeOsc(osc, first, 1);

    std::vector<float> before = renderOsc(osc, 0.f, 4000);

    // New frames arriving far faster than the fade can follow, exactly as the
    // extractor publishes them.
    uint64_t count = 2;
    std::vector<float> out;
    for (int block = 0; block < 400; block++) {
        for (int i = 0; i < 256; i++) {
            osc.offerFrame(second.data(), count);
            out.push_back(osc.process(0.f));
        }
        count++;   // a new frame every block
    }

    // It must CONVERGE on the new material, not merely wobble toward it.
    //
    // Checking only that the output moved is not enough: a fade that is
    // repeatedly cancelled and restarted still blends a little each time, so it
    // drifts without ever arriving. The tail is therefore compared against the
    // second frame played on its own.
    WavetableOsc reference;
    reference.setSampleRate(48000);
    reference.setLevel(1.f);
    reference.setMorph(1.f);
    primeOsc(reference, second, 1);
    const auto target = renderOsc(reference, 0.f, 4000);

    std::vector<float> tail(out.end() - (long)target.size(), out.end());
    const double distance = spectralDistance(tail, target, 48000);
    if (distance > 0.25) {
        detail = "with a slow morph and frames arriving every block, the output "
                 "never reached the new frame; spectral distance " +
                 std::to_string(distance) +
                 ", so the crossfade is being cancelled before it can complete";
        return false;
    }
    return true;
}

/**
 * The defaults must work without touching a setter.
 *
 * The morph coefficient was only computed inside the setters, so at the
 * documented default it stayed at zero and the crossfade never advanced: the
 * oscillator sat on its first frame for the life of the patch.
 */
bool oscDefaultsCrossfade(std::string& detail) {
    const auto first = harmonicFrame(4);
    std::vector<float> second(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < second.size(); i++) {
        second[i] = (i < second.size() / 2) ? 0.8f : -0.8f;
    }

    // NOTHING configured at all, not even the sample rate. Calling
    // setSampleRate also recomputes the morph coefficient, so a test that calls
    // it exercises the setter rather than the defaults and passes either way.
    WavetableOsc osc;
    primeOsc(osc, first, 1);
    const auto before = renderOsc(osc, 0.f, 8192);

    for (int i = 0; i < 400000; i++) {
        osc.offerFrame(second.data(), 2);
        osc.process(0.f);
    }
    const auto after = renderOsc(osc, 0.f, 8192);

    const double moved = spectralDistance(before, after, 48000);
    if (moved < 0.2) {
        detail = "at the default settings the oscillator never moved off its "
                 "first frame; spectral distance only " + std::to_string(moved);
        return false;
    }
    return true;
}

/**
 * reset() must leave the oscillator able to play again.
 *
 * Clearing the build while keeping the frame it was for meant a later offer of
 * that same frame saw neither a changed count nor a running build, so it could
 * never become playable until the extractor happened to publish again.
 */
bool oscResetRearms(std::string& detail) {
    const auto first = harmonicFrame(4);
    std::vector<float> second(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < second.size(); i++) {
        second[i] = (i < second.size() / 2) ? 0.8f : -0.8f;
    }

    // The case that matters is a reset partway through building a SECOND frame
    // while a first one is already playing. With nothing playing yet the
    // not-ready check restarts the build anyway, so that path passes whether or
    // not reset re-arms, and a test using it proves nothing.
    WavetableOsc osc;
    osc.setSampleRate(48000);
    osc.setLevel(1.f);
    osc.setMorph(1.f);
    osc.setBudgetPerCall(64);
    primeOsc(osc, first, 1);

    for (int i = 0; i < 3; i++) osc.offerFrame(second.data(), 2);
    osc.reset();

    // The same second frame must still become playable.
    for (int i = 0; i < 2000; i++) {
        osc.offerFrame(second.data(), 2);
        osc.process(0.f);
    }
    const auto out = renderOsc(osc, 0.f, 4000);

    WavetableOsc reference;
    reference.setSampleRate(48000);
    reference.setLevel(1.f);
    reference.setMorph(1.f);
    primeOsc(reference, second, 1);
    const auto target = renderOsc(reference, 0.f, 4000);

    const double distance = spectralDistance(out, target, 48000);
    if (distance > 0.25) {
        detail = "after a reset mid-build the new frame never became playable; "
                 "spectral distance from it is " + std::to_string(distance);
        return false;
    }
    return true;
}

/**
 * The chain must cover every pitch 1 V/octave can reach.
 *
 * Eight levels ended at a sixteen sample table carrying eight harmonics, valid
 * only to about 3 kHz, while an ordinary input crosses that by 3.5 V and keeps
 * going. Past the end of the chain the selection saturates and the content
 * aliases, which is what the chain exists to prevent.
 */
bool oscChainCoversTheRange(std::string& detail) {
    const auto frame = harmonicFrame(512);
    for (double volts : {3.5, 4.0, 4.5, 5.0}) {
        WavetableOsc osc;
        osc.setSampleRate(48000);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts);
        const double f0 = 261.6255653005986 * std::exp2(volts);
        if (f0 >= 24000.0) continue;
        const double floorDb = aliasFloorDb(out, f0, 48000);
        if (floorDb > -18.0) {
            detail = "at " + std::to_string(volts) + " V (" + std::to_string(f0) +
                     " Hz) the alias floor was " + std::to_string(floorDb) + " dB";
            return false;
        }
    }
    return true;
}

/**
 * Crossing a mip boundary must not jump the timbre.
 *
 * Adjacent tables differ by a whole octave of bandwidth, so a harmonic present
 * in one is filtered out of the next. Selecting exactly one entry means the
 * smallest pitch modulation across a boundary produces an abrupt timbral step.
 */
bool oscMipBoundaryIsSmooth(std::string& detail) {
    const int sampleRate = 48000;
    const auto frame = harmonicFrame(256);

    // Sweep pitch finely through several boundaries and watch the output level.
    // A hard switch shows up as a step in RMS where a blend gives a ramp.
    double worstJump = 0.0;
    double atVolts = 0.0;
    double previous = -1.0;
    for (int step = 0; step <= 400; step++) {
        const double volts = 0.5 + (double)step * (3.0 / 400.0);
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts, 4096);
        double acc = 0.0;
        for (float v : out) acc += (double)v * v;
        const double rms = std::sqrt(acc / (double)out.size());
        if (previous >= 0.0) {
            const double jump = std::fabs(rms - previous) / std::max(previous, 1e-9);
            if (jump > worstJump) { worstJump = jump; atVolts = volts; }
        }
        previous = rms;
    }
    // Neighbouring steps are under a hundredth of a volt apart, so any real
    // change in level between them is the table switching rather than the pitch.
    if (worstJump > 0.08) {
        detail = "level jumped by " + std::to_string(worstJump * 100.0) +
                 " per cent between adjacent pitches near " + std::to_string(atVolts) +
                 " V; mip levels are being switched rather than blended";
        return false;
    }
    return true;
}

/**
 * The coarsest table must not carry a harmonic above output Nyquist.
 *
 * The decimation filter is half-band, so its response at the new Nyquist is 0.5
 * rather than zero and that bin survives at every level. It only matters at the
 * end of the chain: the four sample table carries a fundamental and a second
 * harmonic, and above about 12 kHz that second harmonic is itself above output
 * Nyquist and folds back into the audible band.
 */
bool oscTopOfRangeIsClean(std::string& detail) {
    const int sampleRate = 48000;
    // A frame whose second harmonic is strong and in cosine phase, which is the
    // component that survives the filter and folds.
    std::vector<float> frame(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < frame.size(); i++) {
        const double phase = (double)i / (double)frame.size();
        frame[i] = (float)(0.5 * std::sin(2 * M_PI * phase) +
                           0.5 * std::cos(4 * M_PI * phase));
    }

    for (double volts : {5.5, 6.0, 6.5}) {
        const double f0 = 261.6255653005986 * std::exp2(volts);
        if (f0 >= sampleRate * 0.5) continue;
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts);
        const double floorDb = aliasFloorDb(out, f0, sampleRate);
        if (floorDb > -20.0) {
            detail = "at " + std::to_string(volts) + " V (" + std::to_string(f0) +
                     " Hz) the alias floor was " + std::to_string(floorDb) +
                     " dB; the coarsest table is still carrying a second harmonic";
            return false;
        }
    }
    return true;
}

/**
 * The oscillator must work at the cadence the extractor actually publishes at.
 *
 * This is the integration every other test here misses. They all offer the same
 * frame count over and over, which is not what happens: the extractor publishes
 * a new count every sixteen calls at its default budget, while this chain takes
 * about sixty-five. Restarting the build on any newer frame therefore cancelled
 * it four times over before it could finish, neither side ever became ready,
 * and the oscillator was silent for the life of the patch.
 */
bool oscAtProducerCadence(std::string& detail) {
    const int sampleRate = 48000;
    const auto frame = harmonicFrame(16);

    WavetableOsc osc;            // defaults throughout, as a module would
    osc.setSampleRate(sampleRate);
    osc.setLevel(1.f);

    // A new frame count every sixteen calls, exactly as the extractor produces.
    uint64_t count = 1;
    double peak = 0.0;
    for (int call = 0; call < 4000; call++) {
        if (call > 0 && call % 16 == 0) count++;
        osc.offerFrame(frame.data(), count);
        for (int i = 0; i < 64; i++) {
            peak = std::max(peak, std::fabs((double)osc.process(0.f)));
        }
    }
    if (peak < 0.05) {
        detail = "at the extractor's publish cadence the oscillator never made a "
                 "sound; peak was " + std::to_string(peak) +
                 ", so no chain ever finished building";
        return false;
    }
    return true;
}

/**
 * The first frame must fade in, not appear at full level.
 *
 * Before any chain is ready the output is exact silence. Declaring the first
 * completed chain current instead of fading into it takes the output from zero
 * to full level in one sample, which is a click every time a patch loads.
 */
bool oscFirstFrameFadesIn(std::string& detail) {
    const int sampleRate = 48000;
    // Cosine phase, so the frame starts at its maximum rather than at a zero
    // crossing: this is the shape that makes the jump full scale.
    std::vector<float> frame(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < frame.size(); i++) {
        frame[i] = (float)std::cos(2 * M_PI * (double)i / (double)frame.size());
    }

    WavetableOsc osc;
    osc.setSampleRate(sampleRate);
    osc.setLevel(1.f);
    osc.setMorph(0.5f);

    std::vector<float> out;
    out.reserve(20000);
    for (int call = 0; call < 300; call++) {
        osc.offerFrame(frame.data(), 1);
        for (int i = 0; i < 64; i++) out.push_back(osc.process(0.f));
    }

    const double worst = maxStep(out);
    // The steady-state step of this frame at this pitch is the bar.
    double steady = 0.0;
    for (std::size_t i = out.size() / 2 + 1; i < out.size(); i++) {
        steady = std::max(steady, std::fabs((double)out[i] - (double)out[i - 1]));
    }
    if (worst > steady * 3.0 + 0.02) {
        detail = "the first frame arrived with a step of " + std::to_string(worst) +
                 " against a steady-state step of " + std::to_string(steady);
        return false;
    }
    if (steady < 1e-6) {
        detail = "the oscillator never produced anything to measure";
        return false;
    }
    return true;
}

/**
 * A table must stop contributing when its own bandwidth crosses Nyquist.
 *
 * Blending between the two entries either side of the exact position keeps the
 * lower one for the whole octave AFTER it has stopped fitting. That is not
 * theoretical: it leaves a specific band of pitches where a high harmonic that
 * the chain is supposed to have removed is still being played.
 */
bool oscBlendCrossoverIsEarlyEnough(std::string& detail) {
    const int sampleRate = 48000;
    // A strong high harmonic and little else, so any alias is unambiguous.
    std::vector<float> frame(WavetableOsc::kFrameSize, 0.f);
    for (std::size_t i = 0; i < frame.size(); i++) {
        const double phase = (double)i / (double)frame.size();
        frame[i] = (float)(0.5 * std::sin(2 * M_PI * phase) +
                           0.5 * std::sin(2 * M_PI * 48 * phase));
    }

    // Swept finely, because the defect only shows in part of each octave: at
    // 700 Hz it measured -23.5 dB where its neighbours were past -30.
    for (double hz = 280.0; hz <= 1600.0; hz *= 1.06) {
        const double volts = std::log2(hz / 261.6255653005986);
        WavetableOsc osc;
        osc.setSampleRate(sampleRate);
        osc.setLevel(1.f);
        osc.setMorph(1.f);
        primeOsc(osc, frame);
        const auto out = renderOsc(osc, (float)volts);
        const double floorDb = aliasFloorDb(out, hz, sampleRate);
        // The bar is what the shift actually achieves on this deliberately
        // hostile frame, not a round number. Harmonic 48 sits near Nyquist at
        // these pitches, so the Catmull-Rom read leaves a floor of its own that
        // no amount of table selection can remove; the worst case measured with
        // the shift is about -25 dB, against -18.6 dB without it.
        if (floorDb > -22.0) {
            detail = "at " + std::to_string(hz) + " Hz the alias floor was " +
                     std::to_string(floorDb) +
                     " dB; a table is still contributing past its Nyquist crossing";
            return false;
        }
    }
    return true;
}

/** Building the chain must respect the budget. */
bool oscBuildIsAmortised(std::string& detail) {
    const auto frame = harmonicFrame(32);
    // Including budgets below the cost of a single filtered slot, which is a
    // whole run of the fifteen tap kernel and cannot be split across calls.
    for (int budget : {1, 8, 64, 256, 1024}) {
        WavetableOsc osc;
        osc.setSampleRate(48000);
        osc.setBudgetPerCall(budget);
        std::size_t worst = 0;
        int calls = 0;
        for (int i = 0; i < 20000; i++) {
            osc.offerFrame(frame.data(), 1);
            worst = std::max(worst, osc.debugWorkLastCall());
            calls++;
            if (osc.debugWorkLastCall() == 0) break;
        }
        if (worst > osc.effectiveBudget()) {
            detail = "budget " + std::to_string(budget) + ": one call did " +
                     std::to_string(worst) + " units against an effective bound of " +
                     std::to_string(osc.effectiveBudget());
            return false;
        }
        if (calls < 2) {
            detail = "budget " + std::to_string(budget) +
                     " built the whole chain in one call";
            return false;
        }

        // An upper bound alone does not test the accounting: charging every
        // slot one unit also stays under the budget, while doing fifteen times
        // the work at every level above zero. The number of calls is what
        // exposes it, because it follows the TRUE cost.
        //
        // Level 0 is 2048 copies at one unit each; the rest of the chain is
        // filtered, at a whole run of the kernel per slot.
        const std::size_t filtered = 4092 - WavetableOsc::kFrameSize;
        const double trueUnits = (double)WavetableOsc::kFrameSize + (double)filtered * 15.0;
        const double expected = trueUnits / (double)osc.effectiveBudget();
        if ((double)calls < expected * 0.6 || (double)calls > expected * 1.6 + 2) {
            detail = "budget " + std::to_string(budget) + ": took " +
                     std::to_string(calls) + " calls, but the chain costs " +
                     std::to_string(trueUnits) + " units so it should take about " +
                     std::to_string(expected);
            return false;
        }

        // And once the chain is finished, further offers of the same frame must
        // report no work. Leaving the counter stale made a completed build look
        // like it was still running forever.
        for (int i = 0; i < 5; i++) {
            osc.offerFrame(frame.data(), 1);
            if (osc.debugWorkLastCall() != 0) {
                detail = "budget " + std::to_string(budget) +
                         ": an offer after the build completed reported " +
                         std::to_string(osc.debugWorkLastCall()) + " units of work";
                return false;
            }
        }
    }
    return true;
}

/** No frame offered yet is the state on patch load. */
bool oscEmpty(std::string& detail) {
    WavetableOsc osc;
    osc.setSampleRate(48000);
    for (int i = 0; i < 1000; i++) {
        const float out = osc.process(0.f);
        if (out != 0.f) {
            detail = "an oscillator with no frame produced " + std::to_string(out);
            return false;
        }
    }
    // A null offer must be ignored rather than crash or half-build.
    for (int i = 0; i < 100; i++) osc.offerFrame(nullptr, 1);
    for (int i = 0; i < 100; i++) {
        if (osc.process(0.f) != 0.f) {
            detail = "a null frame offer produced output";
            return false;
        }
    }
    return true;
}

/** Hostile but legal input must not produce a non-finite sample. */
bool oscBadInput(std::string& detail) {
    const auto frame = harmonicFrame(16);
    WavetableOsc osc;
    osc.setSampleRate(48000);
    primeOsc(osc, frame);

    const float bad[] = {std::nanf(""), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), 1e20f, -1e20f,
                         std::numeric_limits<float>::max(), 100.f, -100.f};
    for (float v : bad) {
        for (int i = 0; i < 200; i++) {
            const float out = osc.process(v);
            if (!std::isfinite(out)) {
                detail = "input " + std::to_string(v) + " produced a non-finite sample";
                return false;
            }
        }
        // And a sane input straight afterwards must still work, so a bad value
        // cannot leave the phase permanently poisoned.
        float peak = 0.f;
        for (int i = 0; i < 4000; i++) peak = std::max(peak, std::fabs(osc.process(0.f)));
        if (peak < 0.05f) {
            detail = "after input " + std::to_string(v) +
                     " the oscillator fell silent, peaking at " + std::to_string(peak);
            return false;
        }
    }

    for (float v : {std::nanf(""), std::numeric_limits<float>::infinity()}) {
        osc.setMorph(v);
        osc.setCoarse(v);
        osc.setFine(v);
        osc.setLevel(v);
    }
    for (int i = 0; i < 200; i++) {
        if (!std::isfinite(osc.process(0.f))) {
            detail = "non-finite settings produced a non-finite sample";
            return false;
        }
    }
    return true;
}

/** The level control must scale the output and reach silence. */
bool oscLevel(std::string& detail) {
    const auto frame = harmonicFrame(8);
    WavetableOsc osc;
    osc.setSampleRate(48000);
    osc.setMorph(1.f);
    primeOsc(osc, frame);

    osc.setLevel(1.f);
    double full = 0.0;
    for (int i = 0; i < 8000; i++) full = std::max(full, std::fabs((double)osc.process(0.f)));
    osc.setLevel(0.5f);
    double half = 0.0;
    for (int i = 0; i < 8000; i++) half = std::max(half, std::fabs((double)osc.process(0.f)));
    osc.setLevel(0.f);
    double none = 0.0;
    for (int i = 0; i < 8000; i++) none = std::max(none, std::fabs((double)osc.process(0.f)));

    if (full < 0.1) { detail = "full level produced only " + std::to_string(full); return false; }
    const double ratio = half / std::max(full, 1e-9);
    if (std::fabs(ratio - 0.5) > 0.05) {
        detail = "half level gave a ratio of " + std::to_string(ratio);
        return false;
    }
    if (none > 1e-6) {
        detail = "zero level still produced " + std::to_string(none);
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// LowpassGate
// ---------------------------------------------------------------------------

using WiggleRoom::LowpassGate;

namespace {
/** Spectral centroid in Hz. Rises with brightness. */
double centroidHz(const std::vector<float>& x, int sampleRate) {
    const std::size_t n = 4096;
    ReferenceFft fft(n);
    std::vector<float> in(n, 0.f), spectrum(fft.spectrumLength(), 0.f);
    for (std::size_t i = 0; i < n && i < x.size(); i++) {
        const double w = 0.5 * (1.0 - std::cos(2 * M_PI * (double)i / (double)n));
        in[i] = (float)(x[i] * w);
    }
    fft.forward(in.data(), spectrum.data());
    double weighted = 0.0, total = 0.0;
    for (std::size_t b = 1; b < fft.numBins(); b++) {
        const double re = spectrum[2 * b], im = spectrum[2 * b + 1];
        const double mag = std::sqrt(re * re + im * im);
        weighted += mag * ((double)b * sampleRate / (double)n);
        total += mag;
    }
    return (total > 1e-12) ? (weighted / total) : 0.0;
}

double rmsOf(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double acc = 0.0;
    for (float v : x) acc += (double)v * v;
    return std::sqrt(acc / (double)x.size());
}

/** A bright, harmonically rich excitation. */
std::vector<float> buzz(std::size_t n, int sampleRate) {
    std::vector<float> out(n, 0.f);
    for (std::size_t i = 0; i < n; i++) {
        double v = 0.0;
        for (int k = 1; k <= 40; k++) {
            v += std::sin(2 * M_PI * 110.0 * k * (double)i / sampleRate) / k;
        }
        out[i] = (float)(v * 0.4);
    }
    return out;
}
}  // namespace

/**
 * The step response must show the vactrol's asymmetry.
 *
 * Roughly 12 ms up and 250 ms down. That 20:1 ratio is what makes a lowpass
 * gate sound plucked rather than merely gated.
 */
bool lpgStepResponse(std::string& detail) {
    const int sampleRate = 48000;
    LowpassGate gate(sampleRate);
    gate.setColour(0.5f);
    gate.setDecaySeconds(0.25f);
    gate.trigger();

    std::vector<float> envelope;
    envelope.reserve(sampleRate);
    for (int i = 0; i < sampleRate; i++) {
        gate.process(1.f);
        envelope.push_back(gate.envelope());
    }

    int at10 = -1, at90 = -1;
    for (std::size_t i = 0; i < envelope.size(); i++) {
        if (at10 < 0 && envelope[i] >= 0.1f) at10 = (int)i;
        if (at90 < 0 && envelope[i] >= 0.9f) { at90 = (int)i; break; }
    }
    if (at10 < 0 || at90 < 0) { detail = "the gate never opened"; return false; }
    const double riseMs = 1000.0 * (at90 - at10) / sampleRate;

    std::size_t peak = 0;
    for (std::size_t i = 0; i < envelope.size(); i++) {
        if (envelope[i] > envelope[peak]) peak = i;
    }
    int fallSamples = -1;
    for (std::size_t i = peak; i < envelope.size(); i++) {
        if (envelope[i] <= envelope[peak] * 0.3679f) { fallSamples = (int)(i - peak); break; }
    }
    if (fallSamples < 0) { detail = "the gate never closed"; return false; }
    const double fallMs = 1000.0 * fallSamples / sampleRate;

    if (riseMs < 6.0 || riseMs > 20.0) {
        detail = "rise was " + std::to_string(riseMs) + " ms, expected about 12";
        return false;
    }
    if (fallMs < 180.0 || fallMs > 330.0) {
        detail = "fall was " + std::to_string(fallMs) + " ms, expected about 250";
        return false;
    }
    // The ASYMMETRY is the character, so it is checked directly rather than
    // left implicit in the two figures above.
    if (fallMs / riseMs < 10.0) {
        detail = "rise and fall are only " + std::to_string(fallMs / riseMs) +
                 ":1 apart; the plucked character comes from about 20:1";
        return false;
    }
    return true;
}

/**
 * Brightness and level must fall together.
 *
 * This is what separates a lowpass gate from a VCA. A VCA closing leaves the
 * timbre alone, so a decaying note keeps its harmonics all the way down and
 * sounds synthetic.
 */
bool lpgBrightnessFallsWithLevel(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = buzz(sampleRate, sampleRate);

    auto measure = [&](float colour, double* earlyCentroid, double* lateCentroid,
                       double* earlyRms, double* lateRms) {
        LowpassGate gate(sampleRate);
        gate.setColour(colour);
        gate.setDecaySeconds(0.4f);
        gate.trigger();
        std::vector<float> out;
        out.reserve(source.size());
        for (std::size_t i = 0; i < source.size(); i++) out.push_back(gate.process(source[i]));

        const std::size_t early = 2000, late = 14000;
        std::vector<float> a(out.begin() + early, out.begin() + early + 4096);
        std::vector<float> b(out.begin() + late, out.begin() + late + 4096);
        *earlyCentroid = centroidHz(a, sampleRate);
        *lateCentroid = centroidHz(b, sampleRate);
        *earlyRms = rmsOf(a);
        *lateRms = rmsOf(b);
    };

    // Both: level AND brightness must drop.
    double ec = 0, lc = 0, er = 0, lr = 0;
    measure(0.5f, &ec, &lc, &er, &lr);
    if (lr >= er * 0.7) {
        detail = "in Both mode the level barely fell: " + std::to_string(er) + " to " +
                 std::to_string(lr);
        return false;
    }
    if (lc >= ec * 0.8) {
        detail = "in Both mode the centroid barely fell: " + std::to_string(ec) +
                 " Hz to " + std::to_string(lc) + " Hz; the decay is losing level only";
        return false;
    }

    // VCA: level drops, brightness does NOT. Without this the test above would
    // pass on any implementation that simply got quieter.
    double vec = 0, vlc = 0, ver = 0, vlr = 0;
    measure(0.f, &vec, &vlc, &ver, &vlr);
    if (vlr >= ver * 0.7) {
        detail = "in VCA mode the level barely fell";
        return false;
    }
    if (vlc < vec * 0.85) {
        detail = "in VCA mode the centroid fell from " + std::to_string(vec) + " Hz to " +
                 std::to_string(vlc) + " Hz; the VCA end should not filter";
        return false;
    }
    return true;
}

/** The three colour positions must be distinct. */
bool lpgColourContinuum(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = buzz(8192, sampleRate);

    auto steady = [&](float colour, double* rms, double* centroid) {
        LowpassGate gate(sampleRate);
        gate.setColour(colour);
        gate.setRestingLevel(0.35f);   // held part open, so nothing is decaying
        std::vector<float> out;
        for (std::size_t i = 0; i < source.size(); i++) out.push_back(gate.process(source[i]));
        std::vector<float> tail(out.end() - 4096, out.end());
        *rms = rmsOf(tail);
        *centroid = centroidHz(tail, sampleRate);
    };

    double vcaRms = 0, vcaCentroid = 0, bothRms = 0, bothCentroid = 0;
    double lpRms = 0, lpCentroid = 0;
    steady(0.f, &vcaRms, &vcaCentroid);
    steady(0.5f, &bothRms, &bothCentroid);
    steady(1.f, &lpRms, &lpCentroid);

    // At the VCA end the filter is open, so it is the brightest.
    if (vcaCentroid <= bothCentroid || vcaCentroid <= lpCentroid) {
        detail = "the VCA end is not the brightest: VCA " + std::to_string(vcaCentroid) +
                 ", Both " + std::to_string(bothCentroid) + ", Lowpass " +
                 std::to_string(lpCentroid);
        return false;
    }
    // At the lowpass end the gain stays up, so it is the loudest.
    if (lpRms <= bothRms || lpRms <= vcaRms) {
        detail = "the lowpass end is not the loudest: VCA " + std::to_string(vcaRms) +
                 ", Both " + std::to_string(bothRms) + ", Lowpass " + std::to_string(lpRms);
        return false;
    }
    return true;
}

/** The decay control must mean what it says. */
bool lpgDecayControl(std::string& detail) {
    const int sampleRate = 48000;
    for (float seconds : {0.05f, 0.25f, 1.0f, 2.0f}) {
        LowpassGate gate(sampleRate);
        gate.setColour(0.5f);
        gate.setDecaySeconds(seconds);
        gate.trigger();

        std::vector<float> envelope;
        const int limit = (int)(sampleRate * 8);
        for (int i = 0; i < limit; i++) {
            gate.process(1.f);
            envelope.push_back(gate.envelope());
        }
        std::size_t peak = 0;
        for (std::size_t i = 0; i < envelope.size(); i++) {
            if (envelope[i] > envelope[peak]) peak = i;
        }
        int fall = -1;
        for (std::size_t i = peak; i < envelope.size(); i++) {
            if (envelope[i] <= envelope[peak] * 0.3679f) { fall = (int)(i - peak); break; }
        }
        if (fall < 0) {
            detail = "a " + std::to_string(seconds) + " s decay never reached 1/e";
            return false;
        }
        const double measured = (double)fall / sampleRate;
        const double ratio = measured / seconds;
        if (ratio < 0.7 || ratio > 1.4) {
            detail = "a " + std::to_string(seconds) + " s decay measured " +
                     std::to_string(measured) + " s";
            return false;
        }
    }
    return true;
}

/** Times are in seconds, so they must not change with the sample rate. */
bool lpgSampleRate(std::string& detail) {
    double reference = 0.0;
    for (int sampleRate : {44100, 48000, 96000}) {
        LowpassGate gate(sampleRate);
        gate.setColour(0.5f);
        gate.setDecaySeconds(0.25f);
        gate.trigger();
        std::vector<float> envelope;
        for (int i = 0; i < sampleRate; i++) {
            gate.process(1.f);
            envelope.push_back(gate.envelope());
        }
        std::size_t peak = 0;
        for (std::size_t i = 0; i < envelope.size(); i++) {
            if (envelope[i] > envelope[peak]) peak = i;
        }
        int fall = -1;
        for (std::size_t i = peak; i < envelope.size(); i++) {
            if (envelope[i] <= envelope[peak] * 0.3679f) { fall = (int)(i - peak); break; }
        }
        if (fall < 0) { detail = "no decay at " + std::to_string(sampleRate); return false; }
        const double seconds = (double)fall / sampleRate;
        if (reference == 0.0) reference = seconds;
        const double ratio = seconds / reference;
        if (ratio < 0.9 || ratio > 1.1) {
            detail = "decay was " + std::to_string(reference) + " s at the first rate and " +
                     std::to_string(seconds) + " s at " + std::to_string(sampleRate);
            return false;
        }
    }
    return true;
}

/** Audio-rate control must not make it blow up. */
bool lpgAudioRateModulation(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = buzz(sampleRate / 2, sampleRate);
    for (double modHz : {50.0, 500.0, 5000.0, 20000.0}) {
        LowpassGate gate(sampleRate);
        gate.setColour(0.5f);
        gate.setDecaySeconds(0.05f);
        double peak = 0.0;
        for (std::size_t i = 0; i < source.size(); i++) {
            const double phase = 2 * M_PI * modHz * (double)i / sampleRate;
            gate.setGate((float)(0.5 + 0.5 * std::sin(phase)));
            const float out = gate.process(source[i]);
            if (!std::isfinite(out)) {
                detail = "modulation at " + std::to_string(modHz) +
                         " Hz produced a non-finite sample";
                return false;
            }
            peak = std::max(peak, std::fabs((double)out));
        }
        // The source peaks near 1, and a gate cannot add energy.
        if (peak > 2.0) {
            detail = "modulation at " + std::to_string(modHz) + " Hz reached " +
                     std::to_string(peak);
            return false;
        }
    }
    return true;
}

/** The resting level holds the gate partly open. */
bool lpgRestingLevel(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = buzz(sampleRate / 4, sampleRate);

    LowpassGate closed(sampleRate);
    closed.setColour(0.5f);
    closed.setRestingLevel(0.f);
    LowpassGate open(sampleRate);
    open.setColour(0.5f);
    open.setRestingLevel(0.5f);

    std::vector<float> closedOut, openOut;
    for (std::size_t i = 0; i < source.size(); i++) {
        closedOut.push_back(closed.process(source[i]));
        openOut.push_back(open.process(source[i]));
    }
    std::vector<float> closedTail(closedOut.end() - 4096, closedOut.end());
    std::vector<float> openTail(openOut.end() - 4096, openOut.end());

    if (rmsOf(closedTail) > 1e-4) {
        detail = "with no resting level the gate still passed " +
                 std::to_string(rmsOf(closedTail));
        return false;
    }
    if (rmsOf(openTail) < 1e-3) {
        detail = "a resting level of 0.5 passed only " + std::to_string(rmsOf(openTail));
        return false;
    }
    return true;
}

/**
 * A held gate must stay open until released.
 *
 * Treating every full-scale target as a ping meant setGate(1) began decaying
 * the moment the attack arrived. Full-scale gate signals commonly clamp to
 * exactly 1, so that is the ordinary case rather than an edge one.
 */
bool lpgHeldGateSustains(std::string& detail) {
    const int sampleRate = 48000;
    LowpassGate gate(sampleRate);
    gate.setColour(0.5f);
    gate.setDecaySeconds(0.25f);
    gate.setGate(1.f);

    for (int i = 0; i < sampleRate; i++) gate.process(1.f);
    if (gate.envelope() < 0.95f) {
        detail = "a gate held at 1.0 decayed to " + std::to_string(gate.envelope()) +
                 " after one second without any release";
        return false;
    }

    // And releasing it must still let it fall.
    gate.release();
    for (int i = 0; i < sampleRate; i++) gate.process(1.f);
    if (gate.envelope() > 0.1f) {
        detail = "after release the gate was still at " + std::to_string(gate.envelope());
        return false;
    }

    // A trigger, by contrast, must decay on its own.
    LowpassGate pinged(sampleRate);
    pinged.setColour(0.5f);
    pinged.setDecaySeconds(0.25f);
    pinged.trigger();
    for (int i = 0; i < sampleRate; i++) pinged.process(1.f);
    if (pinged.envelope() > 0.1f) {
        detail = "a trigger did not decay; envelope was " +
                 std::to_string(pinged.envelope());
        return false;
    }
    return true;
}

/**
 * Extreme but finite audio must not corrupt the filter permanently.
 *
 * Two consecutive legal samples of opposite extreme magnitude overflow the
 * subtraction inside the filter, and once a stage holds an infinity every
 * ordinary sample after it returns NaN for good. Checking finiteness on the way
 * in is not enough.
 */
bool lpgExtremeAudio(std::string& detail) {
    const int sampleRate = 48000;
    LowpassGate gate(sampleRate);
    gate.setColour(0.5f);
    gate.setRestingLevel(1.f);

    const float extremes[] = {std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max(),
                              1e30f, -1e30f, 1e20f, -1e20f};
    for (float v : extremes) {
        const float out = gate.process(v);
        if (!std::isfinite(out)) {
            detail = "an extreme finite sample produced a non-finite output";
            return false;
        }
    }

    // Ordinary audio afterwards must still work, which is what the overflow
    // broke: the filter state stayed infinite and every later sample was NaN.
    double peak = 0.0;
    for (int i = 0; i < 20000; i++) {
        const float out = gate.process(0.5f);
        if (!std::isfinite(out)) {
            detail = "ordinary audio after an extreme sample returned a non-finite value";
            return false;
        }
        peak = std::max(peak, std::fabs((double)out));
    }
    if (peak < 1e-3) {
        detail = "the filter was left dead after extreme input; peak " +
                 std::to_string(peak);
        return false;
    }
    return true;
}

/**
 * The response scale must move BOTH time constants.
 *
 * TheLantern's control scales the whole cell, so an attack fixed at 12 ms
 * leaves half of it with nothing to do.
 */
bool lpgResponseScale(std::string& detail) {
    const int sampleRate = 48000;
    auto riseMs = [&](float response) {
        LowpassGate gate(sampleRate);
        gate.setColour(0.5f);
        gate.setDecaySeconds(0.25f);
        gate.setResponseScale(response);
        gate.trigger();
        std::vector<float> envelope;
        for (int i = 0; i < sampleRate; i++) {
            gate.process(1.f);
            envelope.push_back(gate.envelope());
        }
        int at10 = -1, at90 = -1;
        for (std::size_t i = 0; i < envelope.size(); i++) {
            if (at10 < 0 && envelope[i] >= 0.1f) at10 = (int)i;
            if (at90 < 0 && envelope[i] >= 0.9f) { at90 = (int)i; break; }
        }
        if (at10 < 0 || at90 < 0) return -1.0;
        return 1000.0 * (at90 - at10) / sampleRate;
    };

    const double fast = riseMs(0.5f);
    const double normal = riseMs(1.f);
    const double slow = riseMs(2.f);
    if (fast < 0 || normal < 0 || slow < 0) {
        detail = "the gate did not open at one of the response settings";
        return false;
    }
    if (!(fast < normal && normal < slow)) {
        detail = "response did not scale the attack: " + std::to_string(fast) + ", " +
                 std::to_string(normal) + ", " + std::to_string(slow) + " ms";
        return false;
    }
    // Its documented range should span roughly 6 to 24 ms.
    if (fast > 9.0 || slow < 18.0) {
        detail = "the response range gave " + std::to_string(fast) + " to " +
                 std::to_string(slow) + " ms, expected about 6 to 24";
        return false;
    }
    return true;
}

/** Hostile input must not produce a non-finite sample. */
bool lpgBadInput(std::string& detail) {
    LowpassGate gate(48000);
    const float bad[] = {std::nanf(""), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), 1e20f, -1e20f};
    for (float v : bad) {
        gate.setColour(v);
        gate.setDecaySeconds(v);
        gate.setRestingLevel(v);
        gate.setGate(v);
        for (int i = 0; i < 500; i++) {
            if (!std::isfinite(gate.process(v))) {
                detail = "input " + std::to_string(v) + " produced a non-finite sample";
                return false;
            }
        }
        // And it must recover: a sane signal afterwards still passes.
        gate.setColour(0.5f);
        gate.setDecaySeconds(0.25f);
        gate.setRestingLevel(0.f);
        gate.trigger();
        double peak = 0.0;
        for (int i = 0; i < 4000; i++) {
            peak = std::max(peak, std::fabs((double)gate.process(0.5f)));
        }
        if (peak < 1e-3) {
            detail = "after input " + std::to_string(v) + " the gate stayed shut";
            return false;
        }
    }
    return true;
}


// ---------------------------------------------------------------------------
// GrainEngine
// ---------------------------------------------------------------------------

using WiggleRoom::stems::GrainEngine;

namespace {
std::vector<float> grainSource(std::size_t n, int sampleRate, double hz = 220.0) {
    std::vector<float> out(n, 0.f);
    for (std::size_t i = 0; i < n; i++) {
        out[i] = (float)std::sin(2 * M_PI * hz * (double)i / sampleRate);
    }
    return out;
}

struct GrainRun {
    std::vector<float> left, right;
    int peakGrains = 0;
    uint64_t dropped = 0;
};

GrainRun runGrains(GrainEngine& engine, const std::vector<float>& source, std::size_t samples) {
    GrainRun run;
    run.left.reserve(samples);
    run.right.reserve(samples);
    for (std::size_t i = 0; i < samples; i++) {
        const auto f = engine.process(source.data(), source.size());
        run.left.push_back(f.left);
        run.right.push_back(f.right);
        run.peakGrains = std::max(run.peakGrains, engine.activeGrains());
    }
    run.dropped = engine.debugDropped();
    return run;
}
}  // namespace

/**
 * The pool must cap concurrency, and a grain that cannot start must be dropped
 * rather than stealing a slot.
 *
 * Cost follows density times size, so the top of both controls is 100 Hz
 * against half a second. Stealing would cut an envelope short, which is a click
 * at the density rate; in a cloud a missing grain is inaudible.
 */
bool grainPoolIsBounded(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = grainSource(sampleRate, sampleRate);

    // Well past the worst case the controls allow, so the pool is genuinely
    // exercised rather than merely large enough.
    GrainEngine engine(sampleRate);
    engine.setDensityHz(100.f);
    engine.setSizeSeconds(0.5f);
    engine.setTexture(1.f);
    engine.setReadPosition(sampleRate / 2);

    const auto run = runGrains(engine, source, sampleRate * 3);
    if (run.peakGrains > GrainEngine::kMaxGrains) {
        detail = "concurrency reached " + std::to_string(run.peakGrains) +
                 " against a pool of " + std::to_string(GrainEngine::kMaxGrains);
        return false;
    }
    if (run.peakGrains < 8) {
        detail = "only " + std::to_string(run.peakGrains) +
                 " grains overlapped at maximum settings; the scheduler is not running";
        return false;
    }
    // The pool is sized so it is NOT exhausted at the documented maxima: 100 Hz
    // against half a second is fifty overlapping, and the jitter can stretch
    // that, so there is headroom above it. Nothing should be lost in ordinary
    // use, and the drop path is a safety net rather than a normal outcome,
    // which is why it cannot be reached through the public controls.
    if (run.dropped != 0) {
        detail = std::to_string(run.dropped) +
                 " grains were dropped at the documented maximum settings";
        return false;
    }
    return true;
}

/**
 * Level must stay roughly constant across the density and size controls.
 *
 * Grains sum, so without compensation the output rises with the overlap:
 * measured peaks of 0.90, 2.98 and 4.90 across the range, which is 14 dB of
 * level change from controls that are supposed to change texture.
 */
bool grainLevelIsStable(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = grainSource(sampleRate, sampleRate);

    struct Case { float density, size; };
    const Case cases[] = {{1.f, 0.02f},  {10.f, 0.08f}, {30.f, 0.15f},
                          {50.f, 0.2f},  {100.f, 0.5f}, {100.f, 0.05f}};
    double loudest = 0.0, quietest = 1e9;
    for (const auto& c : cases) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(c.density);
        engine.setSizeSeconds(c.size);
        engine.setTexture(0.3f);
        engine.setSpread(0.f);
        engine.setReadPosition(sampleRate / 2);
        const auto run = runGrains(engine, source, sampleRate * 2);

        // PEAK, not RMS. A sparse setting is legitimately quieter on average:
        // one 20 ms grain per second is a two per cent duty cycle, so its RMS
        // is 17 dB below a dense setting no matter how the grains are scaled.
        // What must not change is how loud the grains themselves are, and that
        // is the peak.
        double peak = 0.0;
        for (float v : run.left) peak = std::max(peak, std::fabs((double)v));
        if (peak < 1e-4) {
            detail = "density " + std::to_string(c.density) + " produced silence";
            return false;
        }
        loudest = std::max(loudest, peak);
        quietest = std::min(quietest, peak);
    }
    const double spreadDb = 20.0 * std::log10(loudest / quietest);
    if (spreadDb > 6.0) {
        detail = "grain level varies by " + std::to_string(spreadDb) +
                 " dB across the density and size controls";
        return false;
    }
    return true;
}

/** Grain boundaries must not click. */
bool grainNoClicks(std::string& detail) {
    const int sampleRate = 48000;
    // DC, so the source itself contributes no sample-to-sample change and every
    // step in the output is an envelope boundary.
    std::vector<float> source(sampleRate, 0.5f);

    for (float density : {5.f, 40.f, 100.f}) {
        for (float texture : {0.f, 0.5f, 1.f}) {
            GrainEngine engine(sampleRate);
            engine.setDensityHz(density);
            engine.setSizeSeconds(0.05f);
            engine.setTexture(texture);
            engine.setSpread(0.f);
            engine.setReadPosition(sampleRate / 2);
            const auto run = runGrains(engine, source, sampleRate);

            const double worst = maxStep(run.left);
            // A grain of 50 ms has an envelope whose steepest slope is about
            // pi/(0.05*48000) per sample. Anything an order of magnitude past
            // that is a boundary being cut rather than faded.
            if (worst > 0.01) {
                detail = "density " + std::to_string(density) + ", texture " +
                         std::to_string(texture) + ": stepped by " +
                         std::to_string(worst) + " between samples";
                return false;
            }
        }
    }
    return true;
}

/** Transposition must shift the pitch by the right amount. */
bool grainPitch(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = grainSource(sampleRate, sampleRate, 440.0);

    for (double semitones : {-12.0, 0.0, 7.0, 12.0}) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(8.f);
        engine.setSizeSeconds(0.25f);
        engine.setTexture(0.f);      // no jitter, so the pitch is clean
        engine.setSpread(0.f);
        engine.setPitchSemitones((float)semitones);
        engine.setReadPosition(1000);
        const auto run = runGrains(engine, source, 16384 * 2);

        std::vector<float> window(run.left.begin() + 8192, run.left.begin() + 8192 + 16384);
        const double measured = dominantHz(window, sampleRate);
        const double expected = 440.0 * std::pow(2.0, semitones / 12.0);
        const double cents = 1200.0 * std::log2(measured / std::max(expected, 1e-9));
        if (std::fabs(cents) > 60.0) {
            detail = std::to_string(semitones) + " semitones gave " +
                     std::to_string(measured) + " Hz, expected " +
                     std::to_string(expected);
            return false;
        }
    }
    return true;
}

/** Spread must widen the image without changing the level. */
bool grainSpread(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = grainSource(sampleRate, sampleRate);

    auto measure = [&](float spread, double* correlation, double* level) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(40.f);
        engine.setSizeSeconds(0.1f);
        engine.setTexture(0.3f);
        engine.setSpread(spread);
        engine.setReadPosition(sampleRate / 2);
        const auto run = runGrains(engine, source, sampleRate * 2);

        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (std::size_t i = 0; i < run.left.size(); i++) {
            lr += (double)run.left[i] * run.right[i];
            ll += (double)run.left[i] * run.left[i];
            rr += (double)run.right[i] * run.right[i];
        }
        *correlation = lr / std::max(1e-12, std::sqrt(ll * rr));
        *level = std::sqrt((ll + rr) / (2.0 * (double)run.left.size()));
    };

    double narrowCorr = 0, narrowLevel = 0, wideCorr = 0, wideLevel = 0;
    measure(0.f, &narrowCorr, &narrowLevel);
    measure(1.f, &wideCorr, &wideLevel);

    if (narrowCorr < 0.99) {
        detail = "at zero spread the channels correlate only " + std::to_string(narrowCorr);
        return false;
    }
    if (wideCorr >= narrowCorr - 0.02) {
        detail = "full spread barely decorrelated the channels: " +
                 std::to_string(narrowCorr) + " to " + std::to_string(wideCorr);
        return false;
    }
    // Constant power, so widening must not change the level. A linear pan law
    // dips 3 dB in the centre and reads as the cloud getting quieter as it
    // widens.
    // Half a decibel. Constant power holds it to 0.04 dB, while a linear pan
    // law shifts it by 1.13, so a looser bar would not tell them apart.
    const double changeDb = 20.0 * std::log10(wideLevel / std::max(narrowLevel, 1e-12));
    if (std::fabs(changeDb) > 0.5) {
        detail = "spread changed the level by " + std::to_string(changeDb) + " dB";
        return false;
    }
    return true;
}

/**
 * A grain crossing the end of the buffer must wrap, not fall off it.
 *
 * The read head is advanced every sample, so a grain starting near the end, or
 * reaching it quickly at positive pitch, runs past it. Reading zero there is a
 * step at full envelope gain followed by silence for the rest of the grain,
 * which is exactly the click the completion guarantee exists to prevent.
 */
bool grainCrossesBoundary(std::string& detail) {
    const int sampleRate = 48000;
    // DC, so every step in the output is the engine rather than the material.
    std::vector<float> source(8192, 0.7f);

    for (double semitones : {0.0, 12.0, 24.0}) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(40.f);
        engine.setSizeSeconds(0.1f);
        engine.setTexture(0.f);
        engine.setSpread(0.f);
        engine.setPitchSemitones((float)semitones);
        // Positioned so the read head crosses the end of the buffer at the
        // MIDDLE of the grain, where the envelope is at full gain. Starting it
        // near the end instead means the crossing happens while the envelope is
        // still ramping up, so falling off the end costs almost nothing and the
        // test passes with the wrap removed.
        const double grainSamples = 0.1 * sampleRate;
        engine.setReadPosition((double)source.size() - grainSamples * 0.5);
        const auto run = runGrains(engine, source, sampleRate);

        const double worst = maxStep(run.left);
        if (worst > 0.02) {
            detail = "at " + std::to_string(semitones) +
                     " semitones a grain crossing the buffer end stepped by " +
                     std::to_string(worst);
            return false;
        }
    }
    return true;
}

/**
 * Coherent overlap must stay bounded.
 *
 * At texture 0 every grain reads the same position, so on sustained or periodic
 * material they are near-identical and sum LINEARLY rather than as the square
 * root the compensation assumes. Left alone the peak reaches 2.5, which clips a
 * full-scale input.
 */
bool grainCoherentOverlap(std::string& detail) {
    const int sampleRate = 48000;
    std::vector<float> dc(sampleRate, 1.f);

    double worst = 0.0;
    for (float density : {1.f, 10.f, 50.f, 100.f}) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(density);
        engine.setSizeSeconds(0.5f);
        engine.setTexture(0.f);      // fully coherent: the worst case
        engine.setSpread(0.f);
        engine.setReadPosition(sampleRate / 2);
        const auto run = runGrains(engine, dc, sampleRate * 3);
        for (float v : run.left) worst = std::max(worst, std::fabs((double)v));
    }
    if (worst > 1.05) {
        detail = "fully coherent overlap reached " + std::to_string(worst) +
                 " with a full-scale input";
        return false;
    }
    return true;
}

/**
 * The shortest grains must still end on silence.
 *
 * Jitter can cut a grain to half a millisecond, which is 24 samples at 48 kHz.
 * The last phase rendered is one increment short of 1, and with the flattened
 * texture-1 window that sample still carried 0.37 of full gain, so the grain
 * ended on a step. The fade is therefore a minimum in SAMPLES, not a fraction.
 */
bool grainShortGrains(std::string& detail) {
    const int sampleRate = 48000;
    std::vector<float> dc(sampleRate, 1.f);

    for (float size : {0.001f, 0.002f, 0.005f}) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(100.f);
        engine.setSizeSeconds(size);
        engine.setTexture(1.f);      // maximum jitter, flattest window
        engine.setSpread(0.f);
        engine.setReadPosition(sampleRate / 2);
        const auto run = runGrains(engine, dc, sampleRate);

        // The bar has to sit above the envelope's OWN slope. A 24 sample grain
        // travels from full scale to zero in twelve samples, so about 0.09 per
        // sample is the envelope working correctly, not a click. Truncating
        // instead gives a cliff of 0.42, so the two are far apart and the line
        // goes between them rather than below both.
        const double worst = maxStep(run.left);
        if (worst > 0.15) {
            detail = "at " + std::to_string(size * 1000.f) +
                     " ms with full texture, grains stepped by " +
                     std::to_string(worst);
            return false;
        }
    }
    return true;
}

/** A sample rate change must not alter the duration of live grains. */
bool grainSampleRateChange(std::string& detail) {
    const int sampleRate = 48000;
    std::vector<float> dc(sampleRate, 1.f);

    GrainEngine engine(sampleRate);
    engine.setDensityHz(4.f);
    engine.setSizeSeconds(0.25f);
    engine.setTexture(0.f);
    engine.setSpread(0.f);
    engine.setReadPosition(1000);

    // Get a grain running, then change the rate underneath it. At 4 Hz the
    // first one starts a quarter of a second in, so this has to run past that.
    for (int i = 0; i < 14000; i++) engine.process(dc.data(), dc.size());
    const int before = engine.activeGrains();
    if (before < 1) { detail = "no grain was running before the rate change"; return false; }

    // Count the SAMPLES the live grain still needs. Merely checking that it is
    // still running does not work: without rescaling it takes the same number
    // of samples as before, which at the new rate is half the seconds, and any
    // short window sees it running either way.
    engine.setDensityHz(0.1f);        // no new grains during the measurement
    engine.setSampleRate(96000);
    int remaining = 0;
    while (remaining < 400000 && engine.activeGrains() > 0) {
        engine.process(dc.data(), dc.size());
        remaining++;
    }
    if (remaining >= 400000) {
        detail = "the grain never finished after the rate change";
        return false;
    }

    // The same measurement without a rate change, as the reference.
    GrainEngine reference(sampleRate);
    reference.setDensityHz(4.f);
    reference.setSizeSeconds(0.25f);
    reference.setTexture(0.f);
    reference.setSpread(0.f);
    reference.setReadPosition(1000);
    for (int i = 0; i < 14000; i++) reference.process(dc.data(), dc.size());
    reference.setDensityHz(0.1f);
    int referenceRemaining = 0;
    while (referenceRemaining < 400000 && reference.activeGrains() > 0) {
        reference.process(dc.data(), dc.size());
        referenceRemaining++;
    }

    // Doubling the rate must double the samples left, since the duration is in
    // seconds. Leaving the increment alone keeps the sample count the same.
    const double ratio = (double)remaining / std::max(1, referenceRemaining);
    if (ratio < 1.6 || ratio > 2.4) {
        detail = "after doubling the sample rate the grain needed " +
                 std::to_string(remaining) + " samples against " +
                 std::to_string(referenceRemaining) + " at the original rate, a ratio of " +
                 std::to_string(ratio) + "; durations are configured in seconds";
        return false;
    }
    return true;
}

/**
 * A two-frame source must play both frames.
 *
 * Wrapping modulo length - 1 drops the interval between the last frame and the
 * first, so the last frame can never be the lower interpolation tap and the
 * loop period is one short. At the smallest valid size the span becomes 1, every
 * position collapses to zero, and only the first frame is ever heard.
 */
bool grainWrapsEveryFrame(std::string& detail) {
    const int sampleRate = 48000;

    // Two frames of opposite sign: if only the first is read, the output never
    // goes negative.
    std::vector<float> two{1.f, -1.f};
    GrainEngine engine(sampleRate);
    engine.setDensityHz(50.f);
    engine.setSizeSeconds(0.05f);
    engine.setTexture(0.f);
    engine.setSpread(0.f);
    engine.setPitchSemitones(-24.f);   // slow enough to dwell on each frame
    engine.setReadPosition(0.0);
    const auto run = runGrains(engine, two, sampleRate);

    double lowest = 0.0, highest = 0.0;
    for (float v : run.left) {
        lowest = std::min(lowest, (double)v);
        highest = std::max(highest, (double)v);
    }
    if (highest < 0.05) {
        detail = "a two frame source never produced a positive sample";
        return false;
    }
    // SYMMETRY, not merely a negative excursion. Two frames of +1 and -1 should
    // swing equally either way. Wrapping one short leaves the last frame as an
    // interpolation partner only, never the primary tap, so the negative side
    // reaches half the positive one: -0.293 against +0.585. Requiring only
    // "some negative output" passes on that.
    if (std::fabs(lowest) < highest * 0.8) {
        detail = "a two frame source swung to " + std::to_string(lowest) +
                 " against " + std::to_string(highest) +
                 "; the last frame is never the primary tap";
        return false;
    }

    // A longer buffer must cover its whole length, including the last frame.
    const std::size_t n = 64;
    std::vector<float> ramp(n, 0.f);
    for (std::size_t i = 0; i < n; i++) ramp[i] = (float)i / (float)(n - 1);
    GrainEngine wide(sampleRate);
    wide.setDensityHz(20.f);
    wide.setSizeSeconds(0.2f);
    wide.setTexture(0.f);
    wide.setSpread(0.f);
    wide.setReadPosition(0.0);
    const auto wideRun = runGrains(wide, ramp, sampleRate * 2);
    double peak = 0.0;
    for (float v : wideRun.left) peak = std::max(peak, (double)v);
    if (peak < 0.5) {
        detail = "a ramp buffer only reached " + std::to_string(peak) +
                 "; the end of the source is not being read";
        return false;
    }
    return true;
}

/** Missing, short and malformed input must be safe. */
bool grainBadInput(std::string& detail) {
    const int sampleRate = 48000;
    GrainEngine engine(sampleRate);
    engine.setDensityHz(50.f);
    engine.setReadPosition(100);

    // No source: the state on patch load.
    for (int i = 0; i < 1000; i++) {
        const auto f = engine.process(nullptr, 48000);
        if (f.left != 0.f || f.right != 0.f) {
            detail = "a null source produced output";
            return false;
        }
    }
    std::vector<float> tiny(1, 0.5f);
    for (int i = 0; i < 1000; i++) engine.process(tiny.data(), tiny.size());

    // A source containing non-finite samples must not poison the output.
    std::vector<float> poisoned = grainSource(4096, sampleRate);
    poisoned[100] = std::nanf("");
    poisoned[2000] = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 20000; i++) {
        const auto f = engine.process(poisoned.data(), poisoned.size());
        if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
            detail = "a poisoned source produced a non-finite sample";
            return false;
        }
    }

    // Non-finite settings must be rejected rather than clamped.
    const float bad[] = {std::nanf(""), std::numeric_limits<float>::infinity(), -1e20f};
    for (float v : bad) {
        engine.setDensityHz(v);
        engine.setSizeSeconds(v);
        engine.setPitchSemitones(v);
        engine.setTexture(v);
        engine.setSpread(v);
        engine.setReadPosition((double)v);
    }
    const auto clean = grainSource(4096, sampleRate);
    for (int i = 0; i < 20000; i++) {
        const auto f = engine.process(clean.data(), clean.size());
        if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
            detail = "non-finite settings produced a non-finite sample";
            return false;
        }
    }
    return true;
}

/** Position jitter must scatter into the buffer, not pile up at its ends. */
bool grainJitterWraps(std::string& detail) {
    const int sampleRate = 48000;
    const auto source = grainSource(4096, sampleRate);

    // Compared against a read position in the MIDDLE, where no grain needs to
    // wrap.
    //
    // Worth being straight that this bounds the behaviour rather than pinning
    // the choice: clamping stray grains to the buffer edge instead of wrapping
    // them also lands within this tolerance, so the test does not distinguish
    // the two. Wrapping is kept because piling every out-of-range grain onto
    // one sample is the wrong thing to do to the material, not because a
    // measurement here demands it. What the test does catch is grains landing
    // outside the buffer entirely and reading silence.
    auto levelAt = [&](double position) {
        GrainEngine engine(sampleRate);
        engine.setDensityHz(100.f);
        engine.setSizeSeconds(0.02f);
        engine.setTexture(1.f);
        engine.setSpread(0.f);
        engine.setReadPosition(position);
        const auto run = runGrains(engine, source, sampleRate * 2);
        double acc = 0.0;
        for (float v : run.left) acc += (double)v * v;
        return std::sqrt(acc / (double)run.left.size());
    };

    const double middle = levelAt((double)source.size() / 2.0);
    const double start = levelAt(0.0);
    if (middle < 1e-4) { detail = "the mid-buffer reference was silent"; return false; }
    const double changeDb = 20.0 * std::log10(start / std::max(middle, 1e-12));
    if (std::fabs(changeDb) > 2.0) {
        detail = "reading at the buffer start differs from mid-buffer by " +
                 std::to_string(changeDb) +
                 " dB; jitter is not wrapping into the material";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Diffusion
// ---------------------------------------------------------------------------

using WiggleRoom::stems::Diffusion;

namespace {
/** Impulse response of the network, wet only. */
std::vector<float> diffusionTail(Diffusion& reverb, std::size_t samples,
                                 std::size_t burst = 1) {
    std::vector<float> out;
    out.reserve(samples);
    for (std::size_t i = 0; i < samples; i++) {
        const float in = (i < burst) ? 1.f : 0.f;
        out.push_back(reverb.process(in, in).left);
    }
    return out;
}

/** Time from the peak until the envelope stays below -60 dB, in seconds. */
double decayTimeSeconds(const std::vector<float>& tail, int sampleRate) {
    double peak = 0.0;
    for (float v : tail) peak = std::max(peak, std::fabs((double)v));
    if (peak < 1e-12) return -1.0;
    const double threshold = peak * 0.001;
    for (std::size_t i = tail.size(); i-- > 0;) {
        if (std::fabs((double)tail[i]) > threshold) return (double)i / sampleRate;
    }
    return 0.0;
}
}  // namespace

/** The decay control must set the decay. */
bool diffusionDecayTracks(std::string& detail) {
    const int sampleRate = 48000;
    double previous = -1.0;
    // Up to eight seconds, because a ceiling that is too low only shows at the
    // top: at 0.93 the four and eight second settings both measured about four,
    // and a test stopping at four would have called that correct.
    for (float seconds : {0.2f, 0.5f, 1.f, 2.f, 4.f, 8.f}) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(seconds);
        reverb.setMix(1.f);
        reverb.setDamping(0.f);
        const auto tail = diffusionTail(reverb, (std::size_t)(sampleRate * 30));
        const double measured = decayTimeSeconds(tail, sampleRate);
        if (measured < 0.0) { detail = "the network produced nothing"; return false; }

        const double ratio = measured / seconds;
        if (ratio < 0.7 || ratio > 1.5) {
            detail = "a " + std::to_string(seconds) + " s decay measured " +
                     std::to_string(measured) + " s";
            return false;
        }
        // Monotonic, so the control cannot saturate part way up its range. The
        // safety ceiling on the loop gain was originally low enough that four
        // and eight seconds both measured about four.
        if (measured <= previous) {
            detail = "decay stopped increasing at " + std::to_string(seconds) +
                     " s: measured " + std::to_string(measured) + " after " +
                     std::to_string(previous);
            return false;
        }
        previous = measured;
    }
    return true;
}

/** At maximum decay it must still be a reverb, not an oscillator. */
bool diffusionNoRunaway(std::string& detail) {
    const int sampleRate = 48000;
    for (float damping : {0.f, 0.5f, 0.9f}) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(10.f);
        reverb.setMix(1.f);
        reverb.setDamping(damping);

        double peak = 0.0, tailLevel = 0.0;
        const std::size_t total = (std::size_t)sampleRate * 60;
        for (std::size_t i = 0; i < total; i++) {
            const float in = (i < 64) ? 1.f : 0.f;
            const auto f = reverb.process(in, in);
            if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
                detail = "the network produced a non-finite sample";
                return false;
            }
            peak = std::max(peak, std::fabs((double)f.left));
            if (i > (std::size_t)sampleRate * 55) {
                tailLevel = std::max(tailLevel, std::fabs((double)f.left));
            }
        }
        if (tailLevel > 1e-6) {
            detail = "at damping " + std::to_string(damping) +
                     " the tail was still at " + std::to_string(tailLevel) +
                     " a minute after the input stopped";
            return false;
        }
        if (peak > 4.0) {
            detail = "peak reached " + std::to_string(peak);
            return false;
        }
    }
    return true;
}

/** Decay is in seconds, so it must not change with the sample rate. */
bool diffusionSampleRate(std::string& detail) {
    double reference = 0.0;
    for (int sampleRate : {44100, 48000, 96000}) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(1.f);
        reverb.setMix(1.f);
        reverb.setDamping(0.f);
        const auto tail = diffusionTail(reverb, (std::size_t)(sampleRate * 8));
        const double measured = decayTimeSeconds(tail, sampleRate);
        if (measured < 0.0) {
            detail = "no output at " + std::to_string(sampleRate);
            return false;
        }
        if (reference == 0.0) reference = measured;
        const double ratio = measured / reference;
        if (ratio < 0.8 || ratio > 1.25) {
            detail = "decay was " + std::to_string(reference) + " s at the first rate and " +
                     std::to_string(measured) + " s at " + std::to_string(sampleRate);
            return false;
        }
    }
    return true;
}

/** Mix must reach fully dry and fully wet. */
bool diffusionMix(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion dry(sampleRate);
    dry.setMix(0.f);
    dry.setDecaySeconds(2.f);
    for (int i = 0; i < 1000; i++) {
        const float in = (i == 0) ? 1.f : 0.f;
        const auto f = dry.process(in, in);
        if (std::fabs(f.left - in) > 1e-6f) {
            detail = "at mix 0 the output differed from the input by " +
                     std::to_string(std::fabs(f.left - in));
            return false;
        }
    }

    Diffusion wet(sampleRate);
    wet.setMix(1.f);
    wet.setDecaySeconds(2.f);
    const auto tail = diffusionTail(wet, 48000);
    double late = 0.0;
    for (std::size_t i = 4000; i < tail.size(); i++) {
        late = std::max(late, std::fabs((double)tail[i]));
    }
    if (late < 1e-4) {
        detail = "at mix 1 there was no tail after the direct sound";
        return false;
    }
    return true;
}

/** Damping must take brightness out of the tail. */
bool diffusionDamping(std::string& detail) {
    const int sampleRate = 48000;
    auto tailCentroid = [&](float damping) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(3.f);
        reverb.setMix(1.f);
        reverb.setDamping(damping);
        // White noise in, so there is content at every frequency to remove.
        std::mt19937 rng(4242);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        std::vector<float> out;
        for (int i = 0; i < sampleRate; i++) {
            const float in = (i < sampleRate / 4) ? dist(rng) : 0.f;
            out.push_back(reverb.process(in, in).left);
        }
        std::vector<float> late(out.begin() + sampleRate / 2,
                                out.begin() + sampleRate / 2 + 4096);
        return centroidHz(late, sampleRate);
    };

    const double bright = tailCentroid(0.f);
    const double dull = tailCentroid(0.9f);
    if (bright < 100.0) { detail = "the undamped tail had no content"; return false; }
    if (dull >= bright * 0.9) {
        detail = "damping barely changed the tail: centroid " + std::to_string(bright) +
                 " Hz to " + std::to_string(dull) + " Hz";
        return false;
    }
    return true;
}

/** The two channels must not be identical, or it is not a space. */
bool diffusionStereo(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion reverb(sampleRate);
    reverb.setDecaySeconds(2.f);
    reverb.setMix(1.f);
    reverb.setDamping(0.3f);

    std::vector<float> left, right;
    for (int i = 0; i < sampleRate; i++) {
        const float in = (i < 64) ? 1.f : 0.f;
        const auto f = reverb.process(in, in);
        left.push_back(f.left);
        right.push_back(f.right);
    }
    // Measured over the TAIL, since the direct sound is the same on both sides
    // by construction and would mask any decorrelation behind it.
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = 4000; i < left.size(); i++) {
        lr += (double)left[i] * right[i];
        ll += (double)left[i] * left[i];
        rr += (double)right[i] * right[i];
    }
    if (ll < 1e-9 || rr < 1e-9) { detail = "one channel was silent"; return false; }
    const double correlation = lr / std::sqrt(ll * rr);
    if (correlation > 0.9) {
        detail = "the two channels correlate at " + std::to_string(correlation) +
                 "; the tail is effectively mono";
        return false;
    }
    return true;
}

/**
 * A recirculating network never recovers from an infinity, so extreme finite
 * input must be clamped rather than merely checked for finiteness.
 */
bool diffusionBadInput(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion reverb(sampleRate);
    reverb.setMix(1.f);
    reverb.setDecaySeconds(2.f);

    const float bad[] = {std::numeric_limits<float>::max(),
                         -std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::nanf(""), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), 1e30f, -1e30f};
    for (float v : bad) {
        const auto f = reverb.process(v, v);
        if (!std::isfinite(f.left) || !std::isfinite(f.right)) {
            detail = "input " + std::to_string(v) + " produced a non-finite sample";
            return false;
        }
    }

    // Ordinary audio afterwards must still work, which is what an infinity
    // trapped in the feedback path destroys permanently.
    double peak = 0.0;
    for (int i = 0; i < 40000; i++) {
        const float in = (i < 100) ? 0.5f : 0.f;
        const auto f = reverb.process(in, in);
        if (!std::isfinite(f.left)) {
            detail = "ordinary audio after extreme input returned a non-finite value";
            return false;
        }
        peak = std::max(peak, std::fabs((double)f.left));
    }
    if (peak < 1e-3) {
        detail = "the network was left dead after extreme input";
        return false;
    }

    for (float v : {std::nanf(""), std::numeric_limits<float>::infinity(), -1e20f}) {
        reverb.setDecaySeconds(v);
        reverb.setMix(v);
        reverb.setDamping(v);
    }
    for (int i = 0; i < 1000; i++) {
        if (!std::isfinite(reverb.process(0.3f, 0.3f).left)) {
            detail = "non-finite settings produced a non-finite sample";
            return false;
        }
    }
    return true;
}

/**
 * A sample rate change must not drop the tail.
 *
 * Clearing every delay line on a rate change is an audible hole exactly where
 * the spec asks for no dropout. The stored samples are at the old rate, so the
 * tail is briefly the wrong length, which is a far smaller artefact.
 */
bool diffusionKeepsTailAcrossRateChange(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion reverb(sampleRate);
    reverb.setDecaySeconds(4.f);
    reverb.setMix(1.f);
    reverb.setDamping(0.2f);

    // Establish a tail.
    for (int i = 0; i < sampleRate / 2; i++) {
        const float in = (i < 256) ? 1.f : 0.f;
        reverb.process(in, in);
    }
    double before = 0.0;
    for (int i = 0; i < 2000; i++) {
        before = std::max(before, std::fabs((double)reverb.process(0.f, 0.f).left));
    }
    if (before < 1e-4) { detail = "no tail before the rate change"; return false; }

    reverb.setSampleRate(96000);
    double after = 0.0;
    for (int i = 0; i < 2000; i++) {
        after = std::max(after, std::fabs((double)reverb.process(0.f, 0.f).left));
    }
    if (after < before * 0.1) {
        detail = "the tail fell from " + std::to_string(before) + " to " +
                 std::to_string(after) + " across a rate change";
        return false;
    }
    return true;
}

/**
 * Every comb must decay at the rate the control asks for.
 *
 * RT60 is proportional to a comb's own delay, so one gain taken from the longest
 * line leaves every other loop decaying at the wrong rate: the offset right
 * channel ran about 17 per cent long and the shorter combs died early.
 */
bool diffusionPerCombDecay(std::string& detail) {
    const int sampleRate = 48000;
    for (float seconds : {1.f, 4.f}) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(seconds);
        reverb.setMix(1.f);
        reverb.setDamping(0.f);

        std::vector<float> left, right;
        const std::size_t total = (std::size_t)(sampleRate * 20);
        for (std::size_t i = 0; i < total; i++) {
            const float in = (i < 1) ? 1.f : 0.f;
            const auto f = reverb.process(in, in);
            left.push_back(f.left);
            right.push_back(f.right);
        }
        const double leftDecay = decayTimeSeconds(left, sampleRate);
        const double rightDecay = decayTimeSeconds(right, sampleRate);
        if (leftDecay < 0 || rightDecay < 0) {
            detail = "one channel produced nothing";
            return false;
        }
        // The two sides use different delay lengths, so a single shared gain
        // makes them decay at visibly different rates.
        const double ratio = rightDecay / std::max(leftDecay, 1e-9);
        if (ratio < 0.88 || ratio > 1.14) {
            detail = "at " + std::to_string(seconds) + " s the left tail ran " +
                     std::to_string(leftDecay) + " s and the right " +
                     std::to_string(rightDecay) + " s, a ratio of " +
                     std::to_string(ratio);
            return false;
        }
    }
    return true;
}

/**
 * The allpass stages must not colour the material.
 *
 * Writing the recurrence without the gain on the feed-forward term gives an
 * impulse response of -1, 1, 0.5, whose magnitude response is nowhere near
 * flat, so four in series amplify broadband material rather than scattering it.
 */
bool diffusionAllpassIsFlat(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion reverb(sampleRate);
    reverb.setDecaySeconds(0.05f);   // shortest tail, so the combs contribute least
    reverb.setMix(1.f);
    reverb.setDamping(0.f);

    // White noise in: an allpass chain must not change its level.
    std::mt19937 rng(31415);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    double inputEnergy = 0.0, outputEnergy = 0.0;
    const std::size_t n = (std::size_t)sampleRate;
    for (std::size_t i = 0; i < n; i++) {
        const float in = dist(rng);
        const auto f = reverb.process(in, in);
        inputEnergy += (double)in * in;
        outputEnergy += (double)f.left * f.left;
    }
    // The network must not ADD energy to broadband material. A correct chain
    // measures -6.35 dB at this setting; the recurrence without the gain on the
    // feed-forward term measures +8.41, so zero separates them cleanly.
    const double gainDb = 10.0 * std::log10(outputEnergy / std::max(inputEnergy, 1e-30));
    if (gainDb > 0.0) {
        detail = "the network added " + std::to_string(gainDb) +
                 " dB to broadband material; the allpass stages are not flat";
        return false;
    }
    return true;
}

/** The tail must reach exact zero rather than idling in denormals. */
bool diffusionFlushesDenormals(std::string& detail) {
    const int sampleRate = 48000;
    Diffusion reverb(sampleRate);
    reverb.setDecaySeconds(10.f);
    reverb.setMix(1.f);
    reverb.setDamping(0.f);

    for (int i = 0; i < 256; i++) reverb.process(1.f, 1.f);
    // Long enough to be well past the point where a float tail becomes
    // subnormal, which at this setting starts around 98 seconds.
    const std::size_t total = (std::size_t)sampleRate * 150;
    for (std::size_t i = 0; i < total; i++) reverb.process(0.f, 0.f);

    for (int i = 0; i < 4096; i++) {
        const auto f = reverb.process(0.f, 0.f);
        if (f.left != 0.f || f.right != 0.f) {
            detail = "after 150 s of silence the network still emits " +
                     std::to_string(f.left) +
                     "; the feedback state is idling in denormals";
            return false;
        }
    }
    return true;
}

/**
 * The tail must not acquire a pitch.
 *
 * Shared periods between delay lines make several loops reinforce at a regular
 * interval, which is heard as flutter. Worth being straight about the limits of
 * this check: it bounds how peaky the tail's spectrum gets, and it does NOT on
 * its own distinguish the coprimality adjustment from the raw millisecond
 * constants, which already avoid the worst collisions. The adjustment is kept
 * because the claim in the header should be true rather than nearly true.
 */
bool diffusionCoprimeLengths(std::string& detail) {
    const int rates[] = {44100, 48000, 96000};
    for (int sampleRate : rates) {
        Diffusion reverb(sampleRate);
        reverb.setDecaySeconds(3.f);
        reverb.setMix(1.f);
        reverb.setDamping(0.f);

        std::vector<float> tail;
        const std::size_t total = (std::size_t)sampleRate;
        for (std::size_t i = 0; i < total; i++) {
            const float in = (i < 1) ? 1.f : 0.f;
            tail.push_back(reverb.process(in, in).left);
        }
        std::vector<float> late(tail.begin() + total / 4, tail.begin() + total / 4 + 16384);

        ReferenceFft fft(16384);
        std::vector<float> in(16384, 0.f), spectrum(fft.spectrumLength(), 0.f);
        for (std::size_t i = 0; i < in.size(); i++) {
            const double w = 0.5 * (1.0 - std::cos(2 * M_PI * (double)i / in.size()));
            in[i] = (float)(late[i] * w);
        }
        fft.forward(in.data(), spectrum.data());

        double peak = 0.0, mean = 0.0;
        int counted = 0;
        for (std::size_t b = 8; b < fft.numBins() / 4; b++) {
            const double re = spectrum[2 * b], im = spectrum[2 * b + 1];
            const double mag = std::sqrt(re * re + im * im);
            peak = std::max(peak, mag);
            mean += mag;
            counted++;
        }
        mean /= std::max(1, counted);
        const double crest = peak / std::max(mean, 1e-20);
        if (crest > 60.0) {
            detail = "at " + std::to_string(sampleRate) +
                     " Hz the tail's spectrum has a crest factor of " +
                     std::to_string(crest) + "; loops are reinforcing at a common period";
            return false;
        }
    }
    return true;
}

/** A sample rate change must not reallocate on the audio thread. */
bool diffusionNoAlloc(std::string& detail) {
    Diffusion reverb(48000);
    reverb.setMix(1.f);
    // Storage is sized once for the highest supported rate, so moving between
    // rates must not need more.
    for (int rate : {44100, 48000, 88200, 96000, 176400, 192000, 48000}) {
        reverb.setSampleRate(rate);
        for (int i = 0; i < 2000; i++) {
            const float in = (i == 0) ? 1.f : 0.f;
            const auto f = reverb.process(in, in);
            if (!std::isfinite(f.left)) {
                detail = "rate " + std::to_string(rate) + " produced a non-finite sample";
                return false;
            }
        }
    }
    return true;
}

struct TestCase {
    const char* cmd;
    const char* name;
    bool (*fn)(std::string&);
};

/**
 * Every check with the plain bool(std::string&) signature.
 *
 * Dispatch, --list-commands and --self-test all read this ONE table. They used
 * to be three separate lists, and they drifted: five worker checks were added to
 * dispatch and to --list-commands but not to the hand-written --self-test
 * sequence, so --self-test kept reporting the old count and silently skipped
 * them. A single table cannot drift from itself.
 */
const TestCase kCases[] = {
    {"--test-buffer-roundtrip",   "buffer_roundtrip",   bufferRoundtrip},
    {"--test-buffer-wraparound",  "buffer_wraparound",  bufferWraparound},
    {"--test-buffer-no-alloc",    "buffer_no_alloc",    bufferNoAlloc},
    {"--test-buffer-interpolate", "buffer_interpolate", bufferInterpolate},
    {"--test-buffer-capacity",    "buffer_capacity",    bufferCapacity},
    {"--test-buffer-non-finite",  "buffer_non_finite",  bufferNonFinite},
    {"--test-buffer-clear-cheap", "buffer_clear_cheap", bufferClearIsCheap},
    {"--test-transport-lock",     "transport_lock",     transportLock},
    {"--test-transport-reset",    "transport_reset",    transportReset},
    {"--test-transport-division", "transport_division", transportDivision},
    {"--test-transport-loop",     "transport_loop",     transportLoop},
    {"--test-transport-no-clock", "transport_no_clock", transportNoClock},
    {"--test-transport-division-snap",   "transport_division_snap",   transportDivisionSnap},
    {"--test-transport-reset-coincident","transport_reset_coincident",transportResetCoincident},
    {"--test-transport-loop-max-start",  "transport_loop_max_start",  transportLoopMaxStart},
    {"--test-transport-samplerate",      "transport_samplerate",      transportSampleRateChange},
    {"--test-transport-clock-restart",   "transport_clock_restart",   transportClockRestart},
    {"--test-transport-downbeat-jitter", "transport_downbeat_jitter", transportDownbeatJitter},
    {"--test-transport-reset-midinterval","transport_reset_midinterval",transportResetMidInterval},
    {"--test-stft-reconstruct",   "stft_reconstruct",   stftReconstructDefault},
    {"--test-stft-odd-lengths",   "stft_odd_lengths",   stftReconstructOddLengths},
    {"--test-stft-zero-mask",     "stft_zero_mask",     stftZeroMask},
    {"--test-stft-short-input",   "stft_short_input",   stftShortInput},
    {"--test-stft-cola",          "stft_cola",          stftCola},
    {"--test-stft-tiny-fft",      "stft_tiny_fft",      stftTinyFft},
    {"--test-hpss-separates",     "hpss_separates",     hpssSeparates},
    {"--test-hpss-sum",           "hpss_sum",           hpssLayersSumToSource},
    {"--test-hpss-low-band",      "hpss_low_band",      hpssLowBand},
    {"--test-hpss-degenerate",    "hpss_degenerate",    hpssDegenerate},
    {"--test-hpss-residual",      "hpss_residual",      hpssResidualPopulated},
    {"--test-hpss-low-split-boundary","hpss_low_split_boundary",hpssLowSplitBoundary},
    {"--test-hpss-margin",        "hpss_margin",        hpssMarginBeforeExponent},
    {"--test-hpss-subframe",      "hpss_subframe",      hpssSubFrameInput},
    {"--test-worker-publishes",   "worker_publishes",   workerPublishes},
    {"--test-worker-stale",       "worker_stale",       workerDiscardsStale},
    {"--test-worker-retire",      "worker_retire",      workerRetiresOffAudioThread},
    {"--test-worker-empty",       "worker_empty",       workerEmptyAcquire},
    {"--test-worker-stereo",      "worker_stereo",      workerStereo},
    {"--test-worker-failure",     "worker_failure",     workerSeparationFailureIsNonFatal},
    {"--test-worker-fft-injection", "worker_fft_injection", workerUsesInjectedFft},
    {"--test-worker-abort",       "worker_abort",       workerAbortsInFlightSeparation},
    {"--test-worker-reclaim-release", "worker_reclaim_release", workerReclaimsAfterReaderLeaves},
    {"--test-worker-hammer",      "worker_hammer",      workerConcurrentHammer},
    {"--test-worker-shutdown",    "worker_shutdown",    workerCleanShutdown},
    {"--test-mixer-unity-sum",    "mixer_unity_sum",    mixerUnitySum},
    {"--test-mixer-mute",         "mixer_mute",         mixerMuteIsClickFree},
    {"--test-mixer-select",       "mixer_select",       mixerSelectIsContinuous},
    {"--test-mixer-select-rapid", "mixer_select_rapid", mixerRapidSelectIsContinuous},
    {"--test-mixer-withdraw",     "mixer_withdraw",     mixerStemsWithdrawalFadesOut},
    {"--test-mixer-fallback",     "mixer_fallback",     mixerFallbackIsSingleChannel},
    {"--test-mixer-fallback-level","mixer_fallback_level",mixerFallbackLevelIsPreserved},
    {"--test-mixer-crossfade",    "mixer_crossfade",    mixerSourceCrossfadeRamps},
    {"--test-mixer-stereo",       "mixer_stereo",       mixerStereoAndMono},
    {"--test-mixer-non-finite",   "mixer_non_finite",   mixerNonFinitePosition},
    {"--test-mixer-empty",        "mixer_empty",        mixerEmptySet},
    {"--test-mixer-samplerate",   "mixer_samplerate",   mixerFadeIsSampleRateInvariant},
    {"--test-yin-sine-range",     "yin_sine_range",     yinSineRange},
    {"--test-yin-noise",          "yin_noise",          yinNoiseConfidence},
    {"--test-yin-octave",         "yin_octave",         yinNoOctaveError},
    {"--test-yin-silence",        "yin_silence",        yinSilence},
    {"--test-yin-non-finite",     "yin_non_finite",     yinNonFinite},
    {"--test-yin-samplerate",     "yin_samplerate",     yinSampleRateInvariant},
    {"--test-yin-range",          "yin_range",          yinRespectsRange},
    {"--test-yin-range-exact",    "yin_range_exact",    yinRangeIsExact},
    {"--test-yin-narrow-range",   "yin_narrow_range",   yinNarrowRange},
    {"--test-yin-bad-params",     "yin_bad_params",     yinNonFiniteParams},
    {"--test-yin-no-alloc",       "yin_no_alloc",       yinNoAlloc},
    {"--test-scale-c-major",      "scale_c_major",      scaleCMajor},
    {"--test-scale-transpose",    "scale_transpose",    scaleTranspositions},
    {"--test-scale-relative",     "scale_relative",     scaleRelativeMinor},
    {"--test-scale-chromatic",    "scale_chromatic",    scaleChromaticFloor},
    {"--test-scale-low-conf",     "scale_low_conf",     scaleLowConfidenceIsIgnored},
    {"--test-scale-unpitched",    "scale_unpitched",    scaleUnpitchedHolds},
    {"--test-scale-weight-gate",  "scale_weight_gate",  scaleWeightGate},
    {"--test-scale-seed",         "scale_seed",         scaleSeeding},
    {"--test-scale-key-change",   "scale_key_change",   scaleFollowsAKeyChange},
    {"--test-scale-inactive",     "scale_inactive",     scaleReportsInactivity},
    {"--test-scale-decay-gate",   "scale_decay_gate",   scaleDecayGateIsReachable},
    {"--test-scale-sustained",    "scale_sustained",    scaleSustainedNoteIsNotAKey},
    {"--test-scale-yin-gate",     "scale_yin_gate",     scaleGateMatchesYin},
    {"--test-scale-flat",         "scale_flat",         scaleFlatHistogram},
    {"--test-scale-bad-input",    "scale_bad_input",    scaleBadInput},
    {"--test-quant-staircase",    "quant_staircase",    quantStaircase},
    {"--test-quant-octave",       "quant_octave",       quantOctaveExact},
    {"--test-quant-glide-zero",   "quant_glide_zero",   quantGlideZero},
    {"--test-quant-glide",        "quant_glide",        quantGlideTiming},
    {"--test-quant-glide-sr",     "quant_glide_sr",     quantGlideSampleRate},
    {"--test-quant-manual",       "quant_manual",       quantManualOverride},
    {"--test-quant-scales",       "quant_scales",       quantAllScales},
    {"--test-quant-stateless",    "quant_stateless",    quantIsStateless},
    {"--test-quant-non-finite",   "quant_non_finite",   quantNonFinite},
    {"--test-quant-extreme",      "quant_extreme",      quantExtremeInput},
    {"--test-wt-frame-size",      "wt_frame_size",      wtFrameSizeIsFixed},
    {"--test-wt-window-content",  "wt_window_content",  wtWindowChangesContent},
    {"--test-wt-amortised",       "wt_amortised",       wtWorkIsAmortised},
    {"--test-wt-tracks",          "wt_tracks",          wtTracksPlayhead},
    {"--test-wt-snapshot",        "wt_snapshot",        wtSnapshotsPosition},
    {"--test-wt-normalise",       "wt_normalise",       wtNormalises},
    {"--test-wt-loop",            "wt_loop",            wtLoopsCleanly},
    {"--test-wt-antialias",       "wt_antialias",       wtAntiAliases},
    {"--test-wt-fractional",      "wt_fractional",      wtFractionalWindow},
    {"--test-wt-dc-source",       "wt_dc_source",       wtDcSource},
    {"--test-wt-nan-stem",        "wt_nan_stem",        wtNonFiniteStem},
    {"--test-wt-boundary",        "wt_boundary",        wtBoundaryPadding},
    {"--test-wt-degenerate",      "wt_degenerate",      wtDegenerateSet},
    {"--test-wt-reset-rearm",     "wt_reset_rearm",     wtResetRearmsSilence},
    {"--test-wt-default-latency", "wt_default_latency", wtDefaultLatency},
    {"--test-wt-tiny-budget",     "wt_tiny_budget",     wtTinyBudget},
    {"--test-wt-budget-window",   "wt_budget_window",   wtBudgetFollowsWindow},
    {"--test-wt-unit-ratio",      "wt_unit_ratio",      wtUnitRatioAlignment},
    {"--test-wt-taper-dc",        "wt_taper_dc",        wtTaperDc},
    {"--test-wt-full-scale",      "wt_full_scale",      wtNeverExceedsFullScale},
    {"--test-wt-stereo",          "wt_stereo",          wtStereoDownmix},
    {"--test-wt-window-automation","wt_window_automation",wtWindowAutomation},
    {"--test-buffer-non-finite-write","buffer_non_finite_write",bufferRejectsNonFinite},
    {"--test-wt-restart",         "wt_restart",         wtRestartsOnChange},
    {"--test-wt-bad-input",       "wt_bad_input",       wtBadInput},
    {"--test-osc-alias",          "osc_alias",          oscAliasFloor},
    {"--test-osc-pitch",          "osc_pitch",          oscPitchTracking},
    {"--test-osc-samplerate",     "osc_samplerate",     oscSampleRate},
    {"--test-osc-frame-change",   "osc_frame_change",   oscFrameChangeIsClickFree},
    {"--test-osc-slow-morph",     "osc_slow_morph",     oscSlowMorphStillTracks},
    {"--test-osc-defaults",       "osc_defaults",       oscDefaultsCrossfade},
    {"--test-osc-reset",          "osc_reset",          oscResetRearms},
    {"--test-osc-range",          "osc_range",          oscChainCoversTheRange},
    {"--test-osc-cadence",        "osc_cadence",        oscAtProducerCadence},
    {"--test-osc-first-frame",    "osc_first_frame",    oscFirstFrameFadesIn},
    {"--test-osc-crossover",      "osc_crossover",      oscBlendCrossoverIsEarlyEnough},
    {"--test-osc-mip-boundary",   "osc_mip_boundary",   oscMipBoundaryIsSmooth},
    {"--test-osc-top-range",      "osc_top_range",      oscTopOfRangeIsClean},
    {"--test-osc-amortised",      "osc_amortised",      oscBuildIsAmortised},
    {"--test-osc-empty",          "osc_empty",          oscEmpty},
    {"--test-osc-bad-input",      "osc_bad_input",      oscBadInput},
    {"--test-osc-level",          "osc_level",          oscLevel},
    {"--test-lpg-step",           "lpg_step",           lpgStepResponse},
    {"--test-lpg-brightness",     "lpg_brightness",     lpgBrightnessFallsWithLevel},
    {"--test-lpg-colour",         "lpg_colour",         lpgColourContinuum},
    {"--test-lpg-decay",          "lpg_decay",          lpgDecayControl},
    {"--test-lpg-samplerate",     "lpg_samplerate",     lpgSampleRate},
    {"--test-lpg-audio-rate",     "lpg_audio_rate",     lpgAudioRateModulation},
    {"--test-lpg-resting",        "lpg_resting",        lpgRestingLevel},
    {"--test-lpg-held-gate",      "lpg_held_gate",      lpgHeldGateSustains},
    {"--test-lpg-extreme-audio",  "lpg_extreme_audio",  lpgExtremeAudio},
    {"--test-lpg-response",       "lpg_response",       lpgResponseScale},
    {"--test-lpg-bad-input",      "lpg_bad_input",      lpgBadInput},
    {"--test-grain-pool",         "grain_pool",         grainPoolIsBounded},
    {"--test-grain-level",        "grain_level",        grainLevelIsStable},
    {"--test-grain-no-clicks",    "grain_no_clicks",    grainNoClicks},
    {"--test-grain-pitch",        "grain_pitch",        grainPitch},
    {"--test-grain-spread",       "grain_spread",       grainSpread},
    {"--test-grain-jitter",       "grain_jitter",       grainJitterWraps},
    {"--test-grain-boundary",     "grain_boundary",     grainCrossesBoundary},
    {"--test-grain-coherent",     "grain_coherent",     grainCoherentOverlap},
    {"--test-grain-short",        "grain_short",        grainShortGrains},
    {"--test-grain-rate-change",  "grain_rate_change",  grainSampleRateChange},
    {"--test-grain-wrap",         "grain_wrap",         grainWrapsEveryFrame},
    {"--test-grain-bad-input",    "grain_bad_input",    grainBadInput},
    {"--test-diffusion-decay",    "diffusion_decay",    diffusionDecayTracks},
    {"--test-diffusion-runaway",  "diffusion_runaway",  diffusionNoRunaway},
    {"--test-diffusion-samplerate","diffusion_samplerate",diffusionSampleRate},
    {"--test-diffusion-mix",      "diffusion_mix",      diffusionMix},
    {"--test-diffusion-damping",  "diffusion_damping",  diffusionDamping},
    {"--test-diffusion-stereo",   "diffusion_stereo",   diffusionStereo},
    {"--test-diffusion-bad-input","diffusion_bad_input",diffusionBadInput},
    {"--test-diffusion-rate-tail","diffusion_rate_tail",diffusionKeepsTailAcrossRateChange},
    {"--test-diffusion-per-comb", "diffusion_per_comb",  diffusionPerCombDecay},
    {"--test-diffusion-allpass",  "diffusion_allpass",   diffusionAllpassIsFlat},
    {"--test-diffusion-denormal", "diffusion_denormal",  diffusionFlushesDenormals},
    {"--test-diffusion-coprime",  "diffusion_coprime",   diffusionCoprimeLengths},
    {"--test-diffusion-no-alloc", "diffusion_no_alloc", diffusionNoAlloc},
    {"--test-extreme-sweep",      "extreme_sweep",      extremeInputSweep},
};

/** The FFT checks take different arguments, so they are named separately. */
const char* const kFftCommands[] = {
    "--test-fft-roundtrip",
    "--test-fft-impulse",
    "--test-fft-sine",
    "--test-fft-sizes",
};

int main(int argc, char** argv) {
    const std::string cmd = (argc > 1) ? argv[1] : "--self-test";

    if (cmd == "--dump-hpss-medians") return dumpHpssMedians();

    if (cmd == "--list-commands") {
        for (const char* c : kFftCommands) std::cout << c << "\n";
        for (const auto& c : kCases) std::cout << c.cmd << "\n";
        return 0;
    }

    if (cmd == "--help" || cmd == "-h") {
        std::cerr << "Stems Test Executable\n\n"
                  << "  --self-test            Run every check\n"
                  << "  --test-fft-roundtrip   inverse(forward(x)) == x\n"
                  << "      --size=N           Transform size (default: 2048)\n"
                  << "  --test-fft-impulse     Impulse has flat unit magnitude\n"
                  << "      --size=N           Transform size (default: 2048)\n"
                  << "  --test-fft-sine        Bin-centred sine lands in one bin\n"
                  << "      --size=N           Transform size (default: 2048)\n"
                  << "      --bin=N            Target bin (default: 64)\n"
                  << "  --test-fft-sizes       Round-trip across 64..8192\n";
        return 0;
    }

    if (cmd == "--test-fft-roundtrip") {
        const std::size_t n = static_cast<std::size_t>(parseIntArg(argc, argv, "--size=", 2048));
        double err = 0.0;
        const bool ok = fftRoundtrip(n, err);
        emit("fft_roundtrip", ok, num("size", (double)n) + ", " + num("max_error", err));
        return ok ? 0 : 1;
    }

    if (cmd == "--test-fft-impulse") {
        const std::size_t n = static_cast<std::size_t>(parseIntArg(argc, argv, "--size=", 2048));
        double err = 0.0;
        const bool ok = fftImpulse(n, err);
        emit("fft_impulse", ok, num("size", (double)n) + ", " + num("max_error", err));
        return ok ? 0 : 1;
    }

    if (cmd == "--test-fft-sine") {
        const std::size_t n   = static_cast<std::size_t>(parseIntArg(argc, argv, "--size=", 2048));
        const std::size_t bin = static_cast<std::size_t>(parseIntArg(argc, argv, "--bin=", 64));
        double leakage = 0.0;
        const bool ok = fftSine(n, bin, leakage);
        emit("fft_sine", ok,
             num("size", (double)n) + ", " + num("bin", (double)bin) + ", " +
             num("leakage_ratio", leakage));
        return ok ? 0 : 1;
    }

    if (cmd == "--test-fft-sizes") {
        bool allOk = true;
        double worst = 0.0;
        int worstSize = 0;
        for (std::size_t n = 64; n <= 8192; n <<= 1) {
            double err = 0.0;
            if (!fftRoundtrip(n, err)) allOk = false;
            if (err > worst) { worst = err; worstSize = (int)n; }
        }
        emit("fft_sizes", allOk, num("worst_error", worst) + ", " + num("worst_size", worstSize));
        return allOk ? 0 : 1;
    }

    for (const auto& c : kCases) {
        if (cmd == c.cmd) {
            std::string detail;
            const bool ok = c.fn(detail);
            emit(c.name, ok, detail.empty() ? "" : ("\"detail\": \"" + detail + "\""));
            return ok ? 0 : 1;
        }
    }

    if (cmd == "--self-test") {
        int passed = 0, failed = 0;
        double err = 0.0, leakage = 0.0;

        auto record = [&](bool ok) { ok ? passed++ : failed++; };
        record(fftRoundtrip(2048, err));
        record(fftImpulse(2048, err));
        record(fftSine(2048, 64, leakage));
        bool sizesOk = true;
        for (std::size_t n = 64; n <= 8192; n <<= 1) {
            double e = 0.0;
            if (!fftRoundtrip(n, e)) sizesOk = false;
        }
        record(sizesOk);

        // Everything else comes straight off the dispatch table, so a check
        // that is runnable is a check --self-test runs.
        for (const auto& c : kCases) {
            std::string detail;
            record(c.fn(detail));
        }

        std::cout << "{\"test\": \"self_test\""
                  << ", \"passed\": " << passed
                  << ", \"failed\": " << failed
                  << "}" << std::endl;
        return failed == 0 ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
