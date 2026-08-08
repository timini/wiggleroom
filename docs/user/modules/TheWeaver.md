# TheWeaver

## Overview

TheWeaver is a WiggleRoom module in the **Sequencers & Clocks** category. A Scale-Bus-aware polyphonic arpeggiator with eleven patterns, clock division and multiplication, latch and hold, and an addressable Index CV.

Feed it a polyphonic chord and it walks through the held notes in the pattern you choose, staying in key when a Scale Bus is connected.

## Signal Flow

- TheWeaver processes pitch CV and gates, not audio.
- The Notes input takes a polyphonic cable carrying the chord to arpeggiate.
- Clock drives the step advance, Reset returns to the start of the pattern.
- Scale Bus, when patched, constrains output pitch to the shared scale.
- Pattern, Div and Index each have a CV input alongside their knob.
- Outputs are CV for pitch, Gate for the note, and EOC which fires at the end of each pass through the pattern.

## Controls

| Control | Description |
|---------|-------------|
| Pattern | Selects one of eleven traversal patterns across the held notes |
| Octave Spread | Extends the arpeggio across additional octaves |
| Clock Div | Divides or multiplies the incoming clock |
| Gate Length | Sets how much of each step the gate stays high for |
| Sync Mode | Chooses how pattern position relates to the incoming clock |
| Hold | Latches the current chord so it keeps arpeggiating after the notes are released |
| Index | Addresses a specific position in the pattern directly |

## Typical Uses

- Turn a held chord from a quantizer or MIDI source into a moving line without programming a sequence.
- Patch Index CV from an LFO or sequencer to scrub through the arpeggio non-linearly rather than stepping in order.
- Use EOC to advance a second sequencer once per pass, so a chord progression moves in step with the arpeggio.

## Tips

- Hold is what makes this playable live. Latch a chord, then change Pattern and Octave Spread over the top.
- Index CV turns the module from an arpeggiator into an addressable note bank, which is a very different instrument.
- Connect the Scale Bus from [TheArchitect](TheArchitect.md) so pattern and octave changes stay in key.
