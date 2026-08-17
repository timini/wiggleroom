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

    def test_concurrent_submit_and_acquire(self):
        """A reader standing in for the audio thread hammers acquire/release
        while jobs are submitted, checking for races and for frees landing on
        the wrong thread."""
        result = run_test(["--test-worker-hammer"])
        assert result["failed"] == 0, result.get("detail", "")

    def test_shutdown_is_prompt_and_complete(self):
        result = run_test(["--test-worker-shutdown"])
        assert result["failed"] == 0, result.get("detail", "")


def run_all_tests() -> bool:
    passed = failed = 0
    errors = []
    for cls in (TestFftBackend, TestRingBuffer, TestTransport, TestStft, TestHpss,
                TestSeparationWorker):
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
