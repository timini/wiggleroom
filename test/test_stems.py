#!/usr/bin/env python3
"""
Stems core test driver.

Shells out to the stems_test executable and asserts on its JSON output. The
Stems core in src/common/stems/ is framework-free, so stems_test links nothing,
not even the Rack SDK, and these tests run anywhere the repo builds.

Run:  python3 test/test_stems.py
      pytest test/test_stems.py -v
"""

import json
import subprocess
import sys
from pathlib import Path

project_root = Path(__file__).resolve().parent.parent


def get_test_executable() -> Path:
    for candidate in (project_root / "build" / "test" / "stems_test",
                      project_root / "build" / "stems_test"):
        if candidate.exists():
            return candidate
    return project_root / "build" / "test" / "stems_test"


def run_test(args: list[str]) -> dict:
    exe = get_test_executable()
    if not exe.exists():
        raise FileNotFoundError(
            f"stems_test not found at {exe}. Run 'just build' first."
        )
    result = subprocess.run(
        [str(exe)] + args, capture_output=True, text=True, timeout=60
    )
    if result.returncode != 0:
        raise AssertionError(
            f"stems_test {' '.join(args)} exited {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return json.loads(result.stdout.strip().split("\n")[0])


class TestFftBackend:
    """The FftBackend contract, exercised through ReferenceFft.

    ReferenceFft is what stems_test uses in place of the Rack adapter's
    RealFFT, so these tests also define the contract any adapter must meet.
    """

    def test_roundtrip_reconstructs_input(self):
        """inverse(forward(x)) must reproduce x to 1e-5."""
        result = run_test(["--test-fft-roundtrip", "--size=2048"])
        assert result["failed"] == 0
        assert result["max_error"] < 1e-5, f"round-trip error {result['max_error']}"

    def test_roundtrip_across_sizes(self):
        """Round-trip must hold for every supported transform size."""
        result = run_test(["--test-fft-sizes"])
        assert result["failed"] == 0
        assert result["worst_error"] < 1e-5, (
            f"worst error {result['worst_error']} at size {result['worst_size']}"
        )

    def test_impulse_is_flat(self):
        """A unit impulse must give unit magnitude in every bin."""
        result = run_test(["--test-fft-impulse", "--size=2048"])
        assert result["failed"] == 0
        assert result["max_error"] < 1e-5

    def test_bin_centred_sine_does_not_leak(self):
        """A sine at exactly bin k must put its energy in bin k alone.

        Guards against off-by-one errors in the bin indexing and against a
        wrong normalisation convention, both of which would silently corrupt
        the STFT built on top of this.
        """
        for bin_index in (1, 64, 512):
            result = run_test(["--test-fft-sine", "--size=2048", f"--bin={bin_index}"])
            assert result["failed"] == 0
            assert result["leakage_ratio"] < 1e-6, (
                f"bin {bin_index} leaked {result['leakage_ratio']}"
            )

    def test_self_test_passes(self):
        """The aggregate self-test must report no failures."""
        result = run_test(["--self-test"])
        assert result["failed"] == 0, f"self-test reported {result['failed']} failures"
        assert result["passed"] > 0


class TestRingBuffer:
    """The capture buffer.

    Not rack::dsp::RingBuffer, whose capacity is a template parameter and so
    cannot depend on sample rate. This one allocates once at construction and
    never reallocates, which is what makes the audio-thread write path safe.
    """

    def test_written_samples_read_back_bit_exact(self):
        result = run_test(["--test-buffer-roundtrip"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_wraparound_overwrites_oldest(self):
        """Writing past capacity must drop the oldest frames, not corrupt or grow.

        Index 0 stays the oldest surviving frame so callers see a stable
        timeline however far the write head has wrapped.
        """
        result = run_test(["--test-buffer-wraparound"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_no_reallocation_after_construction(self):
        """Capacity and storage address must be stable for the object's life.

        A reallocation on the audio thread would be a real-time violation, and
        would invalidate any pointer a reader was holding.
        """
        result = run_test(["--test-buffer-no-alloc"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_fractional_reads_interpolate(self):
        """Vari-speed playback reads at fractional positions."""
        result = run_test(["--test-buffer-interpolate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_positions_are_rejected(self):
        """inf and NaN playback positions must give silence, not NaN audio.

        Casting inf or NaN to size_t is undefined behaviour, and NaN slips past
        a bare `position < 0` guard because every NaN comparison is false. NaN
        audio would then propagate through the entire patch.
        """
        result = run_test(["--test-buffer-non-finite"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clear_does_not_touch_storage(self):
        """clear() must be O(1) and audio-thread safe.

        Zeroing the allocation is up to 24.6 MB at 96 kHz stereo over the
        32 second cap, written synchronously exactly when a new take starts.
        Resetting the counters already makes old samples unreachable.
        """
        result = run_test(["--test-buffer-clear-cheap"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_capacity_follows_rate_and_duration_cap(self):
        """Capacity is sampleRate * maxSeconds, which is how the spec's 32
        second cap is enforced across 44.1k, 48k and 96k."""
        result = run_test(["--test-buffer-capacity"])
        assert result["failed"] == 0, result.get("detail", "")


class TestRingBufferHardening:
    """Guarding the point where audio enters the module.

    Note that these guards are only real because Release builds now pass
    -fno-finite-math-only. -ffast-math implies -ffinite-math-only, which is a
    promise that no value is ever NaN or infinite, and the compiler acts on it
    by folding every finiteness test to true. A bit-pattern check does not
    survive it either, since the promise is about the values. `just test-native`
    compiles stems_test with the real Release flags and runs these checks under
    them, because the rest of the suite builds as Debug and would not notice.
    """

    def test_non_finite_input_is_not_stored(self):
        """This is the path by which a bad sample reaches everything else: HPSS
        carries a NaN through the FFT into all four stems, and from there into
        every oscillator frame and every value the mixer publishes. Guarding on
        write costs one comparison per sample and saves guarding every consumer.
        Per-sample, so the good channel of a half-bad frame survives.
        """
        result = run_test(["--test-buffer-non-finite-write"])
        assert result["failed"] == 0, result.get("detail", "")


class TestTransport:
    """Clock tracking and repitch playback.

    Phase-locking rather than free-running estimation: the playhead advances at
    the measured rate but snaps to the grid on every clock edge, so error is
    bounded by one clock interval and never accumulates.
    """

    def test_stays_locked_over_a_long_run(self):
        """1000 bars at 120 BPM with under one frame of downbeat drift.

        Drift is measured in audio frames, scaled by frames-per-loop. An earlier
        version scaled only by clocksPerLoop, which measures clock intervals: at
        120 BPM and 48 kHz that let roughly 24000 frames pass a "1 frame"
        assertion. Verified to have teeth: removing the phase snap now fails
        with 27999 frames of drift.
        """
        result = run_test(["--test-transport-lock"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_returns_to_downbeat_immediately(self):
        """Reset must land on the downbeat, not wait for the next clock edge."""
        result = run_test(["--test-transport-reset"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clock_division_scales_rate(self):
        result = run_test(["--test-transport-division"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_loop_bounds_window_the_playhead(self):
        result = run_test(["--test-transport-loop"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clock_division_applies_to_edge_snaps(self):
        """Division must scale the snap grid, not just the free-run rate.

        Snapping to the x1 grid while free-running at x2 drags the playhead
        backwards on every clock edge.
        """
        result = run_test(["--test-transport-division-snap"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_consumes_a_coincident_clock_edge(self):
        """PreFlightClock in this repo fires clock and reset together on the
        downbeat, so this is the normal integration. If reset returns early
        without consuming the edge, the still-high clock registers as fresh on
        the next sample and steps the phase one clock off the downbeat."""
        result = run_test(["--test-transport-reset-coincident"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_loop_window_stays_inside_the_buffer(self):
        """A loop start of 1 is a legal end of the parameter range and must not
        place the playhead at or beyond the buffer end."""
        result = run_test(["--test-transport-loop-max-start"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_sample_rate_change_preserves_tempo(self):
        """The period is stored in seconds and needs no recalculation. Resetting
        it would play a 30 BPM loop at four times its rate until the next edge."""
        result = run_test(["--test-transport-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_first_edge_after_idle_sets_origin_only(self):
        """Otherwise the idle duration is recorded as the clock period and
        playback crawls until the second edge, then jumps."""
        result = run_test(["--test-transport-clock-restart"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_clock_jitter_does_not_manufacture_downbeats(self):
        """A downbeat is an integer loop-boundary crossing, not any backward
        phase correction. A late edge corrects backwards while still at, say,
        3/16; treating that as a wrap fired spurious downbeat triggers
        throughout the loop. Measured 88 downbeats over 10 loops before the fix.
        """
        result = run_test(["--test-transport-downbeat-jitter"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_between_edges_preserves_clock_timing(self):
        """Clearing the elapsed timer on an asynchronous reset made the next
        edge measure only the reset-to-edge fragment. A reset halfway through a
        120 BPM interval recorded 0.25 s and doubled playback speed."""
        result = run_test(["--test-transport-reset-midinterval"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_free_runs_safely_without_a_clock(self):
        """No clock must not produce NaN or an out-of-range phase.

        This is the state on patch load, before anything is patched.
        """
        result = run_test(["--test-transport-no-clock"])
        assert result["failed"] == 0, result.get("detail", "")


class TestStft:
    """Short-time Fourier transform front end.

    Parameters match librosa defaults (n_fft 2048, hop 512) so HPSS results can
    be cross-checked against a reference implementation in S6 rather than only
    against themselves.
    """

    def test_reconstructs_the_input(self):
        """Analysis then synthesis with no processing must return the input.

        Asserted over the WHOLE signal including the first and last samples.
        An earlier version skipped a frame at each end, which concealed a real
        boundary gap: without padding the tail after the last complete frame is
        silent, and the first sample is never covered because the Hann window
        is zero there.

        Everything downstream depends on this: if the STFT does not round-trip,
        every HPSS mask is applied to a signal that was already wrong. Verified
        to have teeth twice: removing the window-sum normalisation fails at 0.4,
        removing the padding fails at 0.8.
        """
        result = run_test(["--test-stft-reconstruct"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reconstructs_at_lengths_that_are_not_hop_multiples(self):
        """Framing bugs live at lengths that are not multiples of the hop."""
        result = run_test(["--test-stft-odd-lengths"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_zeroing_every_bin_gives_silence(self):
        """Proves the modify callback is actually wired into the round-trip."""
        result = run_test(["--test-stft-zero-mask"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_short_and_empty_input_is_safe(self):
        """Inputs shorter than one frame must not read out of bounds or emit NaN."""
        result = run_test(["--test-stft-short-input"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_tiny_fft_does_not_hang(self):
        """A size-2 backend derives hop = size/4 = 0, and the frame loop would
        never advance, hanging the worker thread forever. FftBackend permits
        sizes that small, so the hop is clamped to at least one sample."""
        result = run_test(["--test-stft-tiny-fft"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_window_satisfies_cola(self):
        """The squared Hann window must sum to a constant at hop = n/4.

        Without this, overlap-add amplitude-modulates the signal instead of
        reconstructing it. Verified to have teeth: the symmetric Hann variant,
        a common mistake, ripples and fails.
        """
        result = run_test(["--test-stft-cola"])
        assert result["failed"] == 0, result.get("detail", "")


class TestHpss:
    """Tier 0 separation: median-filter HPSS plus a low band split.

    Chosen over a neural model because the instrument needs material that
    differs from itself, not correctly labelled instruments. That keeps ML
    deployment off the critical path entirely.

    The median filter itself is cross-validated against scipy in
    test_hpss_reference.py.
    """

    def test_separates_percussive_from_sustained(self):
        """Clicks must land in the percussive layer and a sine in the harmonic.

        Measured by correlation against the known sources rather than by ear.
        Verified to have teeth: swapping the two masks makes the percussive
        layer correlate 0.9995 with the sine.
        """
        result = run_test(["--test-hpss-separates"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_layers_are_disjoint_and_sum_to_source(self):
        """All four layers at unity must reconstruct the source.

        The spec requires disjoint masks: an earlier draft defined Harmonic
        without excluding the Low band, so the default all-faders-at-unity
        state would have double-counted bass.
        """
        result = run_test(["--test-hpss-sum"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_low_layer_captures_low_frequencies(self):
        result = run_test(["--test-hpss-low-band"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_residual_layer_is_populated(self):
        """The fourth layer must carry energy for ordinary dense audio.

        Pure soft masks make the harmonic and percussive shares sum to exactly
        1, leaving Residual permanently silent and the four-layer interface a
        fiction. A margin above 1 attenuates both where the medians are
        comparable, and the unclaimed remainder becomes Residual.
        """
        result = run_test(["--test-hpss-residual"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_low_split_includes_bins_below_the_split(self):
        """The split converts to a bin by ceiling, not rounding.

        Rounding excluded bins whose centre is still below the split: at
        44.1 kHz with a 2048-point FFT and a 200 Hz split, bin 9 sits at about
        193.8 Hz and was being left out of the Low layer.
        """
        result = run_test(["--test-hpss-low-split-boundary"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_margin_scales_before_the_exponent(self):
        """The margin must scale the competing median BEFORE the soft-mask
        exponent: h^p / (h^p + (m*p)^p). Applying it afterwards gives a weaker
        effective margin than configured and correspondingly less residual.
        With power 2 and margin 2, equal medians must give a share of 0.2, not
        the 0.333 the post-exponent form produces."""
        result = run_test(["--test-hpss-margin"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_sub_frame_input_passes_through_to_residual(self):
        """Stft pads a whole frame on both sides, so analysis always yields
        frames and a frames == 0 check would be unreachable. Short recordings
        must be detected by length and copied intact rather than smeared
        spectrally across all four layers."""
        result = run_test(["--test-hpss-subframe"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_degenerate_inputs_are_safe(self):
        """Empty, silent and sub-frame inputs must give correctly sized,
        finite output. Input too short for a single frame is routed to Residual
        so the layers still sum to the source rather than dropping it."""
        result = run_test(["--test-hpss-degenerate"])
        assert result["failed"] == 0, result.get("detail", "")


class TestSeparationWorker:
    """Background separation and safe publication.

    The first threaded code in this repository. Verified clean under
    ThreadSanitizer, not just observed to pass.
    """

    def test_publishes_a_result(self):
        result = run_test(["--test-worker-publishes"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_superseded_job_never_publishes(self):
        """A take superseded mid-separation must be discarded, not published.

        The test waits until the worker has actually BEGUN the first job before
        submitting the second. Submitting back to back does not exercise this:
        the pending slot is overwritten and the first job never starts, so the
        post-separation generation check is never reached. An earlier version of
        this test made exactly that mistake and passed with the check removed.

        Verified to have teeth: removing the check now fails with "no job was
        discarded".
        """
        result = run_test(["--test-worker-stale"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_retired_sets_are_freed_on_the_worker(self):
        """A multi-megabyte deallocation must never land on the audio thread.

        Every free goes through one funnel that records the freeing thread, so
        the counter this asserts on is genuinely wired up rather than a constant
        zero.
        """
        result = run_test(["--test-worker-retire"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_acquire_before_any_job_is_safe(self):
        """This is the state on patch load."""
        result = run_test(["--test-worker-empty"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_stereo_stems_keep_both_channels(self):
        """Four stereo stems, not one side dropped and not the two interleaved.

        The two submitted channels are unrelated signals, so a worker that
        copied one over the other, or read them interleaved as a single signal,
        produces identical layers and fails. Verified to have teeth: making
        submit() copy the left channel into the right fails with "left and right
        layers are identical".
        """
        result = run_test(["--test-worker-stereo"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_separation_failure_is_non_fatal(self):
        """An exception must not escape the thread entry and kill the host.

        Driven by an FFT backend that always throws. Verified to have teeth:
        narrowing the catch so the exception escapes aborts the whole test
        binary with "terminating due to uncaught exception".
        """
        result = run_test(["--test-worker-failure"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_injected_fft_backend_is_the_one_used(self):
        """ReferenceFft is the test reference, not the production backend.

        Without an injection point every host would be forced onto a double
        precision radix-2 implementation for every frame of a recording up to 32
        seconds long.
        """
        result = run_test(["--test-worker-fft-injection"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_shutdown_interrupts_an_in_flight_separation(self):
        """stop() must not wait for a whole separation to finish.

        Clearing a running flag cannot interrupt work already inside
        Hpss::separate, so cancellation is polled once per STFT frame, which is
        where a separation actually spends its time. Measured latency is 14 ms.
        Verified to have teeth: unwiring the abort flag makes stop() wait
        7898 ms against a budget of 2000.
        """
        result = run_test(["--test-worker-abort"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_retired_set_is_reclaimed_when_the_reader_leaves(self):
        """Reclaiming only at publication time leaked until shutdown.

        A set retired while the audio thread held it cannot be freed at that
        moment. If nothing then triggers a revisit, and no further recording is
        made, those buffers stay allocated for the life of the module. Verified
        to have teeth: removing the worker's reclaim poll fails with "never
        reclaimed after the reader released it".
        """
        result = run_test(["--test-worker-reclaim-release"])
        assert result["failed"] == 0, result.get("detail", "")

class TestStemMixer:
    """Four-channel stem playback, the fallback path and the analyser tap."""

    def test_four_unity_faders_reconstruct_the_source(self):
        """Straight sum, not equal power.

        The HPSS masks are disjoint, so the layers already add up to the source.
        An equal-power law would lift each pair by 3 dB. Verified to have teeth:
        inserting a sqrt(2) factor fails at 3.01 dB against a 0.5 dB budget.
        The check is sample by sample as well as by level, so four layers with
        the right total energy in the wrong proportions would still fail.
        """
        result = run_test(["--test-mixer-unity-sum"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_mute_ramps_rather_than_steps(self):
        """Driven with DC layers so any jump in the output is the gain, not the
        material. Verified to have teeth: a hard mute steps by the full 0.25.
        """
        result = run_test(["--test-mixer-mute"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_stem_select_does_not_step_the_tap(self):
        """The tap is a live signal feeding the wavetable, so a hard switch
        between two stems is an audible click. Verified to have teeth: switching
        outright steps by 2.0 between the two constant stems used here.
        """
        result = run_test(["--test-mixer-select"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_rapid_stem_select_is_still_continuous(self):
        """Switching again before the previous crossfade finishes.

        This is the case a crossfade between two stem indices gets wrong: the
        outgoing index still names the original stem, so the fade restarts from
        that stem's current sample rather than from the half-mixed value
        actually being emitted. Verified to have teeth: the index-based version
        steps by 1.58, while the ordinary single-switch test still passes.
        """
        result = run_test(["--test-mixer-select-rapid"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_withdrawing_the_stems_fades_out(self):
        """The stems cannot be read once the set is gone, so zeroing them would
        collapse that side of the mix in a single sample while the fallback was
        still fading in. Verified to have teeth: zeroing steps by 0.24 against a
        material step of 0.07.
        """
        result = run_test(["--test-mixer-withdraw"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_fallback_uses_channel_one_only(self):
        """Spec scenario 15. Routing the unseparated buffer to all four unity
        channels sums four identical copies. Verified to have teeth: widening
        the routing gives 2.0 where 0.5 is expected.
        """
        result = run_test(["--test-mixer-fallback"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_level_is_preserved_across_the_transition(self):
        """The level during separation and after stems arrive must match.

        Verified to have teeth: routing the fallback to all four channels fails
        at -12.04 dB, which is exactly the step the spec warns about.
        """
        result = run_test(["--test-mixer-fallback-level"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_transition_ramps(self):
        """Separate from the level test, and deliberately so.

        In the level test the fallback is the same recording the layers sum to,
        so a hard switch is already continuous there and removing the crossfade
        changes nothing. Here the fallback is a different signal of the same
        RMS. Verified to have teeth: switching outright steps by 0.38 against a
        material step of 0.07.
        """
        result = run_test(["--test-mixer-crossfade"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_stereo_sets_keep_both_channels(self):
        """Also covers a set flagged stereo whose right channel is missing,
        which must fall back to the left rather than index out of range."""
        result = run_test(["--test-mixer-stereo"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_playhead_gives_silence(self):
        """Casting NaN or infinity to size_t is undefined, and NaN slips past a
        bare `< 0` comparison. The RingBuffer had exactly this defect."""
        result = run_test(["--test-mixer-non-finite"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_empty_stem_set_is_silent(self):
        """This is the state on patch load."""
        result = run_test(["--test-mixer-empty"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_fade_time_does_not_change_with_sample_rate(self):
        """Fades are held in seconds. Verified to have teeth: pinning the step
        to 48 kHz halves every fade at 96 kHz."""
        result = run_test(["--test-mixer-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

class TestYin:
    """Fundamental frequency estimation, de Cheveigne and Kawahara (2002)."""

    def test_sines_across_the_range_within_one_cent(self):
        """55 Hz to 2 kHz, at two window phases each.

        The frequencies deliberately do not divide the sample rate evenly, so
        the true period is never a whole number of samples. Verified to have
        teeth: removing the fractional-lag refinement fails at 1.48 cents at
        2 kHz, where the period is only 24 samples and three integer-spaced
        points are not enough to interpolate.
        """
        result = run_test(["--test-yin-sine-range"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_white_noise_is_not_voiced(self):
        """Confidence is what stops scale detection running on a drum layer.
        Twenty trials, none voiced, none above 0.4 where a pure tone scores 1.0.
        """
        result = run_test(["--test-yin-noise"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_rich_waveforms_do_not_report_an_octave_error(self):
        """Squares, saws, and a stack with a weak fundamental and a strong
        second harmonic.

        A signal that repeats every T also repeats every 2T, so the global
        minimum of the CMNDF often sits at twice the true lag. Verified to have
        teeth: taking the global minimum instead of the first dip below the
        threshold reports 55.0 Hz for a 110 Hz square, a clean -1199.9 cents.
        """
        result = run_test(["--test-yin-octave"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_silence_reports_nothing(self):
        """This is the state on patch load. Verified to have teeth: without the
        energy guard, silence reports a confident-looking 2181 Hz.
        """
        result = run_test(["--test-yin-silence"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_input_is_rejected(self):
        """One NaN poisons every lag in the difference function. Verified to
        have teeth: without the guard a single NaN yields 2181 Hz.
        """
        result = run_test(["--test-yin-non-finite"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_detection_does_not_depend_on_sample_rate(self):
        result = run_test(["--test-yin-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_frequency_range_bounds_the_search(self):
        """Narrowing the range is the only lever on the cost of this, so a range
        that is ignored is a silent performance regression as well as a
        correctness one."""
        result = run_test(["--test-yin-range"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_reported_frequency_stays_inside_the_range(self):
        """Including when the bounds do not divide the sample rate evenly.

        The integer search bounds are widened to whole lags so interpolation has
        a point either side of a minimum near a boundary; letting that widening
        leak into the result puts out-of-band pitches in front of the scale
        detector. Verified to have teeth: without the clamp, a 1110 Hz tone with
        a 300.5 to 1100 Hz range reports 1109.99 Hz. The earlier range test used
        300 and 2000 Hz, which are integer-friendly at 48 kHz, so it never
        exercised this.
        """
        result = run_test(["--test-yin-range-exact"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_range_spanning_two_lags_still_works(self):
        """At 48 kHz a 1000 to 1020 Hz range is lags 47 and 48, and the
        fractional refinement resolves between them. Verified to have teeth:
        requiring a wider span returns nothing at all for a clean 1010 Hz tone
        sitting inside the range.
        """
        result = run_test(["--test-yin-narrow-range"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_parameters_are_rejected(self):
        """std::min and std::max propagate NaN rather than clamping it, because
        every comparison against NaN is false.

        A NaN threshold makes every `cmndf >= threshold` test false, so the first
        lag examined is accepted as voiced whatever the signal is. A NaN
        frequency bound is worse: it reaches a cast to size_t, which is
        undefined. Verified to have teeth in both directions.
        """
        result = run_test(["--test-yin-bad-params"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_analysis_does_not_allocate(self):
        """Including for windows shorter than the constructed maximum, which is
        where a naive implementation would resize to fit."""
        result = run_test(["--test-yin-no-alloc"])
        assert result["failed"] == 0, result.get("detail", "")

class TestScaleDetect:
    """Krumhansl-Schmuckler key finding, with the two gates the spec requires."""

    def test_a_c_major_scale_detects_c_major(self):
        """Equal weights, no tonic emphasis. The published profiles are
        asymmetric enough to carry this on their own."""
        result = run_test(["--test-scale-c-major"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_every_transposition_of_both_modes(self):
        """All twenty-four keys."""
        result = run_test(["--test-scale-transpose"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_relative_major_and_minor_are_told_apart(self):
        """C major and A minor contain exactly the same seven pitch classes, so
        nothing but the weighting of those classes can separate them. Verified
        to have teeth: consulting only the major profile reports A major for A
        minor.
        """
        result = run_test(["--test-scale-relative"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_key_survives_chromatic_bleed(self):
        """A real recording puts some weight in every pitch class.

        That constant floor is what separates a correlation from a dot product:
        the dot product is then dominated by the profile sums, and the minor
        profile sums higher (44.51 against 41.79). Verified to have teeth: a dot
        product reports A minor for C major. The plain relative-key test passes
        against that substitution, so this needed its own case.
        """
        result = run_test(["--test-scale-chromatic"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_low_confidence_pitches_are_ignored_even_when_they_agree(self):
        """A percussive layer has spectral peaks, so YIN latches onto the same
        wrong pitches repeatedly and the histogram is strongly biased rather
        than flat. Only the per-pitch confidence gate stops that deciding the
        key. Verified to have teeth: without it, 200 sub-threshold detections
        move C major to F# major.
        """
        result = run_test(["--test-scale-low-conf"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_an_unpitched_stem_holds_the_last_key(self):
        """Random frequencies make a flat histogram with no tonal centre, which
        the key confidence gate rejects."""
        result = run_test(["--test-scale-unpitched"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_few_pitches_cannot_decide_a_key(self):
        """Correlation alone will not stop this: a histogram with three bins
        filled correlates with something, and correlates well. Verified to have
        teeth: without the minimum weight gate, three pitches detect D# minor at
        confidence 0.89.
        """
        result = run_test(["--test-scale-weight-gate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_manual_seed_covers_the_empty_state(self):
        """A fresh module has no last confident result to hold, so it seeds from
        the manual root and scale. Covers the empty buffer, an unpitched first
        recording, and the window while separation is still running. Also checks
        the seed stops overriding once a real detection lands.
        """
        result = run_test(["--test-scale-seed"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_key_follows_a_change_in_the_material(self):
        """Asserts the contrast, not just the outcome: a long stretch of C major
        followed by a short stretch of F# major must move WITH decay and must
        NOT move without it. Checking only the decaying detector passed with the
        decay removed entirely.
        """
        result = run_test(["--test-scale-key-change"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_held_result_reports_that_it_is_held(self):
        """The spec requires the UI to show analysis is inactive on a percussive
        stem, and the histogram alone cannot say so: nothing reaches it, so it
        looks exactly as it did when the last real key was found and detect()
        goes on re-reporting that as a current detection. A running fraction of
        recent offers that were pitched enough to count is what distinguishes
        them. Verified to have teeth in both parts.
        """
        result = run_test(["--test-scale-inactive"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_evidence_gate_is_reachable_at_any_decay(self):
        """The histogram is a decaying accumulator, so its total is bounded by
        1 / (1 - decay). At decay 0.875 that ceiling is exactly the default
        minimum of 8, and below it the gate can never open however many pitches
        arrive. Decay is now floored at 0.9, and the gate is separately
        reconciled against the reachable ceiling. Verified to have teeth.
        """
        result = run_test(["--test-scale-decay-gate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_sustained_note_is_not_a_scale(self):
        """The weight gate counts observations and says nothing about whether
        they contain enough distinct pitches to imply a key. Verified to have
        teeth: without the tonal spread gate, one sustained note declares its
        own major key at confidence 0.68. A root and fifth is also rejected; a
        triad is accepted, so the guard does not reject ordinary material.
        """
        result = run_test(["--test-scale-sustained"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_pitch_gate_agrees_with_yin(self):
        """YIN marks a lag voiced when its CMNDF is below its threshold and
        reports confidence as 1 - CMNDF, so at the default 0.12 the equivalent
        cutoff is 0.88, not 0.5.

        Driven with real YIN output on noisy tones and damped percussive hits,
        which land between 0.5 and 0.88 while marked unvoiced, usually on the
        wrong frequency. White noise will not do here: it scores under 0.07, so
        a cutoff of 0.5 rejects it too and the test would pass whatever the
        threshold was. Verified to have teeth: at 0.5 this material contributes
        42.6 of weight.
        """
        result = run_test(["--test-scale-yin-gate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_flat_histogram_has_no_key(self):
        """Zero variance, no tonal centre, no NaN."""
        result = run_test(["--test-scale-flat"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_bad_input_is_rejected_or_clamped(self):
        """A bad frequency or a negative confidence contributes nothing. An
        absurdly large confidence is clamped: found while writing this test,
        where one call at 1e9 swamped every other bin and pinned the key to that
        single pitch.
        """
        result = run_test(["--test-scale-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")

class TestQuantizer:
    """Scale-snapping CV output with portamento."""

    def test_a_ramp_gives_a_monotonic_staircase(self):
        """Five keys, five octaves, 60000 points each.

        Every non-scale semitone in a seven-note scale sits exactly between two
        degrees, so the tie-break has to be consistent or a rising input steps
        backwards at some boundaries. Verified to have teeth: searching only the
        input's own octave breaks it.
        """
        result = run_test(["--test-quant-staircase"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_quantisation_does_not_depend_on_history(self):
        """Rising, falling and randomly ordered inputs must agree.

        This catches hysteresis, which the ramp test cannot: preferring the
        previously chosen degree on a tie keeps a RISING ramp monotonic while
        still giving a different answer for the same input depending on the
        approach direction. Verified to have teeth: real hysteresis fails here
        and passes the staircase test.
        """
        result = run_test(["--test-quant-stateless"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_same_degree_an_octave_apart_differs_by_one_volt(self):
        """To float precision, which is the floor here: process() returns float
        because that is what a CV output is, and float rounding at these
        magnitudes is about 1.2e-7 V, or 1.4e-5 cents. The internal degrees are
        exactly twelve semitones apart; the cast is what loses it.
        """
        result = run_test(["--test-quant-octave"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_zero_glide_is_instantaneous(self):
        """One sample, not merely fast. Verified to have teeth: flooring the
        time constant so zero becomes a very short glide leaves an octave jump
        at 0.65 after one sample.
        """
        result = run_test(["--test-quant-glide-zero"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_glide_takes_its_time_and_then_arrives(self):
        """Three assertions, and each is needed.

        It must be most of the way there after its time, clearly not there after
        a quarter of it (without which an instantaneous glide would pass), and
        it must ARRIVE within 2.5 times its time. A bare exponential reaches the
        target eventually, once the increment falls below an ULP, so checking
        only that it lands there passes with the settle snap removed. Verified
        to have teeth at 2.5x.
        """
        result = run_test(["--test-quant-glide"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_glide_time_does_not_change_with_sample_rate(self):
        result = run_test(["--test-quant-glide-sr"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_manual_override_beats_detection(self):
        """Including while the detector changes its mind underneath it. A user
        who has reached for the override has said what they want."""
        result = run_test(["--test-quant-manual"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_every_scale_emits_only_its_own_degrees(self):
        """Fourteen scales, twelve roots each, and never moving an input further
        than half the largest gap in any of them."""
        result = run_test(["--test-quant-scales"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_non_finite_input_does_not_corrupt_the_output(self):
        result = run_test(["--test-quant-non-finite"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_large_finite_input_is_bounded(self):
        """Rejecting NaN and infinity is not enough.

        A value like 1e20 volts puts floor(semitones / 12) outside long long,
        and converting it is undefined: three UndefinedBehaviorSanitizer reports,
        and a release build silently produced 0 V. Verified to have teeth.
        """
        result = run_test(["--test-quant-extreme"])
        assert result["failed"] == 0, result.get("detail", "")


class TestWavetableExtract:
    """Building fixed-size oscillator frames from a stem."""

    def test_the_frame_is_the_same_length_at_every_window_size(self):
        """This is what makes wt_window change how much source material is
        captured rather than the oscillator's pitch: the fundamental is set by
        how fast the oscillator reads a frame."""
        result = run_test(["--test-wt-frame-size"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_longer_window_captures_more_material(self):
        """The companion to the test above. Holding the frame length fixed is
        only half the requirement; the control also has to do something.
        Verified to have teeth: pinning the read step to one sample makes every
        window capture the same thing.
        """
        result = run_test(["--test-wt-window-content"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_work_is_spread_evenly_across_calls(self):
        """Building a whole frame in the call where the playhead crosses a
        boundary is a spike, and the spike lands on the audio thread.

        Reading the window and finalising it are both amortised, and the budget
        counts WORK rather than output samples.

        Counting output samples was wrong twice over. Doing the mean, the peak
        and the copy in the call that completed the read added three whole extra
        passes to one call in sixteen, uncounted. And at a window of 8192 each
        output sample costs four source reads, so a reading call did four times
        the work of a finalising call while both reported the same number.
        Checked across three windows and three budgets. Verified to have teeth
        in both forms.
        """
        result = run_test(["--test-wt-amortised"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_default_budget_publishes_in_sixteen_calls(self):
        """A frame costs kFrameSize * (span + 1) units, reading then writing.

        Sizing the default for the reading phase alone doubled the real latency:
        32 calls rather than 16, which at one call per 256 sample block is
        171 ms instead of 85, and that is the age of the snapshot before any
        morphing is applied. Verified to have teeth.
        """
        result = run_test(["--test-wt-default-latency"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_rearms_the_degenerate_take_handling(self):
        """Leaving the handled flag set while dropping the phase meant a reset
        partway through replacing a frame satisfied the already-handled test
        forever afterwards, so finalisation never restarted and the previous
        audible frame stayed visible. Patch load and sample rate changes both
        call reset(). Verified to have teeth.
        """
        result = run_test(["--test-wt-reset-rearm"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_budget_below_the_span_raises_the_bound(self):
        """A slot's source reads are averaged together, so a slot cannot be
        split across calls and is the smallest indivisible unit of work.
        Advertising a bound of one and then doing four reads is worse than
        admitting the floor. Verified to have teeth.
        """
        result = run_test(["--test-wt-tiny-budget"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_unit_ratio_does_no_averaging(self):
        """Starting the slot at the mapped position rather than centring on it
        puts every read half a step late, so at the DEFAULT window each output
        sample was the mean of two adjacent source samples. Verified to have
        teeth: a stem alternating between +1 and -1 comes out completely silent.
        """
        result = run_test(["--test-wt-unit-ratio"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_advertised_work_bound_follows_the_window(self):
        """The span is only recalculated when a build begins, so reading it
        alone reported the previous window's bound to anyone inspecting straight
        after changing the setting, which is exactly when a caller looks.
        Verified to have teeth: a bound of 1 reported for an 8192 window.
        """
        result = run_test(["--test-wt-budget-window"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_moving_window_still_produces_frames(self):
        """Restarting the build whenever wt_window changed meant automating the
        control, or turning it slowly enough to cross an integer on each call,
        discarded the progress every time.

        Everything the build depends on is snapshotted, so finishing at the old
        size cannot mix two windows; the new size applies to the next build.
        Verified to have teeth: restarting publishes ZERO frames in 4000 calls,
        so the oscillator keeps the previous wavetable until the control stops
        moving, which is the opposite of what a moving control should do.
        """
        result = run_test(["--test-wt-window-automation"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_edge_taper_does_not_put_dc_back(self):
        """Removing the source mean makes the untapered window zero-mean, but
        multiplying by a fade that is not symmetric about the content shifts it
        again, and the offset eats headroom in the oscillator and the lowpass
        gate. The tapered mean is accumulated during the read, so correcting it
        costs no extra pass. Verified to have teeth: a lopsided zero-mean window
        publishes a frame sitting 5% of full scale off centre.
        """
        result = run_test(["--test-wt-taper-dc"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_stereo_stems_contribute_both_channels(self):
        """The voice is mono and there is no channel selector, and the worker
        separates the two sides independently, so the right channel carries
        different material. Verified to have teeth: reading only the left
        channel publishes a SILENT wavetable for a stem panned hard right. Also
        covers mono sets and stereo sets whose right channel is missing.
        """
        result = run_test(["--test-wt-stereo"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_no_frame_ever_exceeds_full_scale(self):
        """The taper correction is subtracted after scaling, so it shifts every
        sample: a window whose faded edge leans one way against an interior peak
        leaning the other pushed that peak past unity, and downstream stages
        then clip content this class claims to have normalised.

        The bound is analytic rather than measured, so it costs no extra pass:
        the taper never exceeds one, so the shifted result is within
        sourcePeak + |taperMean|. Verified to have teeth: 1.024 without it.
        Also sweeps random material across every window and several playheads.
        """
        result = run_test(["--test-wt-full-scale"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_frames_follow_the_playhead(self):
        """Driven with noise, so successive windows genuinely differ rather than
        repeating a periodic waveform that would look the same wherever it was
        sampled. Also checks the same playhead gives the same frame."""
        result = run_test(["--test-wt-tracks"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_build_does_not_smear_across_playhead_motion(self):
        """The position is snapshotted when a build begins. Re-reading it every
        call spreads one frame over however far the transport moved, so the
        frame corresponds to no actual moment in the material. Verified to have
        teeth: a sweeping playhead changes the frame by 1.77.
        """
        result = run_test(["--test-wt-snapshot"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_frames_are_normalised_and_dc_free(self):
        """Silence is checked twice, and the second case is the one that
        matters. Exact zeros stay zero under any gain, so a missing guard passes
        that; denormal-level noise, which is what a real empty buffer holds
        after a filter has run over it, gets lifted to full scale. Verified to
        have teeth at 1e-9.
        """
        result = run_test(["--test-wt-normalise"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_frame_joins_up_at_the_wrap_point(self):
        """The frame is read cyclically and source audio has no reason to join
        up, so the step is a click at the oscillator's own frequency. Measured
        against the worst step inside the frame, so the bar scales with how fast
        the waveform is moving. Verified to have teeth: without the edge fade
        the wrap step is 1.50 against an interior step of 0.002.
        """
        result = run_test(["--test-wt-loop"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_decimation_is_averaged_not_point_sampled(self):
        """Measured on the PUBLISHED frame, not on a pre-normalisation
        diagnostic.

        Normalising to the frame's own peak undoes the filter: a tone rejected
        down to five per cent gets multiplied by twenty and published at unity,
        so the anti-aliasing exists only in the diagnostic. The frame is
        normalised against the SOURCE window's peak instead, so whatever the
        filter rejected stays rejected. Verified to have teeth: frame-peak
        normalisation publishes 20 kHz at 1.0 against a bar of 0.3.
        """
        result = run_test(["--test-wt-antialias"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_filter_holds_at_fractional_window_ratios(self):
        """Nearly every position of a continuous control gives a fractional
        window-to-frame ratio, and a window of 4095 has a step just under two.
        Truncating that to a single sample turns the filter off exactly where it
        is still needed. Verified to have teeth: truncation publishes a 16.8 kHz
        tone at full scale, while the exact 4:1 test above still passes.
        """
        result = run_test(["--test-wt-fractional"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_dc_is_removed_before_the_taper(self):
        """Tapering first multiplies the offset by the fade, so a flat input
        becomes a shape that rises and falls with the window and then normalises
        to full scale. Verified to have teeth: a tone on a 0.9 offset leaves a
        frame DC of -0.45.
        """
        result = run_test(["--test-wt-dc-source"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_one_bad_stem_sample_does_not_poison_the_frame(self):
        """RingBuffer stores what it is given, so this is reachable from a
        misbehaving upstream module. Without a guard the value spreads through
        the mean and the gain into every value of every frame published
        afterwards.
        """
        result = run_test(["--test-wt-nan-stem"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reading_past_the_stem_does_not_manufacture_a_waveform(self):
        """The window is centred on the playhead, so at the loop start half of
        it lies before the beginning of the material, and wt_offset can push it
        out entirely.

        Zero padding puts artificial silence into the mean and the peak, and
        after DC removal the padded half and the real half come out equal and
        opposite. Verified to have teeth: a constant 1.0 stem at playhead 0
        produces a full-scale square instead of silence. The loop start is not
        an edge case; it is where the playhead sits at the top of every bar.
        """
        result = run_test(["--test-wt-boundary"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_degenerate_stem_set_invalidates_the_old_frame(self):
        """Hpss.separate sizes every layer to the input length and accepts
        sub-frame input, so a zero or one frame StemSet really can be published.

        Returning early on the size, before checking whether the set is stale,
        left frame() showing the previous recording indefinitely. Verified to
        have teeth.

        Replacing the frame runs through the same amortised path as building
        one, driven by a gain of zero, so a very short take cannot recreate the
        frame-boundary spike the reading path exists to avoid. Also checks
        silence is issued once per take rather than republished on every call,
        and that real material afterwards builds normally.
        """
        result = run_test(["--test-wt-degenerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_stale_snapshot_restarts_the_build(self):
        """A new stem set, a different layer or a changed window mid-build means
        the snapshot no longer describes what is being read. Verified to have
        teeth: without the restart, a frame built across a stem change differs
        from a clean one by 2.0.
        """
        result = run_test(["--test-wt-restart"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_missing_and_malformed_input_is_safe(self):
        """Null stem set, which is the state on patch load, plus empty stems,
        out-of-range layers, non-finite playheads and playheads far outside the
        material."""
        result = run_test(["--test-wt-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")


class TestWavetableOsc:
    """Playing the extracted frames, with mip-mapped antialiasing."""

    def test_the_alias_floor_is_low_and_flat_across_the_range(self):
        """A frame repeats exactly, so folded content lands on fixed inharmonic
        partials rather than spreading into noise. That reads as a metallic ring
        under the note, and a floor that climbs with pitch is what makes it
        noticeable.

        Measured by classifying every significant spectral peak as harmonic or
        not. Probing a handful of fixed frequencies does not work: aliases land
        at |k*f0 - m*sr|, and a first attempt measured almost no difference
        between a mip-mapped and a raw oscillator purely because it looked in
        the wrong places. Verified to have teeth: without the chain the floor
        climbs from -25.9 dB to -12.6 dB over five octaves, where with it the
        floor holds flat at about -31 dB.
        """
        result = run_test(["--test-osc-alias"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_pitch_tracks_one_volt_per_octave(self):
        """Including that coarse and fine add on top, in semitones."""
        result = run_test(["--test-osc-pitch"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_pitch_does_not_depend_on_sample_rate(self):
        result = run_test(["--test-osc-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_swapping_frames_does_not_click(self):
        """Measured against the steady-state step of EACH frame played alone.

        Comparing the transition against only the first frame's step is not a
        test: the second frame here is a square whose own edge is four times
        larger than anything in a smooth frame, so the first version of this
        failed on the material rather than on any click. Verified to have teeth
        against both a hard switch and fading into a chain that is still being
        built.
        """
        result = run_test(["--test-osc-frame-change"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_slow_morph_still_reaches_new_frames(self):
        """While a crossfade runs, the incoming side is being sounded.

        Rebuilding it because a newer frame arrived cancels the fade before the
        sides can swap, and with a morph slower than the extractor's publish
        interval that happens on every publish. The default extractor publishes
        roughly every 85 ms while morph 0 fades for a second, so this is the
        ordinary case, not an extreme.

        Compared by SPECTRUM, not sample by sample: the two runs sit at
        different points in the cycle, so an identical signal can differ by
        twice its amplitude. An earlier version compared waveforms and reported
        a difference of 1.66 between two runs of the same material. Verified to
        have teeth.
        """
        result = run_test(["--test-osc-slow-morph"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_defaults_crossfade_without_touching_a_setter(self):
        """The morph coefficient was only computed inside the setters, so at the
        documented defaults it stayed zero and the crossfade never advanced.

        The test configures NOTHING, not even the sample rate, because
        setSampleRate also recomputes the coefficient and a test that calls it
        exercises the setter rather than the defaults. Verified to have teeth.
        """
        result = run_test(["--test-osc-defaults"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_reset_leaves_the_oscillator_able_to_play(self):
        """Clearing the build while keeping the frame it was for meant a later
        offer of that same frame saw neither a changed count nor a running
        build, so it could never become playable.

        The case that matters is a reset partway through building a SECOND frame
        while a first is already playing. With nothing playing yet the
        not-ready check restarts the build anyway, so that path passes either
        way. Verified to have teeth.
        """
        result = run_test(["--test-osc-reset"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_mip_chain_covers_the_whole_pitch_range(self):
        """Eight levels ended at a sixteen sample table carrying eight
        harmonics, valid only to about 3 kHz, while an ordinary 1 V/oct input
        crosses that by 3.5 V and keeps going. Past the end of the chain the
        selection saturates and the content aliases. Verified to have teeth:
        eight levels gives a -15.8 dB floor at 4.5 V.
        """
        result = run_test(["--test-osc-range"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_it_works_at_the_extractors_publish_cadence(self):
        """The integration every other test here misses.

        They all offer the same frame count repeatedly, which is not what
        happens: the extractor publishes a new count every sixteen calls while
        this chain takes about sixty-five. Verified to have teeth: restarting on
        any newer frame leaves the oscillator SILENT for the life of the patch,
        peak 0.000000.
        """
        result = run_test(["--test-osc-cadence"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_first_frame_fades_in(self):
        """Before any chain is ready the output is exact silence, so declaring
        the first completed chain current takes it from zero to full level in
        one sample. Verified to have teeth: a step of 0.999 against a
        steady-state step of 0.034.
        """
        result = run_test(["--test-osc-first-frame"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_table_stops_contributing_at_its_nyquist_crossing(self):
        """Blending between the entries either side of the exact position keeps
        the lower one for the whole octave after it has stopped fitting, which
        leaves a band of pitches playing a harmonic the chain should have
        removed. Verified to have teeth: without the shift, a frame with a
        strong 48th harmonic measures -18.6 dB where the shift gives -25.2.
        """
        result = run_test(["--test-osc-crossover"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_crossing_a_mip_boundary_does_not_jump_the_timbre(self):
        """Adjacent tables differ by a whole octave of bandwidth, so a harmonic
        present in one is filtered out of the next. Selecting exactly one entry
        means the smallest pitch modulation across a boundary is an abrupt
        timbral step, so the two entries are blended by frequency instead.
        Verified to have teeth: switching outright also costs 6 dB of alias
        floor at 3 V.
        """
        result = run_test(["--test-osc-mip-boundary"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_top_of_the_range_carries_no_harmonic_above_nyquist(self):
        """The decimation filter is half-band, so its response at the new
        Nyquist is 0.5 rather than zero and that bin survives at every level.

        It only matters at the end of the chain: the four sample table carries a
        fundamental and a second harmonic, and above about 12 kHz that second
        harmonic is itself above output Nyquist and folds back. Verified to have
        teeth: keeping it gives a -5.5 dB alias floor at 6 V.
        """
        result = run_test(["--test-osc-top-range"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_building_the_chain_is_amortised(self):
        """Charged by what a slot actually costs: one copy at level 0, a whole
        run of the fifteen tap filter above it.

        An upper bound alone does not test the accounting, since charging every
        slot one unit also stays under the budget while doing fifteen times the
        work. The CALL COUNT is what exposes it, because it follows the true
        cost. Also checks that an offer after the build completes reports no
        work, since leaving the counter stale made a finished build look like it
        was still running. Verified to have teeth on all three.
        """
        result = run_test(["--test-osc-amortised"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_an_oscillator_with_no_frame_is_silent(self):
        """This is the state on patch load. Also covers a null frame offer."""
        result = run_test(["--test-osc-empty"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_hostile_input_does_not_produce_a_non_finite_sample(self):
        """Also checks a sane input straight afterwards still works, so a bad
        value cannot leave the phase permanently poisoned."""
        result = run_test(["--test-osc-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_level_control_scales_and_reaches_silence(self):
        result = run_test(["--test-osc-level"])
        assert result["failed"] == 0, result.get("detail", "")


class TestLowpassGate:
    """The shared vactrol lowpass gate, used by Stems and TheLantern."""

    def test_the_step_response_shows_the_vactrol_asymmetry(self):
        """About 12 ms up and 250 ms down. The roughly 20:1 ratio is what makes
        a lowpass gate sound plucked rather than merely gated, so it is asserted
        directly rather than left implicit in the two figures. Verified to have
        teeth against a symmetric envelope and against using the rise constant
        uncalibrated, which gives 26.4 ms.
        """
        result = run_test(["--test-lpg-step"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_brightness_and_level_fall_together(self):
        """This is what separates a lowpass gate from a VCA: a VCA closing
        leaves the timbre alone, so a decaying note keeps its harmonics all the
        way down and sounds synthetic.

        Measured as spectral centroid, and checked in BOTH directions: in Both
        mode the centroid must fall, and at the VCA end it must NOT, since
        otherwise any implementation that simply got quieter would pass.
        """
        result = run_test(["--test-lpg-brightness"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_three_colour_positions_are_distinct(self):
        """VCA is the brightest because its filter stays open; Lowpass is the
        loudest because its gain stays up. The midpoint is the classic Both
        position, where the cell drives gain and cutoff together rather than
        half of each.
        """
        result = run_test(["--test-lpg-colour"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_decay_control_means_what_it_says(self):
        """The level-dependent tail stretches the fall, so the coefficient is
        calibrated against the measured time rather than used as a raw constant.
        Checked from 50 ms to 2 s.
        """
        result = run_test(["--test-lpg-decay"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_times_do_not_change_with_sample_rate(self):
        result = run_test(["--test-lpg-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_audio_rate_control_stays_stable(self):
        """Up to 20 kHz on the control input, which is well past anything
        musical, checking both finiteness and that a gate never adds energy."""
        result = run_test(["--test-lpg-audio-rate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_resting_level_holds_the_gate_open(self):
        result = run_test(["--test-lpg-resting"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_hostile_input_is_safe_and_recoverable(self):
        """Also checks the gate still works afterwards, so a bad value cannot
        leave it permanently shut."""
        result = run_test(["--test-lpg-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")


class TestGrainEngine:
    """The granular texturiser: a fixed pool of overlapping windowed reads."""

    def test_the_pool_bounds_concurrency(self):
        """Cost follows density times size, so the top of both controls is
        100 Hz against half a second, or fifty overlapping grains. The pool is
        sized above that with headroom for jitter, so nothing is lost in
        ordinary use and the drop path is a safety net rather than a normal
        outcome.
        """
        result = run_test(["--test-grain-pool"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_grain_level_is_stable_across_density_and_size(self):
        """Grains sum, so without compensation the output rises with the
        overlap: 17 dB of level change from controls meant to change texture.

        Measured as PEAK, not RMS. A sparse setting is legitimately quieter on
        average, since one 20 ms grain per second is a two per cent duty cycle;
        what must not change is how loud the grains themselves are. Verified to
        have teeth.
        """
        result = run_test(["--test-grain-level"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_grain_boundaries_do_not_click(self):
        """Driven with DC, so the source contributes no sample-to-sample change
        and every step in the output is an envelope boundary. Verified to have
        teeth: ending grains on a sample count rather than their own envelope
        steps by 0.23.
        """
        result = run_test(["--test-grain-no-clicks"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_transposition_shifts_the_pitch(self):
        result = run_test(["--test-grain-pitch"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_spread_widens_without_changing_level(self):
        """Constant power holds the level to 0.04 dB where a linear pan law
        shifts it by 1.18, so the tolerance is half a decibel; anything looser
        would not tell them apart. A linear law also dips 3 dB in the centre,
        which reads as the cloud getting quieter as it widens.
        """
        result = run_test(["--test-grain-spread"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_jitter_near_the_buffer_edge_is_bounded(self):
        """Bounds the behaviour rather than pinning the choice: clamping stray
        grains to the edge also lands within tolerance, so this does not
        distinguish it from wrapping. What it does catch is grains landing
        outside the buffer entirely and reading silence.
        """
        result = run_test(["--test-grain-jitter"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_missing_and_poisoned_input_is_safe(self):
        """Null source, which is the state on patch load, plus a one-sample
        buffer, non-finite samples in the source, and non-finite settings."""
        result = run_test(["--test-grain-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")


class TestDiffusion:
    """The space at the end of the granular chain."""

    def test_the_decay_control_sets_the_decay(self):
        """Checked to eight seconds and required to be monotonic, because a
        ceiling that is too low only shows at the top: with the safety limit at
        0.93 the four and eight second settings both measured about four, and a
        test stopping at four would have called that correct. Verified to have
        teeth against both a fixed feedback gain and the low ceiling.
        """
        result = run_test(["--test-diffusion-decay"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_it_does_not_run_away_at_maximum(self):
        """A minute of silence after the input stops, at three damping
        settings. Verified to have teeth: a loop gain of one leaves the tail at
        2.06 a minute later.
        """
        result = run_test(["--test-diffusion-runaway"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_decay_does_not_change_with_sample_rate(self):
        """The gain is derived from the decay time and the delay length, so the
        same knob position means the same seconds at every rate."""
        result = run_test(["--test-diffusion-samplerate"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_mix_reaches_fully_dry_and_fully_wet(self):
        result = run_test(["--test-diffusion-mix"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_damping_takes_brightness_out_of_the_tail(self):
        """Measured as spectral centroid on the tail, driven with noise so
        there is content at every frequency to remove."""
        result = run_test(["--test-diffusion-damping"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_the_two_channels_decorrelate(self):
        """Measured over the tail, since the direct sound is identical on both
        sides by construction and would mask the decorrelation behind it."""
        result = run_test(["--test-diffusion-stereo"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_extreme_input_does_not_poison_the_feedback_path(self):
        """Also checks ordinary audio afterwards still works, which is what an
        infinity trapped in a recirculating network destroys permanently."""
        result = run_test(["--test-diffusion-bad-input"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_a_sample_rate_change_does_not_reallocate(self):
        """Storage is sized once for the highest supported rate, so moving
        between rates never needs more."""
        result = run_test(["--test-diffusion-no-alloc"])
        assert result["failed"] == 0, result.get("detail", "")


class TestExtremeInput:
    """Hostile but legal input, across every module at once."""

    def test_every_entry_point_survives_extreme_input(self):
        """Written after review found the large-finite gap in Quantizer.

        The problem was not a missing guard so much as that nothing in the suite
        ever supplied such a value, so every sanitiser run came back clean. This
        sweep covers the class rather than the one instance, and it immediately
        found a second: Transport.setLoopBounds clamped with std::min and
        std::max, which PROPAGATE NaN because every comparison against NaN is
        false, so a NaN length survived both calls and playheadFrames() returned
        NaN for the rest of the patch.

        Most of its value comes from running under AddressSanitizer and
        UndefinedBehaviorSanitizer, where the failure is the sanitiser aborting
        rather than an assertion here.
        """
        result = run_test(["--test-extreme-sweep"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_concurrent_submit_and_acquire(self):
        """A reader standing in for the audio thread hammers acquire/release
        while jobs are submitted, checking for races and for frees landing on
        the wrong thread."""
        result = run_test(["--test-worker-hammer"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_shutdown_is_prompt_and_complete(self):
        result = run_test(["--test-worker-shutdown"])
        assert result["failed"] == 0, result.get("detail", "")


def discover_test_classes() -> list:
    """Every Test* class in this module, in definition order.

    Discovered rather than listed. The list used to be hand-written and stopped
    at TestSeparationWorker, so seven classes added afterwards were never
    instantiated by the native runner that CI actually invokes. Every check in
    them could have failed while the workflow stayed green.

    This is the same drift that made stems_test declare its own commands instead
    of having them scraped, and it went the same way: a list maintained by hand
    beside the thing it describes falls behind it.
    """
    # vars() preserves definition order, so no explicit sort is needed.
    module = sys.modules[__name__]
    return [obj for name, obj in vars(module).items()
            if name.startswith("Test") and isinstance(obj, type)]


def run_all_tests() -> bool:
    passed = failed = 0
    errors = []
    classes = discover_test_classes()
    if not classes:
        print("No test classes discovered; the runner is broken.")
        return False
    print(f"Discovered {len(classes)} test classes\n")
    for cls in classes:
        instance = cls()
        for name in dir(instance):
            if not name.startswith("test_"):
                continue
            try:
                getattr(instance, name)()
                passed += 1
                print(f"  PASS: {name}")
            except Exception as exc:  # noqa: BLE001 - report and continue
                failed += 1
                errors.append((f"{cls.__name__}.{name}", str(exc)))
                print(f"  FAIL: {name}")

    total = passed + failed
    print(f"\n{'='*60}")
    print(f"Results: {passed}/{total} passed, {failed} failed")
    print("=" * 60)
    if errors:
        print("\nFailures:")
        for name, error in errors:
            print(f"\n  {name}:\n    {error[:300]}")
    return failed == 0


if __name__ == "__main__":
    print("Stems Core Test Suite")
    print("=" * 60)
    exe = get_test_executable()
    if not exe.exists():
        print(f"\nERROR: stems_test not found at: {exe}")
        print("Run 'just build' first to compile the test executable.")
        sys.exit(1)
    print(f"Using test executable: {exe}\n")
    sys.exit(0 if run_all_tests() else 1)
