#pragma once
/******************************************************************************
 * ReferenceFft - self-contained radix-2 FFT implementing FftBackend
 *
 * Exists so stems_test can exercise every FFT-dependent algorithm without
 * linking the Rack SDK, and so the STFT has a reference to validate the VCV
 * adapter against.
 *
 * Iterative Cooley-Tukey with bit-reversal permutation. Not the fastest
 * possible, but it is short enough to be obviously correct, which is what a
 * reference implementation is for. Fast enough for tests: a 2048-point
 * transform is well under a millisecond.
 ******************************************************************************/

#include "FftBackend.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace WiggleRoom {
namespace stems {

class ReferenceFft : public FftBackend {
public:
    explicit ReferenceFft(std::size_t n) : n_(n), scratch_(n) {
        if (n < 2 || (n & (n - 1)) != 0) {
            throw std::invalid_argument("ReferenceFft size must be a power of two >= 2");
        }
        buildTwiddles();
    }

    std::size_t size() const override { return n_; }

    void forward(const float* input, float* spectrum) override {
        for (std::size_t i = 0; i < n_; i++) {
            scratch_[i] = std::complex<double>(static_cast<double>(input[i]), 0.0);
        }
        transform(scratch_, /*inverse=*/false);

        // Only bins 0..n/2 are independent for a real input; the rest are the
        // conjugate mirror.
        for (std::size_t bin = 0; bin <= n_ / 2; bin++) {
            spectrum[2 * bin]     = static_cast<float>(scratch_[bin].real());
            spectrum[2 * bin + 1] = static_cast<float>(scratch_[bin].imag());
        }
    }

    void inverse(const float* spectrum, float* output) override {
        // Rebuild the full conjugate-symmetric spectrum before transforming.
        for (std::size_t bin = 0; bin <= n_ / 2; bin++) {
            scratch_[bin] = std::complex<double>(spectrum[2 * bin], spectrum[2 * bin + 1]);
        }
        for (std::size_t bin = n_ / 2 + 1; bin < n_; bin++) {
            scratch_[bin] = std::conj(scratch_[n_ - bin]);
        }
        transform(scratch_, /*inverse=*/true);

        const double norm = 1.0 / static_cast<double>(n_);
        for (std::size_t i = 0; i < n_; i++) {
            output[i] = static_cast<float>(scratch_[i].real() * norm);
        }
    }

private:
    void buildTwiddles() {
        twiddles_.resize(n_ / 2);
        for (std::size_t k = 0; k < n_ / 2; k++) {
            const double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n_);
            twiddles_[k] = std::complex<double>(std::cos(angle), std::sin(angle));
        }
    }

    void transform(std::vector<std::complex<double>>& data, bool inverse) const {
        // Bit-reversal permutation
        for (std::size_t i = 1, j = 0; i < n_; i++) {
            std::size_t bit = n_ >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[i], data[j]);
        }

        for (std::size_t len = 2; len <= n_; len <<= 1) {
            const std::size_t step = n_ / len;
            for (std::size_t i = 0; i < n_; i += len) {
                for (std::size_t k = 0; k < len / 2; k++) {
                    std::complex<double> w = twiddles_[k * step];
                    if (inverse) w = std::conj(w);
                    const std::complex<double> u = data[i + k];
                    const std::complex<double> v = data[i + k + len / 2] * w;
                    data[i + k]             = u + v;
                    data[i + k + len / 2]   = u - v;
                }
            }
        }
    }

    static constexpr double kPi = 3.14159265358979323846;

    std::size_t n_;
    mutable std::vector<std::complex<double>> scratch_;
    std::vector<std::complex<double>> twiddles_;
};

}  // namespace stems
}  // namespace WiggleRoom
