# GravityClock

Clock-synced bouncing-ball trigger generator.

## Overview

This page provides a quick operational reference for **GravityClock**. Use it alongside the panel labels in Rack for exact control names and ranges.

## What it does

- Designed for the WiggleRoom workflow and modulation ranges.
- Accepts standard VCV Rack signal levels unless noted in-module.
- Supports audio-rate and/or control-rate use depending on patch context.

## Inputs and outputs

Refer to the module panel for exact jack labeling. As a general rule:

- **Signal inputs** receive audio or CV sources.
- **CV inputs** modulate front-panel parameters.
- **Main outputs** provide the processed/ generated signal.

## Patch ideas

1. Start with a simple source and destination (VCO → GravityClock → VCA).
2. Modulate one parameter at a time with slow LFO or envelope.
3. Add attenuation so modulation depth stays musical.
4. Record a few bars and compare subtle vs extreme settings.

## Related modules

See [module index](index.md) for complementary WiggleRoom modules to pair with **GravityClock**.
