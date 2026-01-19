#!/usr/bin/env python3
"""
Generate VCV Rack module faceplates using Gemini AI image generation.

Creates organic, psychedelic-style backgrounds based on module description.
Outputs PNG files that are loaded via NanoVG ImagePanel widget (VCV Rack's
NanoSVG parser ignores embedded raster images, so we use programmatic loading).
"""

import argparse
import base64
import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path

# Add test directory for .env loading
script_dir = Path(__file__).parent
project_root = script_dir.parent
sys.path.insert(0, str(project_root / "test"))

# Load environment variables
try:
    from dotenv import load_dotenv
    load_dotenv(project_root / ".env")
except ImportError:
    pass

# VCV Rack panel dimensions
HP_WIDTH_MM = 15.24  # 1HP in mm
PANEL_HEIGHT_MM = 380  # Standard 3U height in mm
DEFAULT_HP = 8

# Module HP widths (from SVG files)
MODULE_HP = {
    "BigReverb": 4,
    "ChaosFlute": 4,
    "Cycloid": 12,
    "GravityClock": 2,
    "InfiniteFolder": 2,
    "Intersect": 8,
    "Matter": 8,
    "ModalBell": 8,
    "MoogLPF": 4,
    "OctoLFO": 7,
    "PluckedString": 6,
    "SaturationEcho": 4,
    "SolinaEnsemble": 4,
    "SpaceCello": 4,
    "SpectralResonator": 4,
    "SuperOsc": 3,
    "TheAbyss": 4,
    "TheArchitect": 5,
    "TheCauldron": 3,
}


def get_module_hp(module_name: str) -> int:
    """Get the HP width for a module."""
    return MODULE_HP.get(module_name, DEFAULT_HP)


def get_module_info(module_name: str) -> dict:
    """Extract module info from DSP file and test_config.json."""
    module_dir = project_root / "src" / "modules" / module_name

    info = {
        "name": module_name,
        "description": "",
        "module_type": "effect",
        "parameters": []
    }

    # Try to read test_config.json
    config_path = module_dir / "test_config.json"
    if config_path.exists():
        with open(config_path) as f:
            config = json.load(f)
            info["description"] = config.get("description", "")
            info["module_type"] = config.get("module_type", "effect")

    # Try to read DSP file for more detail
    dsp_pattern = list(module_dir.glob("*.dsp"))
    if dsp_pattern:
        dsp_path = dsp_pattern[0]
        with open(dsp_path) as f:
            dsp_content = f.read()

            # Extract declare description
            desc_match = re.search(r'declare\s+description\s+"([^"]+)"', dsp_content)
            if desc_match and not info["description"]:
                info["description"] = desc_match.group(1)

            # Extract parameter names from hslider/vslider
            param_matches = re.findall(r'hslider\("([^"]+)"', dsp_content)
            info["parameters"] = [p for p in param_matches if p not in ("gate", "volts")]

    return info


def generate_background_prompt(module_info: dict, hp: int) -> str:
    """Generate a prompt for the AI to create a complete faceplate image.

    Style: Grateful Dead merchandise art meets dark futuristic psychedelia.
    See scripts/faceplate_prompt.md for full style guide.
    """

    # Map module types to visual themes
    type_themes = {
        "instrument": "ethereal musical instruments, flowing sound waves, resonating strings",
        "filter": "liquid flowing through geometric shapes, frequency spectrums, waveforms",
        "effect": "swirling echoes, rippling transformations, time and space distortions",
        "resonator": "vibrating membranes, resonant chambers, standing waves, harmonics",
        "utility": "clean geometric patterns, mathematical precision, signal flow",
    }

    theme = type_themes.get(module_info["module_type"], "abstract audio synthesis")

    # Format module name for display (split camelCase)
    name = module_info['name']
    import re
    name_parts = re.findall(r'[A-Z][a-z]*|[a-z]+', name)
    display_name = ' '.join(name_parts).upper()

    aspect_ratio = PANEL_HEIGHT_MM / (hp * HP_WIDTH_MM)

    prompt = f"""Create a psychedelic poster illustration. NOT a photo. Pure artwork only.

Text "{display_name}" at TOP in psychedelic flowing lettering.
Text "WIGGLE ROOM" prominently at BOTTOM in stylized text (this is the brand name - make it visible and readable).

STYLE: Grateful Dead concert t-shirt meets dark futuristic psychedelia
- Flowing psychedelic lettering (60s/70s Dead poster style)
- Organic, hand-drawn illustration quality
- Dark background (deep purple, navy blue, or black)
- Bioluminescent glowing accents in cyan, magenta, orange, gold
- Neon glow effects on key elements

IMAGERY:
- Flowing organic shapes and tendrils
- Sacred geometry and subtle fractals
- Cosmic/space elements (stars, nebulae, galaxies)
- Stylized cosmic imagery in the Grateful Dead tradition

COMPOSITION:
- Leave MIDDLE AREA relatively calm/dark for hardware controls
- Text should be stylized but READABLE
- Symmetrical or balanced design
- Dark edges blending to black at all borders
- "WIGGLE ROOM" brand text at bottom should be clear and prominent

DO NOT include synthesizers, knobs, sliders, hardware, equipment, or photos.
This is illustration art only."""
    return prompt


def generate_faceplate_image(module_info: dict, output_path: Path, hp: int) -> Path:
    """Use Gemini to generate a faceplate image."""
    from google import genai
    from google.genai import types
    from PIL import Image
    import io

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        raise ValueError("GEMINI_API_KEY not found in environment. Add it to .env file.")

    client = genai.Client(api_key=api_key)

    prompt = generate_background_prompt(module_info, hp)

    print(f"Generating faceplate for {module_info['name']} ({hp}HP)...")
    print(f"Prompt preview: {prompt[:300]}...")

    # Use Imagen 4 model for image generation with aspect ratio support
    # Request 9:16 aspect ratio (closest to 1:3 panel ratio)
    response = client.models.generate_images(
        model="imagen-4.0-generate-001",
        prompt=prompt,
        config=types.GenerateImagesConfig(
            aspectRatio="9:16",
            numberOfImages=1,
        )
    )

    # Calculate target dimensions for the module panel
    # VCV Rack panels: 1HP = 15.24mm width, height = 380mm
    # Use ~3 pixels per mm for good resolution
    target_width = int(hp * 15.24 * 3)   # e.g., 8HP = 366 pixels
    target_height = int(380 * 3)          # = 1140 pixels

    # Extract image from response
    image_path = output_path.with_suffix('.png')

    # Navigate the response structure for generate_images
    if response.generated_images:
        for gen_image in response.generated_images:
            if gen_image.image and gen_image.image.image_bytes:
                image_data = gen_image.image.image_bytes
                # Resize to correct aspect ratio for VCV Rack panel
                img = Image.open(io.BytesIO(image_data))
                img_resized = img.resize((target_width, target_height), Image.Resampling.LANCZOS)
                img_resized.save(image_path, 'PNG')
                print(f"Generated faceplate: {image_path} ({target_width}x{target_height})")
                return image_path

    raise ValueError("No image generated in response")


def backup_existing(output_path: Path) -> None:
    """Backup existing file if it exists."""
    if output_path.exists():
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_dir = project_root / "res" / "faceplate_archive"
        backup_dir.mkdir(exist_ok=True)
        backup_path = backup_dir / f"{output_path.stem}_{timestamp}{output_path.suffix}"
        output_path.rename(backup_path)
        print(f"Backed up existing to: {backup_path}")


def main():
    parser = argparse.ArgumentParser(description="Generate VCV Rack module faceplate using Gemini AI")
    parser.add_argument("module", nargs="?", help="Module name (e.g., ChaosFlute)")
    parser.add_argument("--hp", type=int, help="Panel width in HP (auto-detected if not specified)")
    parser.add_argument("--output", help="Output PNG path (default: res/<ModuleName>.png)")
    parser.add_argument("--prompt-only", action="store_true", help="Only show the prompt, don't generate")
    parser.add_argument("--all", action="store_true", help="Generate faceplates for all modules")
    parser.add_argument("--list", action="store_true", help="List available modules")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing files (default: backup)")

    args = parser.parse_args()

    # List modules
    if args.list:
        modules_dir = project_root / "src" / "modules"
        print("Available modules:")
        for module_dir in sorted(modules_dir.iterdir()):
            if module_dir.is_dir():
                hp = get_module_hp(module_dir.name)
                info = get_module_info(module_dir.name)
                desc = info['description'][:50] + "..." if len(info['description']) > 50 else info['description']
                print(f"  {module_dir.name} ({hp}HP): {desc}")
        return

    # Generate for all modules
    if args.all:
        modules_dir = project_root / "src" / "modules"
        for module_dir in sorted(modules_dir.iterdir()):
            if module_dir.is_dir():
                module_name = module_dir.name
                module_info = get_module_info(module_name)
                hp = args.hp if args.hp else get_module_hp(module_name)
                output_png = project_root / "res" / f"{module_name}.png"
                try:
                    if not args.overwrite:
                        backup_existing(output_png)
                    generate_faceplate_image(module_info, output_png, hp)
                except Exception as e:
                    print(f"Error generating {module_name}: {e}")
        print("\nDone! All faceplates generated.")
        return

    # Single module
    if not args.module:
        parser.error("Module name required (or use --all)")

    module_info = get_module_info(args.module)
    hp = args.hp if args.hp else get_module_hp(args.module)

    if args.prompt_only:
        print(generate_background_prompt(module_info, hp))
        return

    # Set output path
    if args.output:
        output_png = Path(args.output)
    else:
        output_png = project_root / "res" / f"{args.module}.png"

    # Backup existing if not overwriting
    if not args.overwrite:
        backup_existing(output_png)

    # Generate the faceplate
    generate_faceplate_image(module_info, output_png, hp)

    print(f"\nDone! Faceplate for {args.module} generated at {output_png}")


if __name__ == "__main__":
    main()
