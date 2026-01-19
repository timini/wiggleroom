# WiggleRoom Faceplate Generation Prompt

This is the prompt template used by `generate_faceplate.py` to create AI-generated module faceplates via Gemini.

## Style Guide

**Aesthetic:** Grateful Dead merchandise art meets dark futuristic psychedelia

- Classic 60s/70s Grateful Dead poster art style (flowing lettering, organic shapes, skulls, roses, dancing bears vibe)
- But with a DARK color palette: deep purples, navy blues, blacks
- Bioluminescent neon accents: cyan, magenta, orange, gold glows
- Sacred geometry and fractal patterns subtly woven in
- Synth-wave / retro-futuristic atmosphere

## Prompt Template

```
Create a complete synthesizer module faceplate panel image.

Module name: {DISPLAY_NAME}
Description: {DESCRIPTION}
Theme: {THEME}

CRITICAL REQUIREMENTS:
1. VERTICAL ORIENTATION - the panel is tall and narrow (aspect ratio approximately 1:{ASPECT_RATIO})
2. Include the text "{DISPLAY_NAME}" prominently at the TOP of the panel
3. Include "WiggleRoom" small at the BOTTOM as the brand name
4. STYLE: Grateful Dead concert poster / merchandise art aesthetic
   - Flowing psychedelic lettering for the title
   - Organic, hand-drawn quality
   - Classic 60s/70s poster art composition
5. COLOR PALETTE: Dark futuristic psychedelia
   - Dark background (deep purple, navy blue, or black base)
   - Bioluminescent glowing accents in cyan, magenta, orange, and gold
   - Neon glow effects on key elements
6. IMAGERY:
   - Flowing organic shapes and tendrils
   - Sacred geometry and subtle fractals
   - Cosmic/space elements (stars, nebulae)
   - Optional: stylized skulls, roses, cosmic imagery in the Dead tradition
7. COMPOSITION:
   - Leave MIDDLE AREA relatively calm/dark for knobs and jacks
   - Text should be stylized but READABLE
   - Symmetrical or balanced design

The image should evoke the sonic character: {DESCRIPTION}

Make it feel like a premium hardware synthesizer module that could be vintage Grateful Dead merchandise reimagined for a dark electronic future.
```

## Module Type Themes

| Type | Theme Inspiration |
|------|-------------------|
| instrument | ethereal musical instruments, flowing sound waves, resonating strings |
| filter | liquid flowing through geometric shapes, frequency spectrums, waveforms |
| effect | swirling echoes, rippling transformations, time and space distortions |
| resonator | vibrating membranes, resonant chambers, standing waves, harmonics |
| utility | clean geometric patterns, mathematical precision, signal flow |
