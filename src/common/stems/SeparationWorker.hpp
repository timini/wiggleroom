#pragma once
/******************************************************************************
 * SeparationWorker - background separation and safe publication for Stems
 *
 * The first thread in this repository, so the ownership rules are spelled out
 * rather than assumed.
 *
 * Three requirements from specs/Stems.md, all of which came out of review:
 *
 *  1. Immutable per-job input snapshots with generation IDs. The worker never
 *     reads the live ring buffer, because process() may be mutating it if the
 *     user starts a new take mid-job. A result whose generation is no longer
 *     current is discarded rather than published, so a superseded take can
 *     never overwrite a newer one.
 *
 *  2. Publication by a single atomic pointer swap. The audio thread pins the
 *     pointer once per process() call, so it sees either the old set or the new
 *     one and never a partial one.
 *
 *  3. A retirement queue, not shared ownership. An atomic swap alone says
 *     nothing about when the audio thread has finished with the previous set.
 *     Freeing on the worker immediately risks use-after-free; shared_ptr risks
 *     the final release landing on the audio thread and deallocating tens of
 *     megabytes there; keeping every old set leaks. Retired pointers go back to
 *     the worker and are destroyed there.
 *
 * Reclamation uses HAZARD POINTERS, not an epoch counter. The first version of
 * this file used two independent atomics, a reader epoch and the published
 * pointer, and that is unsound: on a weakly ordered machine the reader can bump
 * the epoch and still load the OLD pointer while the worker exchanges the
 * pointer and still observes the preceding EVEN epoch, so the worker frees a
 * set the reader is about to dereference. That is a plain store-buffering
 * outcome, entirely legal, and ThreadSanitizer does not report it because no
 * execution it observes actually races. The fix is for the reader to publish the
 * exact pointer it took, and to re-read after publishing:
 *
 *     do { p = published_; hazard_ = p; } while (p != published_);
 *
 * with both the hazard store and the re-read sequentially consistent. That gives
 * a single total order over the two operations, so either the worker sees the
 * hazard and keeps the set, or the reader sees the new pointer and retries. The
 * loop spins only when a publication lands in the same instant, which happens at
 * most once per separation.
 *
 * Audio-thread contract: acquire() and release() allocate nothing, take no
 * locks, and never free. Everything else runs on the worker or the UI thread.
 ******************************************************************************/

#include "FftBackend.hpp"
#include "Hpss.hpp"
#include "ReferenceFft.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace WiggleRoom {
namespace stems {

/** An immutable, fully-formed separation result. */
struct StemSet {
    static constexpr int kNumLayers = Hpss::kNumLayers;
    static constexpr int kMaxChannels = 2;

    /**
     * One layer, one vector per channel.
     *
     * Stereo is carried explicitly rather than by interleaving. Handing HPSS an
     * interleaved buffer would make it read alternating left and right samples
     * as a single signal, which corrupts every median window and every phase;
     * handing it one side only would silently discard the other. The module has
     * stereo inputs and stereo loop outputs, so the published set has to be
     * stereo all the way through.
     */
    struct Layer {
        std::vector<float> channel[kMaxChannels];
    };

    Layer layer[kNumLayers];
    int channels = 1;
    uint64_t generation = 0;

    /** Channel @p ch of layer @p L, falling back to channel 0 when mono. */
    const std::vector<float>& samples(int L, int ch) const {
        return layer[L].channel[(channels > 1 && ch > 0) ? 1 : 0];
    }

    std::size_t length() const { return layer[0].channel[0].size(); }
};

class SeparationWorker {
public:
    /** Builds an FFT backend of the requested size. Called on the worker. */
    using FftFactory = std::function<std::unique_ptr<FftBackend>(std::size_t)>;

    SeparationWorker() = default;

    ~SeparationWorker() { stop(); }

    SeparationWorker(const SeparationWorker&) = delete;
    SeparationWorker& operator=(const SeparationWorker&) = delete;

    /**
     * Supply the FFT implementation the worker should use. Must be called
     * before start().
     *
     * Without this the worker falls back to ReferenceFft, which exists as the
     * self-contained test reference: it is double precision, radix-2 only and
     * makes no attempt at speed. Running a 32 second recording through it frame
     * by frame is far slower than necessary, and lengthens the shutdown wait
     * accordingly. Host adapters inject a real backend here (pffft under VCV,
     * juce::dsp::FFT under JUCE), which is the entire reason FftBackend is an
     * interface rather than a concrete type.
     */
    void setFftFactory(FftFactory factory) { fftFactory_ = std::move(factory); }

    void start() {
        if (running_.exchange(true)) return;
        abort_.store(false, std::memory_order_release);
        thread_ = std::thread([this] { run(); });
    }

    /**
     * Join the worker and free everything. Not audio-thread safe.
     *
     * The abort flag is what makes this prompt. running_ alone cannot interrupt
     * a separate() already in progress, so a stop() landing just after a job
     * started would block for the whole separation. At the 32 second buffer
     * limit that is a multi-second freeze on module removal or host quit.
     */
    void stop() {
        abort_.store(true, std::memory_order_release);
        if (!running_.exchange(false)) {
            reclaimAll();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wake_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
        reclaimAll();
    }

    /**
     * Queue a separation job from an immutable copy of the input.
     *
     * The copy is taken here, on the calling thread, precisely so the worker
     * never reads a buffer that process() may be writing.
     *
     * @param left   Left/mono channel. Required.
     * @param right  Right channel, or nullptr for mono.
     * @return the generation ID assigned to this job. Generations start at 1,
     *         so 0 is always "nothing submitted".
     */
    uint64_t submit(const float* left, const float* right, std::size_t length,
                    int sampleRate) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Generation is advanced under the same lock the worker holds while it
        // makes its final check and publishes. Bumping it outside meant a
        // submit() could land between the worker's last validation and its
        // pointer swap, so the worker published a result it had just been told
        // was superseded, and that stale set then stayed visible for the whole
        // of the newer job.
        const uint64_t gen = nextGeneration_.load(std::memory_order_relaxed) + 1;
        nextGeneration_.store(gen, std::memory_order_relaxed);

        pendingLeft_.assign(left, left + length);
        if (right) {
            pendingRight_.assign(right, right + length);
            pendingChannels_ = 2;
        } else {
            pendingRight_.clear();
            pendingChannels_ = 1;
        }
        pendingRate_ = sampleRate;
        pendingGeneration_ = gen;
        hasPending_ = true;
        wake_.notify_one();
        return gen;
    }

    /** Mono convenience overload. */
    uint64_t submit(const float* input, std::size_t length, int sampleRate) {
        return submit(input, nullptr, length, sampleRate);
    }

    /**
     * Retire whatever is published, without waiting for a replacement.
     *
     * A new recording supersedes the old take the moment it starts. Leaving the
     * previous set published means the module keeps playing the last take right
     * through the new one, and keeps it for good if the new separation fails.
     *
     * Safe from the audio thread: it advances the generation and swaps the
     * pointer under the same lock publication uses, and the retired set is
     * reclaimed by the worker exactly as any other.
     */
    void invalidate() {
        std::lock_guard<std::mutex> lock(mutex_);
        nextGeneration_.store(nextGeneration_.load(std::memory_order_relaxed) + 1,
                              std::memory_order_relaxed);
        hasPending_ = false;
        StemSet* old = published_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            std::lock_guard<std::mutex> retired(retiredMutex_);
            retired_.push_back(old);
        }
        // The worker may be in an indefinite wait, having evaluated its
        // retirement queue as empty before this call added to it. A bare notify
        // is not enough: its predicate would still be false and it would go
        // straight back to sleep, so the request is explicit.
        reclaimRequested_ = true;
        wake_.notify_one();
    }

    /** Generation of the most recently submitted job. */
    uint64_t currentGeneration() const {
        return nextGeneration_.load(std::memory_order_relaxed);
    }

    /**
     * Pin the published set for the duration of a process() call.
     * Returns nullptr when nothing has been published yet.
     *
     * Must be paired with release(). Allocation-free and lock-free.
     */
    const StemSet* acquire() {
        // Hazard pointer: publish the exact pointer taken, then confirm it is
        // still the live one. See the header comment for why an epoch counter
        // cannot do this job.
        const StemSet* candidate;
        for (;;) {
            candidate = published_.load(std::memory_order_acquire);
            hazard_.store(candidate, std::memory_order_seq_cst);
            if (candidate == published_.load(std::memory_order_seq_cst)) break;
        }
        return candidate;
    }

    void release(const StemSet* /*set*/) {
        // Clearing the hazard is the whole of the audio-side release. A set
        // retired while this section was open is then reclaimed by the worker's
        // poll, which is why the worker never sleeps indefinitely while
        // anything is retired: with no further recording, the old multi-megabyte
        // buffers would otherwise stay allocated until shutdown, and successive
        // publications that each coincide with an active audio block would stack
        // several retired sets up.
        //
        // No condition_variable notify here. notify_one takes the internal lock
        // and is not something to do from the audio thread.
        hazard_.store(nullptr, std::memory_order_seq_cst);
    }

    /** Sets currently allocated, live plus awaiting reclamation. Diagnostics. */
    std::size_t liveSetCount() const {
        std::lock_guard<std::mutex> lock(retiredMutex_);
        return retired_.size() + (published_.load(std::memory_order_relaxed) ? 1 : 0);
    }

    /**
     * Sets freed on a thread other than the worker. Must always be zero: a
     * non-zero value means a multi-megabyte deallocation happened on the audio
     * thread, which is the failure this design exists to prevent.
     */
    std::size_t debugFreedOnAcquireThread() const {
        return freedOffWorker_.load(std::memory_order_relaxed);
    }

    /** Jobs the worker actually began separating. */
    uint64_t debugJobsStarted() const { return jobsStarted_.load(std::memory_order_relaxed); }

    /** Jobs discarded after separation because they had been superseded. */
    uint64_t debugJobsDiscarded() const { return jobsDiscarded_.load(std::memory_order_relaxed); }

    /** Jobs abandoned because separation threw. Non-fatal by design. */
    uint64_t debugJobsFailed() const { return jobsFailed_.load(std::memory_order_relaxed); }

    /** Sets still waiting for the reader to let go. Diagnostics. */
    std::size_t debugRetiredCount() const {
        std::lock_guard<std::mutex> lock(retiredMutex_);
        return retired_.size();
    }

private:
    void run() {
        workerId_.store(std::this_thread::get_id(), std::memory_order_release);

        std::unique_ptr<FftBackend> fft =
            fftFactory_ ? fftFactory_(kFftSize)
                        : std::unique_ptr<FftBackend>(new ReferenceFft(kFftSize));
        Hpss hpss(*fft);
        hpss.setAbortFlag(&abort_);

        while (running_.load(std::memory_order_acquire)) {
            std::vector<float> left, right;
            int channels = 1;
            int sampleRate = 48000;
            uint64_t generation = 0;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto ready = [this] {
                    return hasPending_ || reclaimRequested_ ||
                           !running_.load(std::memory_order_acquire);
                };
                if (hasRetired()) {
                    // Something is waiting to be freed, so do not sleep
                    // indefinitely: the reader may release its pin without any
                    // further job arriving to wake us.
                    wake_.wait_for(lock, std::chrono::milliseconds(kReclaimPollMs), ready);
                } else {
                    wake_.wait(lock, ready);
                }
                if (!running_.load(std::memory_order_acquire)) break;
                if (!hasPending_) {
                    // Cleared before releasing the lock. Leaving it set would
                    // make the predicate true forever and spin the worker at
                    // full tilt whenever a reader is still holding the set;
                    // once it is cleared, the retirement queue keeps the timed
                    // wait alive instead, which polls rather than spins.
                    reclaimRequested_ = false;
                    lock.unlock();
                    tryReclaim();
                    continue;
                }
                left = std::move(pendingLeft_);
                right = std::move(pendingRight_);
                pendingLeft_.clear();
                pendingRight_.clear();
                channels = pendingChannels_;
                sampleRate = pendingRate_;
                generation = pendingGeneration_;
                hasPending_ = false;
            }

            // Drop the job if it was superseded while we were waiting.
            if (generation != currentGeneration()) {
                jobsDiscarded_.fetch_add(1, std::memory_order_relaxed);
                tryReclaim();
                continue;
            }
            jobsStarted_.fetch_add(1, std::memory_order_relaxed);

            std::unique_ptr<StemSet> set;
            bool aborted = false;

            // Exception boundary. An exception escaping a thread entry point
            // calls std::terminate and takes the whole host down with it, and
            // the largest allocations in the module happen right here: HPSS
            // scratch and four output layers over a recording up to 32 seconds
            // long, doubled for stereo. The spec requires separation failure to
            // be non-fatal and to leave the unseparated fallback in place, so
            // the failure is recorded and the generation abandoned.
            try {
                set = separate(hpss, left, right, channels, sampleRate, generation, aborted);
            } catch (...) {
                jobsFailed_.fetch_add(1, std::memory_order_relaxed);
                set.reset();
            }

            if (!set || aborted) {
                tryReclaim();
                continue;
            }

            {
                // Final generation check and the pointer swap happen under the
                // same lock submit() uses, so no submission can slip between
                // them.
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != nextGeneration_.load(std::memory_order_relaxed)) {
                    jobsDiscarded_.fetch_add(1, std::memory_order_relaxed);
                    set.reset();
                } else {
                    publishLocked(set.release());
                }
            }
            tryReclaim();
        }
    }

    /** Run HPSS over each channel independently and assemble the set. */
    std::unique_ptr<StemSet> separate(Hpss& hpss, const std::vector<float>& left,
                                      const std::vector<float>& right, int channels,
                                      int sampleRate, uint64_t generation, bool& aborted) {
        std::unique_ptr<StemSet> set(new StemSet());
        set->generation = generation;
        set->channels = (channels > 1) ? 2 : 1;

        Hpss::Result result;
        hpss.separate(left.data(), left.size(), sampleRate, result);
        if (hpss.wasAborted()) { aborted = true; return set; }
        for (int L = 0; L < StemSet::kNumLayers; L++) {
            set->layer[L].channel[0] = std::move(result.layer[L]);
        }

        if (set->channels == 2) {
            // Separate the right channel on its own. Shared-mask stereo HPSS is
            // a refinement, not a requirement, and doing it per channel is the
            // variant that cannot silently mix the two sides together.
            Hpss::Result rightResult;
            hpss.separate(right.data(), right.size(), sampleRate, rightResult);
            if (hpss.wasAborted()) { aborted = true; return set; }
            for (int L = 0; L < StemSet::kNumLayers; L++) {
                set->layer[L].channel[1] = std::move(rightResult.layer[L]);
            }
        }
        return set;
    }

    /** Swap in a fresh set and retire the old one. Caller holds mutex_. */
    void publishLocked(StemSet* fresh) {
        StemSet* old = published_.exchange(fresh, std::memory_order_acq_rel);
        if (!old) return;
        std::lock_guard<std::mutex> lock(retiredMutex_);
        retired_.push_back(old);
    }

    bool hasRetired() const {
        std::lock_guard<std::mutex> lock(retiredMutex_);
        return !retired_.empty();
    }

    /** Free retired sets the reader is not pinning. Worker only. */
    void tryReclaim() {
        std::lock_guard<std::mutex> lock(retiredMutex_);
        const StemSet* pinned = hazard_.load(std::memory_order_seq_cst);
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (*it != pinned) {
                destroySet(*it, /*shuttingDown=*/false);
                it = retired_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /** Free everything unconditionally. Only safe once readers have stopped. */
    void reclaimAll() {
        StemSet* live = published_.exchange(nullptr, std::memory_order_acq_rel);
        // Shutdown frees run on the caller's thread by design, after the worker
        // has been joined and no reader remains, so they are not counted.
        destroySet(live, /*shuttingDown=*/true);
        std::lock_guard<std::mutex> lock(retiredMutex_);
        for (auto* set : retired_) destroySet(set, /*shuttingDown=*/true);
        retired_.clear();
    }

    /**
     * Single funnel for every deallocation, so "was this freed off the worker"
     * is actually measurable rather than assumed. Without this the diagnostic
     * counter would sit at zero forever and the test asserting it would be
     * vacuous.
     */
    void destroySet(StemSet* set, bool shuttingDown) {
        if (!set) return;
        if (!shuttingDown &&
            std::this_thread::get_id() != workerId_.load(std::memory_order_acquire)) {
            freedOffWorker_.fetch_add(1, std::memory_order_relaxed);
        }
        delete set;
    }

    static constexpr std::size_t kFftSize = 2048;
    static constexpr int kReclaimPollMs = 20;

    std::atomic<bool> running_{false};
    std::atomic<bool> abort_{false};
    std::thread thread_;
    FftFactory fftFactory_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<float> pendingLeft_;
    std::vector<float> pendingRight_;
    int pendingChannels_ = 1;
    int pendingRate_ = 48000;
    uint64_t pendingGeneration_ = 0;
    bool hasPending_ = false;
    bool reclaimRequested_ = false;

    std::atomic<uint64_t> nextGeneration_{0};
    std::atomic<StemSet*> published_{nullptr};
    std::atomic<const StemSet*> hazard_{nullptr};

    mutable std::mutex retiredMutex_;
    std::vector<StemSet*> retired_;
    std::atomic<std::size_t> freedOffWorker_{0};
    std::atomic<std::thread::id> workerId_{};
    std::atomic<uint64_t> jobsStarted_{0};
    std::atomic<uint64_t> jobsDiscarded_{0};
    std::atomic<uint64_t> jobsFailed_{0};
};

}  // namespace stems
}  // namespace WiggleRoom
