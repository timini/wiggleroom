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
 ******************************************************************************/

#include "common/stems/FftBackend.hpp"
#include "common/stems/ReferenceFft.hpp"
#include "common/stems/RingBuffer.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
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

}  // namespace

int main(int argc, char** argv) {
    const std::string cmd = (argc > 1) ? argv[1] : "--self-test";

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

        std::cout << "{\"test\": \"self_test\""
                  << ", \"passed\": " << passed
                  << ", \"failed\": " << failed
                  << "}" << std::endl;
        return failed == 0 ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
