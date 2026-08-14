#pragma once
/******************************************************************************
 * Transport - clock tracking and repitch playback for Stems
 *
 * Framework-free: no rack.hpp, so it is directly unit-testable.
 *
 * Phase locking, not free-running estimation. The playhead advances at a rate
 * derived from the measured clock period, but on every clock edge the phase is
 * snapped to the exact grid position that edge represents. Free-running from a
 * measured period accumulates error without bound; a musical loop that drifts
 * away from the clock over a few minutes is useless. Snapping means error is
 * bounded by one clock interval and never accumulates.
 *
 * S4 implements repitch (vari-speed) playback only. Time-stretch is deferred to
 * S20, so sync_mode defaults to repitch per the spec.
 *
 * Real-time contract: process() allocates nothing and takes no locks.
 ******************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace WiggleRoom {
namespace stems {

class Transport {
public:
    explicit Transport(int sampleRate) { setSampleRate(sampleRate); }

    /**
     * Change sample rate without disturbing musical state.
     *
     * The measured period is stored in seconds, so it needs no recalculation.
     * Resetting it here would drop a 30 BPM loop to the 120 BPM default and
     * play it at four times its intended rate until the next clock edge, which
     * is precisely the mid-playback sample-rate change the spec requires be
     * seamless.
     */
    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
    }

    /** Clock pulses that make up one full loop. 16 is 4 bars of 4/4. */
    void setClocksPerLoop(int clocks) {
        clocksPerLoop_ = (clocks > 0) ? clocks : 1;
    }

    /** Multiplier on the incoming clock. 2 = twice as fast, 0.5 = half. */
    void setClockDivision(float division) {
        clockDivision_ = (division > 0.f) ? division : 1.f;
    }

    /**
     * Loop window as fractions of the buffer.
     *
     * Length is clamped first and start is then constrained to keep
     * start + length <= 1. Clamping start first meant a start of 1, a legal
     * end of the parameter range, produced a zero length that the minimum then
     * pushed past the end of the buffer, so every read returned silence.
     */
    void setLoopBounds(float start, float length) {
        loopLength_ = std::min(std::max(length, kMinLoopLength), 1.f);
        loopStart_  = std::min(std::max(start, 0.f), 1.f - loopLength_);
    }

    void setBufferFrames(std::size_t frames) { bufferFrames_ = frames; }

    /**
     * Advance one sample.
     * @param clockVoltage  Clock input, Schmitt-triggered at 1V/0.1V.
     * @param resetVoltage  Reset input, same thresholds.
     */
    void process(float clockVoltage, float resetVoltage) {
        downbeat_ = false;

        // Advance BOTH triggers every sample, before any early return. If reset
        // returned early without consuming a coincident clock edge, the still
        // high clock would register as a fresh edge on the next sample and step
        // the phase straight off the downbeat. PreFlightClock in this repo
        // fires its master clock and reset together on the downbeat, so that is
        // the normal integration, not an edge case.
        const bool clockEdge = clockTrigger_.process(clockVoltage);
        const bool resetEdge = resetTrigger_.process(resetVoltage);

        timeSinceClock_ += 1.0 / sampleRate_;

        // Clock TIMING is maintained on every edge, independently of reset.
        // Doing this before the reset check matters twice over: a reset
        // coincident with an edge must not lose that edge's measurement, and a
        // reset between edges must not restart the timer. Clearing the timer on
        // an asynchronous reset made the next edge measure only the
        // reset-to-edge fragment, so a reset halfway through a 120 BPM interval
        // recorded 0.25 s and doubled playback speed until the edge after that.
        if (clockEdge) {
            // Only measure between two real edges. After an idle gap
            // timeSinceClock_ holds the whole idle duration; recording that as
            // the period would crawl playback until the next edge and then
            // jump. The first edge after idle sets the origin only.
            if (hasClockOrigin_ && timeSinceClock_ > kDebounceSeconds &&
                timeSinceClock_ <= kClockTimeoutSeconds) {
                clockPeriodSeconds_ = timeSinceClock_;
                clockDetected_ = true;
            }
            hasClockOrigin_ = true;
            timeSinceClock_ = 0.0;
        }

        // Reset wins on musical POSITION, and is immediate, so a song-position
        // reset lands exactly on the downbeat rather than at the next edge.
        if (resetEdge) {
            phase_ = 0.0;
            pulseCount_ = 0;
            loopIndex_ = 0;
            downbeat_ = true;
            return;
        }

        if (clockEdge) {
            pulseCount_++;
            // Snap to the grid the DIVISION implies, not the x1 grid. At x2 the
            // loop completes in half the clocks, so snapping to pulse/16 would
            // drag the playhead backwards on every edge.
            const double loopPosition =
                static_cast<double>(pulseCount_) * clockDivision_ / clocksPerLoop_;

            // A downbeat is a crossing of an integer loop boundary on the
            // divided grid, NOT any backward phase correction. A late edge
            // corrects the phase backwards while still sitting at, say, 3/16;
            // treating that as a wrap fired spurious downbeats throughout the
            // loop under ordinary clock jitter.
            const long long newLoopIndex =
                static_cast<long long>(std::floor(loopPosition));
            if (newLoopIndex != loopIndex_) {
                loopIndex_ = newLoopIndex;
                downbeat_ = true;
            }
            phase_ = loopPosition - std::floor(loopPosition);
            return;
        }

        if (timeSinceClock_ > kClockTimeoutSeconds) {
            clockDetected_ = false;
            // Force the next edge to be treated as an origin rather than the
            // end of an enormous interval.
            hasClockOrigin_ = false;
        }

        // Between edges, advance at the rate implied by the measured period.
        const double loopSeconds = clockPeriodSeconds_ * clocksPerLoop_ / clockDivision_;
        if (loopSeconds > 1e-9) {
            const double increment = 1.0 / (loopSeconds * sampleRate_);
            phase_ += increment;
            totalPhaseAdvanced_ += increment;
            if (phase_ >= 1.0) {
                phase_ -= std::floor(phase_);
                loopIndex_++;
                downbeat_ = true;
            }
        }
    }

    double phase() const { return phase_; }
    bool downbeat() const { return downbeat_; }
    bool clockDetected() const { return clockDetected_; }
    double clockPeriodSeconds() const { return clockPeriodSeconds_; }

    /** Cumulative phase advanced, used by tests to compare division ratios. */
    double totalPhaseAdvanced() const { return totalPhaseAdvanced_; }

    /** Playhead position in frames, windowed into the loop bounds. */
    double playheadFrames() const {
        if (bufferFrames_ == 0) return 0.0;
        const double start  = loopStart_  * static_cast<double>(bufferFrames_);
        const double length = loopLength_ * static_cast<double>(bufferFrames_);
        return start + phase_ * length;
    }

    /** Test hook: drive phase directly to check the playhead mapping. */
    void setPhaseForTest(double phase) {
        phase_ = std::min(std::max(phase, 0.0), 1.0);
    }

private:
    /** Minimal Schmitt trigger. The core cannot use rack::dsp::SchmittTrigger. */
    struct SchmittTrigger {
        bool state = false;
        bool process(float value, float low = 0.1f, float high = 1.f) {
            if (state) {
                if (value <= low) state = false;
            } else if (value >= high) {
                state = true;
                return true;
            }
            return false;
        }
    };

    static constexpr float kMinLoopLength = 0.001f;
    static constexpr double kDebounceSeconds = 0.0005;    // caps usable clock at 2 kHz
    static constexpr double kClockTimeoutSeconds = 3.0;

    int sampleRate_ = 48000;
    int clocksPerLoop_ = 16;
    float clockDivision_ = 1.f;
    float loopStart_ = 0.f;
    float loopLength_ = 1.f;
    std::size_t bufferFrames_ = 0;

    double phase_ = 0.0;
    double totalPhaseAdvanced_ = 0.0;
    double clockPeriodSeconds_ = 0.5;
    double timeSinceClock_ = 0.0;
    long long pulseCount_ = 0;
    bool downbeat_ = false;
    bool clockDetected_ = false;
    bool hasClockOrigin_ = false;
    long long loopIndex_ = 0;

    SchmittTrigger clockTrigger_;
    SchmittTrigger resetTrigger_;
};

}  // namespace stems
}  // namespace WiggleRoom
