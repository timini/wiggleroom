#pragma once
/******************************************************************************
 * Stft - short-time Fourier transform front end for Stems
 *
 * Framework-free: takes an FftBackend&, so the core never includes rack.hpp and
 * stems_test can drive it with ReferenceFft.
 *
 * Parameters match librosa's defaults (n_fft 2048, hop = n_fft/4) so HPSS
 * results can be validated against a reference implementation rather than only
 * against themselves.
 *
 * Windowing: Hann applied on both analysis and synthesis. With hop = n/4 the
 * squared Hann window satisfies the constant-overlap-add condition, so
 * unmodified analysis followed by synthesis reconstructs the input up to a
 * fixed gain, which is divided out. Applying the window on synthesis as well as
 * analysis is what keeps a modified spectrum from producing edge discontinuities
 * at frame boundaries, which is the whole point given HPSS will be modifying it.
 *
 * Not real-time: this runs on the worker thread over a whole recorded buffer,
 * so it allocates its scratch up front but is not required to be RT-safe.
 ******************************************************************************/

#include "FftBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace WiggleRoom {
namespace stems {

class Stft {
public:
    /**
     * @param fft  Backend supplying the transform. Must outlive this object.
     * @param hop  Samples between frame starts. Defaults to frameSize/4.
     */
    Stft(FftBackend& fft, std::size_t hop = 0)
        : fft_(fft),
          frameSize_(fft.size()),
          // Never allow a zero hop. FftBackend permits sizes down to 2, where
          // size/4 truncates to 0 and process() would loop forever without
          // advancing, hanging the worker thread.
          hopSize_(std::max<std::size_t>(1, hop ? hop : fft.size() / 4)) {
        buildWindow();
        frame_.resize(frameSize_);
        spectrum_.resize(fft_.spectrumLength());
        padded_.clear();
        accum_.clear();
        windowSum_.clear();
    }

    std::size_t frameSize() const { return frameSize_; }
    std::size_t hopSize() const { return hopSize_; }
    std::size_t numBins() const { return fft_.numBins(); }

    /** Analysis window value at index i, exposed so tests can check COLA. */
    double windowGain(std::size_t i) const {
        // The COLA property that matters is of the SQUARED window, since it is
        // applied on both analysis and synthesis.
        return window_[i] * window_[i];
    }

    /**
     * Run analysis, an in-place spectrum callback, and synthesis.
     *
     * @param modify  Called once per frame with the interleaved re/im spectrum
     *                and its length in floats. Passing an empty callback gives
     *                pure reconstruction.
     */
    template <typename ModifyFn>
    void process(const float* input, float* output, std::size_t length, ModifyFn modify) {
        if (!input || !output || length == 0) return;

        // Zero-pad by a whole frame at each end and run the analysis over the
        // padded signal.
        //
        // Without padding, two regions are silently lost: the tail after the
        // last frame that fits (a 5000 sample input at hop 512 goes silent from
        // 4608), and the first sample, where the Hann window is zero so no
        // frame contributes. Both would show up as missing audio and
        // discontinuities at loop boundaries, which is the worst possible place
        // for them. Excluding a frame at each end in the tests merely hid this.
        const std::size_t pad = frameSize_;
        const std::size_t paddedLength = length + 2 * pad;

        padded_.assign(paddedLength, 0.f);
        std::copy(input, input + length, padded_.begin() + pad);

        accum_.assign(paddedLength, 0.f);
        windowSum_.assign(paddedLength, 0.f);

        for (std::size_t start = 0; start + frameSize_ <= paddedLength; start += hopSize_) {
            for (std::size_t i = 0; i < frameSize_; i++) {
                frame_[i] = padded_[start + i] * static_cast<float>(window_[i]);
            }

            fft_.forward(frame_.data(), spectrum_.data());
            modify(spectrum_.data(), spectrum_.size());
            fft_.inverse(spectrum_.data(), frame_.data());

            for (std::size_t i = 0; i < frameSize_; i++) {
                accum_[start + i]     += frame_[i] * static_cast<float>(window_[i]);
                windowSum_[start + i] += static_cast<float>(window_[i] * window_[i]);
            }
        }

        // Normalise by the accumulated squared window. Per sample rather than
        // by a constant gain, so partial-overlap regions are corrected too.
        for (std::size_t i = 0; i < length; i++) {
            const float w = windowSum_[pad + i];
            output[i] = (w > 1e-8f) ? (accum_[pad + i] / w) : 0.f;
        }
    }

private:
    void buildWindow() {
        window_.resize(frameSize_);
        // Periodic Hann, which is the correct form for STFT overlap-add.
        // The symmetric variant does not satisfy COLA at hop = n/4.
        for (std::size_t i = 0; i < frameSize_; i++) {
            window_[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                               static_cast<double>(frameSize_)));
        }
    }

    static constexpr double kPi = 3.14159265358979323846;

    FftBackend& fft_;
    std::size_t frameSize_;
    std::size_t hopSize_;
    std::vector<double> window_;
    std::vector<float> frame_;
    std::vector<float> spectrum_;
    std::vector<float> padded_;
    std::vector<float> accum_;
    std::vector<float> windowSum_;
};

}  // namespace stems
}  // namespace WiggleRoom
