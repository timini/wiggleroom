# PixelProbe

## Overview

PixelProbe is a WiggleRoom module in the **Utilities** category. An image-to-CV colour sampler. Load an image, address a probe with X and Y CV, and read back Hue, Saturation and Intensity as control voltages, with pan and zoom over the canvas and a choice of unipolar or bipolar output.

## Signal Flow

- PixelProbe generates CV from an image, it does not process audio.
- X and Y inputs move the probe across the canvas.
- The three outputs carry Hue, Saturation and Intensity for whatever pixel the probe currently sits on.
- Pan and Zoom decide which part of the image the probe's full CV range maps onto.

## Controls

| Control | Description |
|---------|-------------|
| Pan X | Moves the sampled region horizontally across the image |
| Pan Y | Moves the sampled region vertically across the image |
| Zoom | Sets how much of the image the probe's travel covers |
| Mode | Switches the outputs between unipolar and bipolar ranges |

Load an image from the module's right-click menu.

## Typical Uses

- Scan a photograph with two slow LFOs on X and Y to get three correlated but non-repeating modulation sources.
- Zoom into a small region of a gradient for smooth, slowly drifting CV rather than jumpy transitions.
- Drive X from a sequencer and Y from a fixed offset to read a single row as a repeatable pattern.

## Tips

- Images with large flat areas give long stretches of unchanging CV. High-contrast or noisy images give lively output.
- Zooming right in turns any image into a smooth source, because neighbouring pixels differ very little.
- Hue wraps around, so it jumps when the probe crosses red. Saturation and Intensity are continuous, which makes them better for pitch.
- Drive the same X and Y from another modulation source you are already using, and the three outputs will stay musically related to it.
