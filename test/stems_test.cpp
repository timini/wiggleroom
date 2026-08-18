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
#include "common/stems/Hpss.hpp"
#include "common/stems/SeparationWorker.hpp"
#include "common/stems/StemMixer.hpp"
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
