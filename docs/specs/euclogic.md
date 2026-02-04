# Euclogic - 4-Channel Euclidean Drum Sequencer with Logic

A generative drum sequencer that combines Euclidean rhythm algorithms with combinatorial truth table logic for complex polyrhythmic patterns.

## Signal Path Diagram

```
                           ┌─────────────────────────────────────────────────────────────────┐
                           │                          EUCLOGIC                               │
                           │                                                                 │
 ┌─────────┐               │  ┌─────────────────┐                                           │
 │  CLOCK  │───────────────┼─▶│  Master Clock   │                                           │
 └─────────┘               │  │  Div/Mult (/16  │                                           │
                           │  │   to x16)       │                                           │
 ┌─────────┐               │  └────────┬────────┘                                           │
 │  RESET  │───────────────┼───────────┼──────────────────────────────────────────────────┐ │
 └─────────┘               │           │                                                  │ │
                           │           ▼                                                  │ │
 ┌─────────┐               │  ┌────────────────────────────────────────────────────────┐  │ │
 │   RUN   │───────────────┼─▶│                  QUANT DIVIDERS                        │  │ │
 └─────────┘               │  │   ×4 independent ratio dividers (1:1 to 1:64)          │  │ │
                           │  └────────┬───────────┬───────────┬───────────┬───────────┘  │ │
                           │           │ CH1       │ CH2       │ CH3       │ CH4         │ │
                           │           ▼           ▼           ▼           ▼             │ │
                           │  ┌────────────────────────────────────────────────────────┐  │ │
                           │  │               EUCLIDEAN ENGINES (×4)                   │  │ │
 ┌─────────┐               │  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │  │ │
 │ HITS CV │───────────────┼──┼─▶│ Engine 1 │ │ Engine 2 │ │ Engine 3 │ │ Engine 4 │  │  │ │
 │  (×4)   │               │  │  │Steps:1-64│ │Steps:1-64│ │Steps:1-64│ │Steps:1-64│  │  │ │
 └─────────┘               │  │  │Hits:0-N  │ │Hits:0-N  │ │Hits:0-N  │ │Hits:0-N  │  │  │ │
                           │  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘  │  │ │
                           │  └───────┼────────────┼────────────┼────────────┼────────┘  │ │
                           │          │            │            │            │           │ │
                           │          ▼            ▼            ▼            ▼           │ │
                           │  ┌────────────────────────────────────────────────────────┐  │ │
                           │  │              PROBABILITY A STAGE (×4)                  │  │ │
 ┌─────────┐               │  │  Each hit passes through probability gate (0-100%)     │  │ │
 │PROB A CV│───────────────┼─▶│                                                        │  │ │
 │  (×4)   │               │  │    P(A₁)          P(A₂)          P(A₃)          P(A₄)  │  │ │
 └─────────┘               │  └────────┬───────────┬───────────┬───────────┬───────────┘  │ │
                           │           │           │           │           │             │ │
                           │           ▼           ▼           ▼           ▼             │ │
                           │  ┌────────────────────────────────────────────────────────┐  │ │
                           │  │                   TRUTH TABLE                          │  │ │
                           │  │                                                        │  │ │
                           │  │     4-bit input ───▶ ┌─────────────┐ ───▶ 4-bit output │  │ │
 ┌─────────┐               │  │                      │  16-state   │                   │  │ │
 │ RANDOM  │───────────────┼─▶│                      │   logic     │                   │  │ │
 └─────────┘               │  │                      │   matrix    │                   │  │ │
 ┌─────────┐               │  │                      └─────────────┘                   │  │ │
 │ MUTATE  │───────────────┼─▶│         ┌───────────────────────────────┐              │  │ │
 └─────────┘               │  │         │     4×4 LED Matrix Display    │              │  │ │
 ┌─────────┐               │  │         │     (shows active state)      │              │  │ │
 │  UNDO   │───────────────┼─▶│         └───────────────────────────────┘              │  │ │
 └─────────┘               │  └────────┬───────────┬───────────┬───────────┬───────────┘  │ │
                           │           │           │           │           │             │ │
                           │           ▼           ▼           ▼           ▼             │ │
                           │  ┌────────────────────────────────────────────────────────┐  │ │
                           │  │              PROBABILITY B STAGE (×4)                  │  │ │
 ┌─────────┐               │  │  Post-logic probability (0-100%)                       │  │ │
 │PROB B CV│───────────────┼─▶│                                                        │  │ │
 │  (×4)   │               │  │    P(B₁)          P(B₂)          P(B₃)          P(B₄)  │  │ │
 └─────────┘               │  └────────┬───────────┬───────────┬───────────┬───────────┘  │ │
                           │           │           │           │           │             │ │
                           │           │           │           │           │             │ │
                           │           ▼           ▼           ▼           ▼             │ │
                           │  ┌────────────────────────────────────────────────────────┐  │ │
                           │  │                    OUTPUTS                             │  │ │
                           │  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │◀─┘ │
                           │  │  │  GATE 1  │ │  GATE 2  │ │  GATE 3  │ │  GATE 4  │  │    │
                           │  │  │  TRIG 1  │ │  TRIG 2  │ │  TRIG 3  │ │  TRIG 4  │  │    │
                           │  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │    │
                           │  └────────────────────────────────────────────────────────┘    │
                           └───────────────────────────────────────────────────────────────┘

LEGEND:
  ───▶  Signal flow
  ×4    Four parallel channels
  P()   Probability gate
```

## Module Overview

**Width:** 20 HP

Euclogic is a 4-channel generative rhythm sequencer that creates complex polyrhythms by combining:

1. **Euclidean Rhythms** - Mathematically optimal distribution of hits across steps
2. **Pre-Logic Probability** - Random hit suppression before logic processing
3. **Truth Table Logic** - 16-state combinatorial logic mixing all 4 channels
4. **Post-Logic Probability** - Final random filtering for humanization

## Panel Layout

```
┌─────────────────────────────────────┐
│              EUCLOGIC               │
│                                     │
│  [CLOCK]  [RESET]  [RUN]           │
│    ○        ○       ○              │
│                                     │
│      MASTER SPEED: /16...x16       │
│         ◉──────────────◉           │
│                                     │
│  ┌─────────────────────────────┐   │
│  │  CH1    CH2    CH3    CH4   │   │
│  │ ╔════╗ ╔════╗ ╔════╗ ╔════╗│   │
│  │ ║STEP║ ║STEP║ ║STEP║ ║STEP║│   │
│  │ ╚════╝ ╚════╝ ╚════╝ ╚════╝│   │
│  │ ╔════╗ ╔════╗ ╔════╗ ╔════╗│   │
│  │ ║HITS║ ║HITS║ ║HITS║ ║HITS║│   │
│  │ ╚════╝ ╚════╝ ╚════╝ ╚════╝│   │
│  │   ○      ○      ○      ○   │   │
│  │ HITS   HITS   HITS   HITS  │   │
│  │  CV     CV     CV     CV   │   │
│  │                             │   │
│  │ QUANT  QUANT  QUANT  QUANT │   │
│  │ ◉───◉ ◉───◉ ◉───◉ ◉───◉  │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │         PROB A (×4)         │   │
│  │ ╔════╗ ╔════╗ ╔════╗ ╔════╗│   │
│  │ ╚════╝ ╚════╝ ╚════╝ ╚════╝│   │
│  │   ○      ○      ○      ○   │   │
│  │  CV     CV     CV     CV   │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │       TRUTH TABLE           │   │
│  │    ┌───┬───┬───┬───┐       │   │
│  │    │ ● │ ○ │ ○ │ ● │       │   │
│  │    ├───┼───┼───┼───┤       │   │
│  │    │ ○ │ ● │ ○ │ ○ │       │   │
│  │    ├───┼───┼───┼───┤       │   │
│  │    │ ● │ ● │ ○ │ ○ │       │   │
│  │    ├───┼───┼───┼───┤       │   │
│  │    │ ○ │ ○ │ ● │ ● │       │   │
│  │    └───┴───┴───┴───┘       │   │
│  │                             │   │
│  │  [RND]   [MUT]   [UNDO]    │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │         PROB B (×4)         │   │
│  │ ╔════╗ ╔════╗ ╔════╗ ╔════╗│   │
│  │ ╚════╝ ╚════╝ ╚════╝ ╚════╝│   │
│  │   ○      ○      ○      ○   │   │
│  │  CV     CV     CV     CV   │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │          OUTPUTS            │   │
│  │                             │   │
│  │  GATE   GATE   GATE   GATE │   │
│  │   ●      ●      ●      ●   │   │
│  │                             │   │
│  │  TRIG   TRIG   TRIG   TRIG │   │
│  │   ○      ○      ○      ○   │   │
│  └─────────────────────────────┘   │
│                                     │
│            WIGGLE ROOM              │
└─────────────────────────────────────┘

LEGEND:
  ○  Jack (input)
  ●  Jack (output) or LED
  ╔═╗ Knob
  ╚═╝
  ◉─◉ Switch/selector
```

## Euclidean Rhythm Algorithm

Euclogic uses **Bjorklund's algorithm** to generate Euclidean rhythms - mathematically optimal distributions of K hits over N steps.

### How It Works

The algorithm distributes hits as evenly as possible:

```
E(3, 8) = [1,0,0,1,0,0,1,0]  "Tresillo" - Cuban rhythm
E(5, 8) = [1,0,1,1,0,1,1,0]  "Cinquillo" - Another Cuban pattern
E(4,12) = [1,0,0,1,0,0,1,0,0,1,0,0]  Standard 12/8 pattern
E(2, 4) = [1,0,1,0]  Simple alternating
```

### Musical Examples

| Pattern | Style | Feel |
|---------|-------|------|
| E(3,8) | Tresillo | Latin, Reggae |
| E(5,8) | Cinquillo | Son Clave |
| E(7,12) | West African | Complex polyrhythm |
| E(5,16) | | Syncopated |

## Truth Table Logic

The truth table is the heart of Euclogic's generative power. It maps all 16 possible input states (4 channels × 2 states each) to any output configuration.

### Input/Output Mapping

```
INPUT STATE      OUTPUT
[A B C D]     → [1 2 3 4]
─────────────────────────
[0 0 0 0] 0  → [? ? ? ?]
[1 0 0 0] 1  → [? ? ? ?]
[0 1 0 0] 2  → [? ? ? ?]
[1 1 0 0] 3  → [? ? ? ?]
[0 0 1 0] 4  → [? ? ? ?]
[1 0 1 0] 5  → [? ? ? ?]
[0 1 1 0] 6  → [? ? ? ?]
[1 1 1 0] 7  → [? ? ? ?]
[0 0 0 1] 8  → [? ? ? ?]
[1 0 0 1] 9  → [? ? ? ?]
[0 1 0 1] 10 → [? ? ? ?]
[1 1 0 1] 11 → [? ? ? ?]
[0 0 1 1] 12 → [? ? ? ?]
[1 0 1 1] 13 → [? ? ? ?]
[0 1 1 1] 14 → [? ? ? ?]
[1 1 1 1] 15 → [? ? ? ?]
```

### Preset Logic Modes

| Preset | Description | Use Case |
|--------|-------------|----------|
| **PASS** | Input = Output | No mixing, direct through |
| **OR** | Any input → all outputs | Merge all rhythms |
| **AND** | All inputs → all outputs | Only when all coincide |
| **XOR** | Odd inputs → all outputs | Phase-based triggering |
| **MAJORITY** | 2+ inputs → all outputs | Voting logic |

### Random and Mutate

- **RANDOM**: Creates entirely new random truth table mapping
- **MUTATE**: Flips 1-3 random bits (subtle evolution)
- **UNDO**: Restores previous state (unlimited history)

## Controls Reference

### Global Controls

| Control | Range | Description |
|---------|-------|-------------|
| Clock In | 0-10V | External clock input |
| Reset In | Trigger | Reset all sequences to step 1 |
| Run In | Gate | Enable/disable sequencing |
| Master Speed | /16 to x16 | Global clock multiplier/divider |

### Per-Channel Controls (×4)

| Control | Range | Default | Description |
|---------|-------|---------|-------------|
| Steps | 1-64 | 16 | Pattern length |
| Hits | 0-Steps | 8 | Number of hits distributed |
| Hits CV | ±5V | - | Modulates hits (±12 range) |
| Quant | 1:1 to 1:64 | 1:1 | Clock division ratio |
| Prob A | 0-100% | 100% | Pre-logic probability |
| Prob A CV | ±5V | - | Probability modulation |
| Prob B | 0-100% | 100% | Post-logic probability |
| Prob B CV | ±5V | - | Probability modulation |

### Logic Controls

| Control | Type | Description |
|---------|------|-------------|
| RANDOM | Button | Randomize entire truth table |
| MUTATE | Button | Slightly modify truth table |
| UNDO | Button | Restore previous state |

### Outputs (×4)

| Output | Type | Description |
|--------|------|-------------|
| GATE | 0-10V | High while hit is active |
| TRIG | 0-10V | 1ms pulse on hit |

## Usage Tips

### Basic Drum Pattern

1. Connect a clock source to CLOCK input
2. Set CH1 to E(4,16) for kick (Steps=16, Hits=4)
3. Set CH2 to E(8,16) for hi-hat
4. Set CH3 to E(3,8) for snare (tresillo feel)
5. Leave truth table in PASS mode

### Generative Polyrhythm

1. Set different step lengths: 16, 12, 7, 5
2. Use RANDOM to create unexpected combinations
3. Reduce Prob B to 70% for humanization
4. Use MUTATE to evolve the pattern over time

### Logic-Based Fills

1. Start with sparse Euclidean patterns
2. Set truth table to OR mode - creates fills when patterns align
3. Or use AND mode - creates accents only on strong beats

### CV Modulation

1. Route an LFO to Hits CV for evolving density
2. Route envelope to Prob A for dynamic muting
3. Use slow random to Prob B for unpredictable drops

## Technical Specifications

- Sample rate: Follows VCV Rack (default 48kHz)
- Trigger width: 1ms
- Gate output: 10V when active
- Clock detection: Rising edge (Schmitt trigger)
- Reset: Immediate return to step 0
- CPU: Low (no audio processing)

## Files

```
src/modules/Euclogic/
├── Euclogic.cpp          # Main module implementation
├── EuclideanEngine.hpp   # Bjorklund algorithm
├── TruthTable.hpp        # Logic engine with undo
├── ProbabilityGate.hpp   # Seeded RNG gate
├── CMakeLists.txt        # Build configuration
└── test_config.json      # Test configuration

test/
├── euclogic_test.cpp     # C++ unit tests
└── test_euclogic.py      # Python test harness
```
