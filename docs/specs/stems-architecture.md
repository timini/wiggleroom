# Stems: architecture, library evaluation, and fact-check

Background research for `specs/Stems.md`. Part 1 fact-checks the originating concept document. Part 2 covers the constraints that decide the architecture. Part 3 is the library and licensing evaluation.

---

## Part 1: Fact-check of the concept document

The originating document is largely accurate on library names and model rankings. Its errors are concentrated in feasibility, deployment and licensing, which unfortunately is where they matter most.

### Verified correct

| Claim | Verdict |
|-------|---------|
| BS-RoFormer is state of the art for music source separation | **Correct.** Won the Music Source Separation track of the Sound Demixing Challenge 2023. Achieves 9.80 dB average SDR on MUSDB18-HQ without extra training data, 11.99 dB when trained with 500 additional songs |
| HTDemucs is a hybrid time/frequency-domain model from Meta AI | **Correct.** `facebookresearch/demucs`, operates on both waveform and spectrogram |
| BTrack is a causal real-time C++ beat tracker | **Correct.** Adam Stark, designed for real-time use, C++ with Python and Vamp wrappers |
| Essentia is from MTG UPF and includes `RhythmExtractor2013` | **Correct** |
| Rubber Band is the industry standard time-stretcher | **Correct**, with a major licensing caveat below |
| Signalsmith Stretch is lightweight and real-time capable | **Correct** |
| Clouds' DSP source is available and adaptable | **Correct.** MIT licensed |
| ONNX Runtime C++ is the route to embedding these models | **Correct** |
| **VCV Rack Pro can host VST3 plugins** | **Correct.** VCV Host supports 64-bit VST2 and VST3. Rack Pro also ships as VST2/VST3/AU/CLAP itself. I doubted this initially and was wrong |

### Wrong or seriously misleading

**1. "Real-time or near-real-time AI stem separation model."** The document says this in section 2.2 and then contradicts itself in the follow-up, correctly describing separation as "an asynchronous background task". The first framing is wrong and matters, because it implies you can separate a live input stream.

HTDemucs and BS-RoFormer are **offline, non-causal** models: they need the whole signal, including future samples, to produce output. Real-time causal separation is a separate and much younger research area with materially lower quality, currently represented by work such as Hybrid Spectrogram-TasNet and RT-STT.

The spec resolves this by defining separation as a discrete post-record job with an explicit playable state while it runs.

**2. Model size is never mentioned, and it is the single biggest constraint.** HTDemucs exported to ONNX is roughly **316 MB per model**, and a full four-stem configuration is around **1.26 GB**. The `htdemucs_ft` variant runs four separately fine-tuned passes, so it is roughly four times slower again.

This is not a detail. It decides distribution:
- VCV plugins are cross-compiled by VCV's own `rack-plugin-toolchain` and distributed through the Library. Shipping over a gigabyte of model weights through that pipeline is not realistic.
- Hence the spec's rule: **no bundled model weights**, with Tier 1 pointing at a user-supplied model directory.

**3. Performance figures are cherry-picked from high-end Apple Silicon.** "A 7-minute track in 12 seconds" and "real-time factor 0.20" are M4 Max and M4 Pro figures. A mid-range Windows laptop without a usable NPU path will be far slower. Any UX that assumes separation completes in a couple of seconds will feel broken for a large share of users. The spec therefore requires the module to stay playable throughout, rather than treating separation as a brief pause.

**4. The recommended stack is almost entirely copyleft, while the recommended product is a commercial plugin.** The document does not mention licensing once. This is the most consequential omission:

| Library | Licence | Consequence |
|---------|---------|-------------|
| Rubber Band | **GPL-2.0-or-later**, or paid commercial licence | Using it forces GPL on the whole product unless you buy a licence |
| Essentia | **AGPL-3.0** | Strongest copyleft here. Viral, with network-use obligations |
| aubio | **GPL** | Same forcing effect |
| BTrack | **GPL-3.0** | Same |
| Signalsmith Stretch | **MIT** | Permissive |
| Clouds DSP | **MIT** | Permissive |
| ONNX Runtime | MIT | Permissive |

Recommending Rubber Band, Essentia, aubio and BTrack for a JUCE VST3 product, without noting that all four force GPL/AGPL, is the document's most serious error. It is fine for this repository, which is already GPL-3.0-or-later, but it forecloses the commercial route the document otherwise steers toward.

Where a permissive option exists at comparable quality, the spec prefers it: **Signalsmith Stretch over Rubber Band**.

**5. Scale detection on an arbitrary stem is not meaningful.** The concept has the pitch analyser read "the selected stem", and the stem set includes Drums. Fundamental estimation on a drum layer returns noise, and a quantiser fed by it emits effectively random scales. The spec adds a confidence measure with hold-last-good behaviour and a UI indication.

**6. Wavetable extraction versus time-stretching is unspecified, and the ordering matters.** The concept time-stretches stems to lock to the clock, and separately extracts a wavetable "around the currently playing area". If extraction happens after stretching, stretch artefacts are baked into the wavetable, where they are far more audible than in the loop because the frame is played cyclically at audio rate and any artefact becomes a stable timbral feature. The spec requires extraction from the unstretched stem.

### Missing from the concept entirely

Each of these is now specified:

- No `RESET` input, which a beat-synced module cannot do without
- No threading model or real-time-safety rules
- No behaviour defined while separation is running, or if it fails
- No patch-persistence policy. Naively saving four stereo stems into a patch produces files well over 100 MB
- No sample-rate-change handling
- No CPU or memory budget, and no bound on grain concurrency
- No buffer length limit
- Only a single main output. Separate loop and voice outputs make the module far more useful and each stage independently testable
- No behaviour defined for an empty buffer, which is the state the module is in every time it loads

---

## Part 2: Constraints that decide the architecture

### Why the module is native C++, not Faust

Faust is excellent for the repo's DSP modules but cannot express this one: multi-second buffers, a worker thread, dynamic wavetable construction, and a grain scheduler with per-grain state. `FaustModule` is additionally single-instance and monophonic.

### Threading

Rack guarantees it never calls `Module` methods concurrently, but that guarantee does not extend to threads the plugin creates. All cross-thread state moves through lock-free SPSC queues or atomic pointer swaps. Nothing on the audio thread allocates, locks, logs or touches the filesystem.

The separation worker produces four complete stem buffers and publishes them with a single atomic pointer swap, so the audio thread either sees the old set or the new set and never a partial one.

### Why tiered separation

Tier 0, harmonic/percussive separation by median filtering (Fitzgerald, DAFx-10, extended by Driedger, Müller and Disch in 2014), is cheap, deterministic, dependency-free and well documented. It is implemented in librosa and FluCoMa among others, so the algorithm is easy to validate against a reference implementation.

The insight that makes Tier 0 sufficient for an MVP: this instrument needs **material that differs from itself**, not correctly labelled instruments. "Percussive versus harmonic versus low versus residual" gives four musically distinct layers to mix, analyse and turn into wavetables. Labelling them "drums/bass/vocals/other" is a nicety for this application, not a requirement.

That reframing removes the ML deployment problem from the critical path entirely. Tier 1 becomes an enhancement that can be added, deferred or dropped without redesigning anything, because both tiers publish the same four-buffer interface.

### Platform strategy

The user's requirement was to avoid being tied to one platform. The way to honour that is a **platform-agnostic C++ core** with thin adapters, not a choice between VCV and JUCE:

```
        core/            no framework dependencies, unit-testable
        ├── FftBackend interface (implementation supplied by the adapter)
        ├── buffer, sync, separation (tier 0 + tier 1 interface)
        ├── pitch analysis + quantiser
        ├── wavetable extractor + oscillator
        ├── lowpass gate
        └── grain engine + diffuser
             │
    ┌────────┼────────┬──────────────┐
    │        │        │              │
  VCV      JUCE     stems_test    (future)
  adapter  adapter  adapter
  RealFFT  juce FFT reference DFT
```

The concept document's advice to start with JUCE is defensible but not obligatory. Since this repository is already a working GPL-3 VCV plugin with build, test and CI infrastructure in place, starting with the VCV adapter at Tier 0 gets a playable instrument soonest and defers every hard deployment question. Nothing about that choice blocks a JUCE adapter later, provided the core stays framework-free.

---

## Part 3: Library evaluation

Recommended set for a GPL-3.0-or-later VCV module:

| Function | Choice | Licence | Rationale |
|----------|--------|---------|-----------|
| Time-stretch | Signalsmith Stretch | MIT | Permissive, real-time, small. Keeps a non-GPL future open. Rubber Band is better on complex polyphonic material but is GPL-or-commercial |
| Separation, Tier 0 | Own implementation, median-filter HPSS | n/a | Roughly 200 lines over an FFT. No dependency |
| Separation, Tier 1 | ONNX Runtime + user-supplied model | MIT runtime | Model weights not bundled |
| Pitch detection | Own YIN implementation | n/a | aubio is GPL and heavier than needed. YIN is a well-documented, self-contained algorithm |
| Beat detection | Not required for v1 | n/a | Tempo and phase come from `clock_in`. Only needed to align an off-grid recording, which can be deferred |
| Granular | Adapted from Clouds | MIT | Émilie Gillet's DSP is MIT and directly usable with attribution |
| Lowpass gate | Shared with `specs/TheLantern.md` | n/a | Do not implement twice. That spec is in PR #66 and not yet on main; if it does not land, this module needs its own |
| FFT | Adapter-supplied behind a core interface; Rack adapter uses `dsp::RealFFT` | FFTPACK (BSD-style) | See the note below. The core must not include Rack headers |

Note the pattern: for every function where the obvious library is GPL or AGPL, either a permissive alternative exists at comparable quality, or the algorithm is small enough to implement directly. Taking that route keeps the core free of copyleft dependencies, which costs little now and preserves optionality later.

### The FFT must not tie the core to Rack

Using `rack::dsp::RealFFT` directly inside `src/common/stems/` would contradict the platform strategy above: the core is meant to have no framework dependencies so a JUCE adapter is possible, and including `dsp/fft.hpp` would make the core include and link the Rack SDK.

The core therefore declares a minimal interface and each adapter supplies the implementation:

```cpp
// src/common/stems/FftBackend.hpp  — no rack.hpp
struct FftBackend {
    virtual ~FftBackend() = default;
    virtual void forward(const float* in, float* out) = 0;   // real -> packed complex
    virtual void inverse(const float* in, float* out) = 0;
    virtual size_t size() const = 0;
};
```

The Rack adapter wraps `dsp::RealFFT`. `stems_test` supplies its own implementation, which conveniently also removes the open question about linking pffft into a standalone test executable: the unit tests can use a small self-contained DFT as the reference and never link the SDK at all. A future JUCE adapter supplies `juce::dsp::FFT`.

Cost is one virtual call per frame, which is negligible against a 2048-point transform.

### Deliberately rejected

- **Essentia** (AGPL-3.0): far more library than needed, strongest copyleft obligations in the candidate set
- **aubio** (GPL): only YIN is required, and YIN is short
- **Rubber Band** (GPL/commercial): only if Signalsmith proves inadequate on real material, and only with the licensing consequence understood
- **BTrack** (GPL-3.0): beat tracking is not needed for v1

---

## Part 4: Suggested phasing

1. **Buffer, clock sync, repitch playback.** Record, loop, stay in time. No separation. Immediately useful and independently testable.
2. **Tier 0 separation and the stem mixer.** Four layers, four faders. The instrument becomes distinctive here.
3. **Pitch analysis and quantiser**, including the confidence and hold-last-good behaviour.
4. **Wavetable extractor, oscillator and LPG.** The synth voice.
5. **Granular stage.**
6. **Optional: time-stretch sync mode, Tier 1 neural separation, JUCE adapter.**

Steps 1 to 5 have no external dependencies beyond Signalsmith Stretch (and not even that until step 6, since step 1 uses repitch). Every hard deployment question lives in step 6.
