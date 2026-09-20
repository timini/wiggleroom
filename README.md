# Wiggle Room

WiggleRoom 2.1.1 is an ACID9 Voice release for [VCV Rack 2](https://vcvrack.com/): a monophonic acid voice with morphing stereo oscillators, two filter characters, accent/slide, an insert loop and stereo delay.

![ACID9 Voice](design/ACID9Voice/preview.png)

**[ACID9 manual](docs/user/modules/ACID9Voice.md)** · **[Example patch](examples/ACID9-first-sound.vcv)** · **[Release checklist](docs/release/2.1.1.md)**

## Installation

Download the `.vcvplugin` matching your OS and CPU from [Releases](https://github.com/timini/WiggleRoom/releases). Place it in your Rack user folder's `plugins-<OS>-<CPU>` directory and restart Rack. The package contains ACID9 Voice only. VCV Library availability depends on completion of the [existing submission](https://github.com/VCVRack/library/issues/877).

The repository also contains experimental modules. They are excluded from this release's Makefile and manifest. Existing development patches containing those modules need their original development build. ACID9's panel is now 34HP, so older patches may need rearranging.

## Build the release

Download the official Rack SDK for your platform and unpack it, then run:

```sh
make -j2 dist RACK_DIR=/absolute/path/to/Rack-SDK
```

This uses Rack's `plugin.mk` and creates a zstd-compressed `.vcvplugin` in `dist/`. The generated DSP header is committed; Faust is not required to build the release. CI builds Linux x64, Windows x64, macOS x64 and macOS arm64 with SDK 2.6.6.

To regenerate the DSP, install Faust **2.85.9** and run `scripts/generate_acid_dsp.sh`. To run the actual module's DSP/adapter integration tests on macOS or Linux:

```sh
RACK_DIR=/absolute/path/to/Rack-SDK scripts/test_acid_release.sh
python3 scripts/verify_release_package.py dist/WiggleRoom-2.1.1-mac-arm64.vcvplugin mac-arm64
```

Replace the example package/platform with your build's values. CMake and the broader test system remain development tooling; release packages use the Makefile above.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE). Artwork generation references and the layout source are in `design/ACID9Voice` and `scripts/generate_acid_panel.py`.
