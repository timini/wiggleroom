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
#include "common/stems/Stft.hpp"
#include "common/stems/Transport.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
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
const char* const kCommands[] = {
    "--test-fft-roundtrip",
    "--test-fft-impulse",
    "--test-fft-sine",
    "--test-fft-sizes",
    "--test-buffer-roundtrip",
    "--test-buffer-wraparound",
    "--test-buffer-no-alloc",
    "--test-buffer-interpolate",
    "--test-buffer-capacity",
    "--test-buffer-non-finite",
    "--test-buffer-clear-cheap",
    "--test-transport-lock",
    "--test-transport-reset",
    "--test-transport-division",
    "--test-transport-loop",
    "--test-transport-no-clock",
    "--test-transport-division-snap",
    "--test-transport-reset-coincident",
    "--test-transport-loop-max-start",
    "--test-transport-samplerate",
    "--test-transport-clock-restart",
    "--test-transport-downbeat-jitter",
    "--test-transport-reset-midinterval",
    "--test-stft-reconstruct",
    "--test-stft-odd-lengths",
    "--test-stft-zero-mask",
    "--test-stft-short-input",
    "--test-stft-cola",
    "--test-stft-tiny-fft",
    "--test-hpss-separates",
    "--test-hpss-sum",
    "--test-hpss-low-band",
    "--test-hpss-degenerate",
};

int main(int argc, char** argv) {
    const std::string cmd = (argc > 1) ? argv[1] : "--self-test";

    if (cmd == "--dump-hpss-medians") return dumpHpssMedians();

    if (cmd == "--list-commands") {
        for (const char* c : kCommands) std::cout << c << "\n";
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

    // RingBuffer commands
    {
        struct BufferCase { const char* cmd; const char* name; bool (*fn)(std::string&); };
        const BufferCase bufferCases[] = {
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
        };
        for (const auto& c : bufferCases) {
            if (cmd == c.cmd) {
                std::string detail;
                const bool ok = c.fn(detail);
                emit(c.name, ok, detail.empty() ? "" : ("\"detail\": \"" + detail + "\""));
                return ok ? 0 : 1;
            }
        }
    }

    if (cmd == "--self-test") {
        int passed = 0, failed = 0;
        double err = 0.0, leakage = 0.0;
        std::string detail;

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
        record(bufferRoundtrip(detail));
        record(bufferWraparound(detail));
        record(bufferNoAlloc(detail));
        record(bufferInterpolate(detail));
        record(bufferCapacity(detail));
        record(bufferNonFinite(detail));
        record(bufferClearIsCheap(detail));
        record(transportLock(detail));
        record(transportReset(detail));
        record(transportDivision(detail));
        record(transportLoop(detail));
        record(transportNoClock(detail));
        record(transportDivisionSnap(detail));
        record(transportResetCoincident(detail));
        record(transportLoopMaxStart(detail));
        record(transportSampleRateChange(detail));
        record(transportClockRestart(detail));
        record(transportDownbeatJitter(detail));
        record(transportResetMidInterval(detail));
        record(stftReconstructDefault(detail));
        record(stftReconstructOddLengths(detail));
        record(stftZeroMask(detail));
        record(stftShortInput(detail));
        record(stftCola(detail));
        record(stftTinyFft(detail));
        record(hpssSeparates(detail));
        record(hpssLayersSumToSource(detail));
        record(hpssLowBand(detail));
        record(hpssDegenerate(detail));

        std::cout << "{\"test\": \"self_test\""
                  << ", \"passed\": " << passed
                  << ", \"failed\": " << failed
                  << "}" << std::endl;
        return failed == 0 ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
