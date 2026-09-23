# ACID9 Voice

ACID9 Voice is a monophonic acid synth with a stereo oscillator, two filter characters, accent, slide, an insert loop and stereo delay. It occupies **34HP**. This is the only module in WiggleRoom 2.1.3; Matter and the other experimental modules are deferred.

![ACID9 Voice panel](../../../design/ACID9Voice/preview.png)

## First sound

1. Connect a sequencer's pitch output to **V/OCT** and gate to **GATE**.
2. Connect **LEFT** and **RIGHT** to an audio output module. Start at a low monitoring level.
3. Load **Classic acid** from the module's Factory presets menu, or use the default settings.
4. Turn **CUTOFF**, **RESONANCE**, **ENV MOD** and **DECAY** while playing notes.
5. Add **GRIT** for saturation, **ISOTOPE** for stereo spread, and **DELAY MIX** for echoes.

The included [MIDI example patch](../../../examples/ACID9-first-sound.vcv) uses only ACID9 Voice and Rack's built-in MIDI-CV and Audio-2 modules. Select your MIDI input and audio output devices in those modules. No audio hardware settings are embedded in the patch.

## Controls

| Section | Control | Behaviour |
|---|---|---|
| Oscillator | Slide time | 10–200ms to reach 90% of the next pitch; default 60ms. |
| Oscillator | Accent amount | 0–100%; default 50%. Zero disables accent effects, including shortened filter decay. |
| Oscillator | Shape | Morphs the oscillator waveform. |
| Oscillator | Isotope | Adds detune and stereo spread to the three oscillator cores. |
| Oscillator | Sub level / Sub mode | Adds an octave-down oscillator; switch between sine and square. |
| Filter | Cutoff | Base cutoff, 20Hz–20kHz. |
| Filter | Resonance | Emphasises the cutoff region. |
| Filter | Env mod | Bipolar filter envelope amount; negative values close the filter. |
| Filter | Decay | Filter envelope decay, 10ms–2s. |
| Filter | Grit | Saturation before the filter. |
| Filter | Filter morph | Continuous ACID → LEAD blend. ACID is a coupled diode ladder with partial level compensation; LEAD is a four-pole TPT ladder with progressive resonance below self-oscillation. The light brightens toward LEAD. |
| Delay | Delay time | Manual left delay, 10–2000ms. The right delay is 5% longer. |
| Delay | Feedback | Repeat amount, internally limited to 0.95. |
| Delay | Delay mix | Dry/wet balance. Zero is completely dry. |
| Delay | Ghost | Raises the delay high-pass cutoff from 80Hz to 480Hz, making repeats thinner. |
| Delay | Sync ratio | Clock period × ¼, ½, 1, 2 or 4, constrained to the delay time range. |

## Inputs and outputs

Only the first channel of polyphonic inputs is used.

| Jack | Signal |
|---|---|
| V/OCT | Pitch, 1V/octave; 0V is approximately C4. Accepted range −5V to +10V, with oscillator frequency limited internally. |
| Gate | Note gate, 1V rising / 0.1V falling thresholds. Fresh rising edges identify notes, including repeated pitches. |
| Accent | Latched per note; raises loudness and drives a resonance-dependent filter sweep. A pulse later in the note also activates accent. |
| Slide | A pulse during a note arms a tied glide into the next note. Held high arms consecutive slides. |
| Cutoff CV | Adds exponential cutoff modulation, 1V/octave, clamped to ±5V. |
| FM in | Pitch modulation: 1V changes pitch by 0.1 octave, clamped to ±5V. |
| Shape CV / Isotope CV | Added to the knob setting; +10V spans the full control range. |
| Decay CV | Added to the knob setting; +5V adds 1s. The result is clamped to 10ms–2s. |
| Clock | Rising edges above 0.5V. Two edges establish tempo; periods of 100 samples or less are ignored. Sync expires after 2s without an edge or when unplugged. |
| Send | Mono voice after filter and VCA, before delay, DC blocked and softly limited toward ±5V. |
| Insert return | Mono return replaces the internal stereo voice before the delay. Unpatched, the internal voice passes through. |
| Left / Right | Stereo audio, softly limited toward ±5V. |

To insert an effect, connect **SEND → effect input → effect output → INSERT RETURN**. Connecting a silent return mutes the dry voice, while Send remains active. Delay repeats can continue after a note ends.

## Timing and patch behaviour

A clock overrides the Delay time knob only after two valid edges. Unplugging or stopping the clock returns to manual time. Changing the sample rate or resetting the module clears the delay and sync state. Patch save/load restores controls; active delay tails are not saved.

The filter envelope has a 1ms attack and exponential decay set by DECAY. Accented notes use a fixed 60ms decay. The VCA uses a 3ms attack, 1.2s decay and 3ms release; there is no ADSR sustain plateau. All attacks start from the current level, with short smoothing to suppress clicks. These timings describe the model, not a claim of exact hardware reproduction.

SLIDE is programmed on the source note. Its pitch stays unchanged until the destination starts, then glides toward the new pitch while the gate and envelopes remain tied. If GATE falls while slide is armed, the voice bridges up to 100ms waiting for the next gate edge, then releases and cancels the slide. MIDI/sequencer rests longer than this are not tied.

Under a held GATE, a V/OCT change exceeding one cent and settled within one cent for 1ms identifies a new note. Octave and FM changes do not generate notes. Continuous CV movement is treated as pitch modulation until it settles; for precise articulation and repeated identical notes, send a fresh gate edge per note. Use normal note-length gates rather than 1ms triggers when you want sustained notes.

ACCENT is remembered even if its input pulse ends. Its sweep retains charge between notes; repeated accents build a stronger sweep, with the curve controlled by RESONANCE. Tied destinations update accent state without retriggering the main envelopes. Both ACID and LEAD use this articulation.

Loading, resetting, bypassing and changing sample rate clear runtime articulation. This update changes the sound of existing patches; new SLIDE TIME and ACCENT AMT controls load at their defaults, while old parameter and cable IDs remain unchanged.

## Compatibility

WiggleRoom 2.1.3 retains the `WiggleRoom/ACID9Voice` slug and existing parameter/port IDs. The panel has grown from 20HP to 34HP; leave space or rearrange neighbours when opening older development patches. Patches using deferred WiggleRoom modules require the development build that supplied them.

## Source and licence

GPL-3.0-or-later. DSP source and the generated header are included. The cosmic artwork uses AI-generated illustration and branding with separately drawn functional labels; it is not affiliated with the Grateful Dead.

### Octave transpose

OCTAVE shifts the voice from −3 to +3 octaves in whole-octave steps; the default is 0. It adds to V/OCT before slide, preserving the sub oscillator’s relative tuning. The combined pitch remains limited to the supported −5 to +10 V range. Existing patches default to zero transpose.

### Filter morph

FILTER MORPH replaces the two-position switch: fully left is Acid, fully right is Lead. Both filters run continuously and the blend is smoothed over 10ms. A linear crossfade avoids the correlated midpoint gain boost. Acid has fixed, resonance-dependent gain compensation; Lead now uses a coupled four-pole ladder instead of two resonant biquads, with a gentler resonance curve. Levels remain tone-dependent, especially near cutoff. Existing endpoint settings retain parameter ID 8 and values 0/1; the filter sound changes deliberately.
