#pragma once
/******************************************************************************
 * FftBackend - FFT abstraction for the Stems core
 *
 * The Stems core must not depend on any framework, so a JUCE or standalone
 * adapter remains possible and so the algorithms stay unit-testable without
 * linking the Rack SDK. Using rack::dsp::RealFFT directly here would defeat
 * both goals.
 *
 * Each host supplies an implementation:
 *   VCV adapter   -> wraps rack::dsp::RealFFT
 *   stems_test    -> ReferenceFft (self-contained radix-2)
 *   JUCE adapter  -> wraps juce::dsp::FFT
 *
 * Cost is one virtual call per frame, negligible against a 2048-point
 * transform.
 *
 * Buffer format (deliberately explicit, unlike the packed layouts some FFT
 * libraries use, so adapters convert rather than the core guessing):
 *
 *   forward()  in : size() real samples
 *              out: 2 * (size()/2 + 1) floats, interleaved re,im for bins
 *                   0 .. size()/2 inclusive
 *   inverse()  in : same interleaved spectrum
 *              out: size() real samples, already scaled so that
 *                   inverse(forward(x)) == x
 ******************************************************************************/

#include <cstddef>

namespace WiggleRoom {
namespace stems {

struct FftBackend {
    virtual ~FftBackend() = default;

    /** Transform length in samples. Always a power of two. */
    virtual std::size_t size() const = 0;

    /** Number of complex bins produced by forward(): size()/2 + 1. */
    std::size_t numBins() const { return size() / 2 + 1; }

    /** Number of floats in the spectrum buffer: 2 * numBins(). */
    std::size_t spectrumLength() const { return 2 * numBins(); }

    virtual void forward(const float* input, float* spectrum) = 0;
    virtual void inverse(const float* spectrum, float* output) = 0;
};

}  // namespace stems
}  // namespace WiggleRoom
