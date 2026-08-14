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
 ******************************************************************************/

#include "common/stems/FftBackend.hpp"
#include "common/stems/ReferenceFft.hpp"

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

        std::cout << "{\"test\": \"self_test\""
                  << ", \"passed\": " << passed
                  << ", \"failed\": " << failed
                  << "}" << std::endl;
        return failed == 0 ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
