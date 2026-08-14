#pragma once
/******************************************************************************
 * RingBuffer - the Stems audio capture buffer
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Why not rack::dsp::RingBuffer: the SDK ring buffers are
 * `template <typename T, size_t S>` with capacity fixed at compile time, so
 * they cannot back a buffer whose length depends on sample rate. This one
 * allocates once at construction, from the sample rate and the duration cap,
 * and never reallocates.
 *
 * Real-time contract:
 *   - write() and every read allocate nothing and take no locks
 *   - capacity is fixed for the lifetime of the object
 *   - resize() exists for sample-rate changes and is NOT audio-thread safe;
 *     the caller must guarantee no concurrent access
 *
 * The 32 second cap from specs/Stems.md is enforced by the caller passing
 * maxSeconds; at 96 kHz stereo that is roughly 24.6 MB.
 ******************************************************************************/

#include <algorithm>
#include <cstddef>
#include <vector>

namespace WiggleRoom {
namespace stems {

class RingBuffer {
public:
    /**
     * @param sampleRate  Frames per second.
     * @param maxSeconds  Duration cap. Capacity is sampleRate * maxSeconds.
     * @param channels    1 (mono) or 2 (stereo). Stored interleaved.
     */
    RingBuffer(int sampleRate, float maxSeconds, int channels)
        : channels_(channels < 2 ? 1 : 2) {
        allocate(sampleRate, maxSeconds);
    }

    std::size_t capacityFrames() const { return capacityFrames_; }
    int channels() const { return channels_; }

    /** Total frames ever written, ignoring overwrites. */
    std::size_t framesWritten() const { return framesWritten_; }

    /** Frames currently retrievable, saturating at capacity. */
    std::size_t framesStored() const {
        return std::min(framesWritten_, capacityFrames_);
    }

    bool empty() const { return framesWritten_ == 0; }

    /** Append one frame. Overwrites the oldest frame once full. */
    void write(float left, float right) {
        const std::size_t base = writeIndex_ * static_cast<std::size_t>(channels_);
        data_[base] = left;
        if (channels_ == 2) data_[base + 1] = right;

        writeIndex_ = (writeIndex_ + 1) % capacityFrames_;
        framesWritten_++;
    }

    /**
     * Read a stored frame. Index 0 is the OLDEST surviving frame, so callers
     * see a stable timeline regardless of how far the write head has wrapped.
     * Out-of-range indices yield silence rather than reading garbage, which
     * keeps the empty-buffer state safe.
     */
    void readFrame(std::size_t index, float& left, float& right) const {
        const std::size_t stored = framesStored();
        if (stored == 0 || index >= stored) {
            left = right = 0.f;
            return;
        }
        const std::size_t physical = (oldestIndex() + index) % capacityFrames_;
        const std::size_t base = physical * static_cast<std::size_t>(channels_);
        left  = data_[base];
        right = (channels_ == 2) ? data_[base + 1] : data_[base];
    }

    /**
     * Linearly interpolated read, for vari-speed (repitch) playback.
     * Positions outside the stored range yield silence.
     */
    void readFrameInterpolated(double position, float& left, float& right) const {
        const std::size_t stored = framesStored();
        if (stored == 0 || position < 0.0) {
            left = right = 0.f;
            return;
        }
        const std::size_t i0 = static_cast<std::size_t>(position);
        if (i0 >= stored) {
            left = right = 0.f;
            return;
        }
        const float frac = static_cast<float>(position - static_cast<double>(i0));

        float l0 = 0.f, r0 = 0.f, l1 = 0.f, r1 = 0.f;
        readFrame(i0, l0, r0);
        // Hold the final frame rather than wrapping to the start, so a read at
        // the very end does not splice in unrelated audio.
        readFrame(std::min(i0 + 1, stored - 1), l1, r1);

        left  = l0 + (l1 - l0) * frac;
        right = r0 + (r1 - r0) * frac;
    }

    /** Discard contents. Keeps the allocation. */
    void clear() {
        std::fill(data_.begin(), data_.end(), 0.f);
        writeIndex_ = 0;
        framesWritten_ = 0;
    }

    /**
     * Reallocate for a new sample rate. NOT audio-thread safe: the caller must
     * ensure nothing is reading or writing. Contents are discarded, since
     * resampling them is the caller's decision.
     */
    void resize(int sampleRate, float maxSeconds) {
        allocate(sampleRate, maxSeconds);
    }

    /** Storage identity, used by tests to prove no reallocation occurred. */
    const float* rawData() const { return data_.data(); }

private:
    void allocate(int sampleRate, float maxSeconds) {
        if (sampleRate < 1) sampleRate = 1;
        if (maxSeconds <= 0.f) maxSeconds = 1.f;
        capacityFrames_ = static_cast<std::size_t>(
            static_cast<double>(sampleRate) * static_cast<double>(maxSeconds));
        if (capacityFrames_ < 1) capacityFrames_ = 1;

        data_.assign(capacityFrames_ * static_cast<std::size_t>(channels_), 0.f);
        writeIndex_ = 0;
        framesWritten_ = 0;
    }

    /** Physical index of the oldest surviving frame. */
    std::size_t oldestIndex() const {
        if (framesWritten_ < capacityFrames_) return 0;
        return writeIndex_;  // write head sits on the oldest frame once wrapped
    }

    int channels_;
    std::size_t capacityFrames_ = 0;
    std::size_t writeIndex_ = 0;
    std::size_t framesWritten_ = 0;
    std::vector<float> data_;
};

}  // namespace stems
}  // namespace WiggleRoom
