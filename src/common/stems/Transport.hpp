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

    void setSampleRate(int sampleRate) {
        sampleRate_ = (sampleRate > 0) ? sampleRate : 48000;
        // 120 BPM default so an unclocked module still behaves sensibly.
        clockPeriodSeconds_ = 0.5;
    }

    /** Clock pulses that make up one full loop. 16 is 4 bars of 4/4. */
    void setClocksPerLoop(int clocks) {
        clocksPerLoop_ = (clocks > 0) ? clocks : 1;
    }

    /** Multiplier on the incoming clock. 2 = twice as fast, 0.5 = half. */
    void setClockDivision(float division) {
        clockDivision_ = (division > 0.f) ? division : 1.f;
    }

    /** Loop window as fractions of the buffer. */
    void setLoopBounds(float start, float length) {
        loopStart_  = std::min(std::max(start, 0.f), 1.f);
        loopLength_ = std::min(std::max(length, 0.001f), 1.f - loopStart_);
        if (loopLength_ <= 0.f) loopLength_ = 0.001f;
    }

    void setBufferFrames(std::size_t frames) { bufferFrames_ = frames; }

    /**
     * Advance one sample.
     * @param clockVoltage  Clock input, Schmitt-triggered at 1V/0.1V.
     * @param resetVoltage  Reset input, same thresholds.
     */
    void process(float clockVoltage, float resetVoltage) {
        downbeat_ = false;

        // Reset takes priority and is immediate, so a song-position reset lands
        // exactly on the downbeat rather than at the next clock edge.
        if (resetTrigger_.process(resetVoltage)) {
            phase_ = 0.0;
            pulseCount_ = 0;
            downbeat_ = true;
            return;
        }

        timeSinceClock_ += 1.0 / sampleRate_;

        if (clockTrigger_.process(clockVoltage)) {
            if (timeSinceClock_ > 0.0005) {   // debounce, caps at 2 kHz
                clockPeriodSeconds_ = timeSinceClock_;
                clockDetected_ = true;
            }
            timeSinceClock_ = 0.0;

            pulseCount_ = (pulseCount_ + 1) % clocksPerLoop_;
            // Snap to the grid. This is what bounds drift.
            phase_ = static_cast<double>(pulseCount_) / clocksPerLoop_;
            if (pulseCount_ == 0) downbeat_ = true;
            return;
        }

        if (timeSinceClock_ > 3.0) clockDetected_ = false;

        // Between edges, advance at the rate implied by the measured period.
        const double loopSeconds = clockPeriodSeconds_ * clocksPerLoop_ / clockDivision_;
        if (loopSeconds > 1e-9) {
            const double increment = 1.0 / (loopSeconds * sampleRate_);
            phase_ += increment;
            totalPhaseAdvanced_ += increment;
            if (phase_ >= 1.0) {
                phase_ -= std::floor(phase_);
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
    int pulseCount_ = 0;
    bool downbeat_ = false;
    bool clockDetected_ = false;

    SchmittTrigger clockTrigger_;
    SchmittTrigger resetTrigger_;
};

}  // namespace stems
}  // namespace WiggleRoom
