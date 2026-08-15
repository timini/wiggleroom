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
 * Reclamation uses an epoch counter rather than reference counting, so the
 * audio side pays only two relaxed atomic stores per block and never touches an
 * allocator. A retired set is destroyed once the reader has been observed to
 * enter a later critical section than the one that could still hold it.
 *
 * Audio-thread contract: acquire() and release() allocate nothing, take no
 * locks, and never free. Everything else runs on the worker or the UI thread.
 ******************************************************************************/

#include "FftBackend.hpp"
#include "Hpss.hpp"
#include "ReferenceFft.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
    std::vector<float> layer[kNumLayers];
    uint64_t generation = 0;
};

class SeparationWorker {
public:
    SeparationWorker() = default;

    ~SeparationWorker() { stop(); }

    SeparationWorker(const SeparationWorker&) = delete;
    SeparationWorker& operator=(const SeparationWorker&) = delete;

    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { run(); });
    }

    /** Join the worker and free everything. Not audio-thread safe. */
    void stop() {
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
     * Queue a separation job from an immutable copy of @p input.
     *
     * The copy is taken here, on the calling thread, precisely so the worker
     * never reads a buffer that process() may be writing.
     *
     * @return the generation ID assigned to this job. Generations start at 1,
     *         so 0 is always "nothing submitted".
     */
    uint64_t submit(const float* input, std::size_t length, int sampleRate) {
        const uint64_t gen = nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.assign(input, input + length);
            pendingRate_ = sampleRate;
            pendingGeneration_ = gen;
            hasPending_ = true;
        }
        wake_.notify_one();
        return gen;
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
        // Enter a critical section: publish an odd epoch so the worker knows a
        // reader may be holding whatever is currently live.
        readerEpoch_.fetch_add(1, std::memory_order_acq_rel);
        return published_.load(std::memory_order_acquire);
    }

    void release(const StemSet* /*set*/) {
        // Leave the critical section: back to an even epoch. The worker can now
        // reclaim anything retired before this section began.
        readerEpoch_.fetch_add(1, std::memory_order_acq_rel);
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

private:
    void run() {
        workerId_.store(std::this_thread::get_id(), std::memory_order_release);
        ReferenceFft fft(2048);
        Hpss hpss(fft);

        while (running_.load(std::memory_order_acquire)) {
            std::vector<float> input;
            int sampleRate = 48000;
            uint64_t generation = 0;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] {
                    return hasPending_ || !running_.load(std::memory_order_acquire);
                });
                if (!running_.load(std::memory_order_acquire)) break;
                input = std::move(pending_);
                pending_.clear();
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

            auto set = std::unique_ptr<StemSet>(new StemSet());
            set->generation = generation;

            Hpss::Result result;
            hpss.separate(input.data(), input.size(), sampleRate, result);
            for (int L = 0; L < StemSet::kNumLayers; L++) {
                set->layer[L] = std::move(result.layer[L]);
            }

            // Check again: separation is slow, and the take may have been
            // superseded while it ran. Publishing now would overwrite a newer
            // result with an older one.
            if (generation != currentGeneration()) {
                jobsDiscarded_.fetch_add(1, std::memory_order_relaxed);
                tryReclaim();
                continue;
            }

            publish(set.release());
            tryReclaim();
        }
    }

    void publish(StemSet* fresh) {
        StemSet* old = published_.exchange(fresh, std::memory_order_acq_rel);
        if (!old) return;
        // Record the epoch at retirement. The set becomes reclaimable once the
        // reader is observed outside any critical section that began at or
        // before this point.
        std::lock_guard<std::mutex> lock(retiredMutex_);
        retired_.push_back({old, readerEpoch_.load(std::memory_order_acquire)});
    }

    /** Free retired sets the reader can no longer be holding. Worker only. */
    void tryReclaim() {
        std::lock_guard<std::mutex> lock(retiredMutex_);
        const uint64_t now = readerEpoch_.load(std::memory_order_acquire);
        for (auto it = retired_.begin(); it != retired_.end();) {
            // Even epoch means the reader is not inside a critical section.
            // A strictly later epoch means it has entered and left at least one
            // since retirement, so it cannot still hold this pointer.
            const bool readerIdle = (now % 2 == 0);
            if (readerIdle && now >= it->epoch) {
                destroySet(it->set, /*shuttingDown=*/false);
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
        for (auto& entry : retired_) destroySet(entry.set, /*shuttingDown=*/true);
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

    struct Retired {
        StemSet* set;
        uint64_t epoch;
    };

    std::atomic<bool> running_{false};
    std::thread thread_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<float> pending_;
    int pendingRate_ = 48000;
    uint64_t pendingGeneration_ = 0;
    bool hasPending_ = false;

    std::atomic<uint64_t> nextGeneration_{0};
    std::atomic<StemSet*> published_{nullptr};
    std::atomic<uint64_t> readerEpoch_{0};

    mutable std::mutex retiredMutex_;
    std::vector<Retired> retired_;
    std::atomic<std::size_t> freedOffWorker_{0};
    std::atomic<std::thread::id> workerId_{};
    std::atomic<uint64_t> jobsStarted_{0};
    std::atomic<uint64_t> jobsDiscarded_{0};
};

}  // namespace stems
}  // namespace WiggleRoom
