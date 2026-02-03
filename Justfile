# WiggleRoom VCV Rack Plugin - Justfile
# Unified automation for local development and CI

# Configuration
slug := "WiggleRoom"
builder_image := "ghcr.io/qno/rack-plugin-toolchain:v2"

# Detect OS for Rack plugin path (VCV Rack Pro uses platform-specific folders)
rack_plugins := if os() == "macos" {
    env_var("HOME") + "/Library/Application Support/Rack2/plugins-mac-arm64"
} else if os() == "windows" {
    env_var("LOCALAPPDATA") + "/Rack2/plugins-win-x64"
} else {
    env_var("HOME") + "/.Rack2/plugins-lin-x64"
}

# Default: List available commands
default:
    @just --list

# --- Local Development (Native) ---

# Configure CMake locally for debug builds
configure:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Configure CMake for release builds
configure-release:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build the plugin locally
build: configure
    cmake --build build -j 4

# Build and install to local Rack plugins folder
install: build
    mkdir -p "{{rack_plugins}}/{{slug}}"
    cp build/plugin.dylib "{{rack_plugins}}/{{slug}}/" 2>/dev/null || \
    cp build/plugin.so "{{rack_plugins}}/{{slug}}/" 2>/dev/null || \
    cp build/plugin.dll "{{rack_plugins}}/{{slug}}/" 2>/dev/null || true
    cp plugin.json "{{rack_plugins}}/{{slug}}/"
    cp -r res "{{rack_plugins}}/{{slug}}/"
    @echo "Installed to {{rack_plugins}}/{{slug}}"

# Install and restart VCV Rack (macOS only)
[macos]
run: install
    @echo "Restarting VCV Rack..."
    -osascript -e 'quit app "VCV Rack 2 Pro"' 2>/dev/null || true
    -osascript -e 'quit app "VCV Rack 2 Free"' 2>/dev/null || true
    @sleep 1
    open -a "VCV Rack 2 Pro" 2>/dev/null || open -a "VCV Rack 2 Free" 2>/dev/null || echo "Please open VCV Rack manually"

# Clean build artifacts
clean:
    rm -rf build build-lin build-win build-mac dist

# --- CI / Cross-Compilation (Docker) ---

# Build for ALL platforms (Linux, Windows, macOS) inside Docker
package:
    docker run --rm -v "{{invocation_directory()}}":/work -w /work {{builder_image}} just _internal-cross

# (Internal) This runs INSIDE the Docker container
_internal-cross:
    #!/usr/bin/env bash
    set -euo pipefail

    echo "=== Building for Linux ==="
    cmake -B build-lin -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-Linux.cmake
    cmake --build build-lin --target dist -j

    echo "=== Building for Windows ==="
    cmake -B build-win -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-Mingw64.cmake
    cmake --build build-win --target dist -j

    echo "=== Building for macOS ==="
    cmake -B build-mac -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-Mac.cmake
    cmake --build build-mac --target dist -j

    echo "=== Gathering artifacts ==="
    mkdir -p dist
    # Package each platform build
    for platform in lin win mac; do
        if [ -d "build-${platform}/dist/{{slug}}" ]; then
            cd "build-${platform}/dist"
            zip -r "../../dist/{{slug}}-${platform}.vcvplugin" "{{slug}}"
            cd ../..
        fi
    done
    echo "=== Done! Artifacts in dist/ ==="
    ls -la dist/

# --- Testing ---

# Test all Faust DSP files compile correctly
test-faust:
    #!/usr/bin/env bash
    set -e
    echo "Testing Faust DSP files..."
    errors=0
    for dsp in $(find src/modules -name "*.dsp"); do
        echo -n "  $dsp ... "
        if faust "$dsp" -o /dev/null 2>&1; then
            echo "OK"
        else
            echo "FAILED"
            faust "$dsp" -o /dev/null 2>&1 || true
            errors=$((errors + 1))
        fi
    done
    if [ $errors -gt 0 ]; then
        echo "FAILED: $errors Faust file(s) have errors"
        exit 1
    fi
    echo "All Faust files OK"

# Generate full validation report with quality metrics and AI analysis
# Requires: GEMINI_API_KEY (set in environment or .env file)
test-report-full: build
    #!/usr/bin/env bash
    set -e
    # Source .env file if it exists
    if [ -f .env ]; then
        set -a
        source .env
        set +a
    fi
    if [ -z "$GEMINI_API_KEY" ]; then
        echo "Error: GEMINI_API_KEY is required"
        echo "Set it in your environment or create a .env file with: GEMINI_API_KEY=your_key"
        exit 1
    fi
    python3 test/run_tests.py --quick --clean --workers 4 --quality --ai

# Render audio from Faust modules and generate spectrograms
test-audio module="": build
    #!/usr/bin/env bash
    set -e
    if [ ! -f build/test/faust_render ]; then
        echo "Error: faust_render not found. Rebuilding..."
        cmake --build build -j 4
    fi
    if [ -n "{{module}}" ]; then
        python3 test/run_tests.py --module "{{module}}" --quick
    else
        python3 test/run_tests.py --quick
    fi
    echo "Open test/output/report.html to view results"

# Render audio with full parameter grid (slower)
test-audio-full module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/run_tests.py --module "{{module}}"
    else
        python3 test/run_tests.py
    fi
    echo "Open test/output/report.html to view results"

# List available modules and their parameters
list-params module="":
    @build/test/faust_render --list-modules
    @if [ -n "{{module}}" ]; then build/test/faust_render --module "{{module}}" --list-params; fi

# Analyze parameter sensitivity (which params actually affect sound?)
test-sensitivity module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/analyze_sensitivity.py --module "{{module}}"
    else
        python3 test/analyze_sensitivity.py
    fi
    echo "Open test/sensitivity/*/report.html to view results"

# Run comprehensive module tests (catches parameter issues, stability, regressions)
test-modules module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/test_framework.py --module "{{module}}" -v
    else
        python3 test/test_framework.py -v
    fi

# Run module tests in CI mode (JSON output, exit code)
test-modules-ci: build
    python3 test/test_framework.py --ci --output test/results.json

# Generate baseline files for regression testing
test-generate-baselines: build
    python3 test/test_framework.py --generate-baselines

# Regenerate HTML report from existing renders (no re-rendering)
test-report:
    python3 test/generate_report.py

# Run audio quality analysis (THD, aliasing, harmonics, envelope)
test-quality module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/audio_quality.py --module "{{module}}" -v
    else
        python3 test/audio_quality.py -v
    fi

# Run audio quality analysis with JSON output
test-quality-report module="": build
    #!/usr/bin/env bash
    set -e
    mkdir -p test/quality
    if [ -n "{{module}}" ]; then
        python3 test/audio_quality.py --module "{{module}}" --json --output "test/quality/{{module}}_quality.json"
        echo "Report saved to test/quality/{{module}}_quality.json"
    else
        python3 test/audio_quality.py --json --output "test/quality/all_modules_quality.json"
        echo "Report saved to test/quality/all_modules_quality.json"
    fi

# Run module tests without audio quality tests (faster)
test-modules-fast module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/test_framework.py --module "{{module}}" --no-quality -v
    else
        python3 test/test_framework.py --no-quality -v
    fi

# AI-powered audio analysis (Gemini + CLAP)
test-ai module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/ai_audio_analysis.py --module "{{module}}" -v
    else
        python3 test/ai_audio_analysis.py --instruments-only -v
    fi

# AI analysis with CLAP only (no API key needed)
test-ai-clap module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/ai_audio_analysis.py --module "{{module}}" --clap-only -v
    else
        python3 test/ai_audio_analysis.py --instruments-only --clap-only -v
    fi

# Analyze parameter ranges to find safe/interesting areas
analyze-ranges module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/analyze_param_ranges.py --module "{{module}}" --steps 15 -v
    else
        echo "Usage: just analyze-ranges ModuleName"
        exit 1
    fi

# Generate Faust code with recommended parameter ranges
analyze-ranges-faust module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/analyze_param_ranges.py --module "{{module}}" --steps 15 --faust
    else
        echo "Usage: just analyze-ranges-faust ModuleName"
        exit 1
    fi

# Full validation: quality + AI + range analysis
validate module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        echo "=== Audio Quality Test ==="
        python3 test/audio_quality.py --module "{{module}}" --report
        echo ""
        echo "=== Parameter Range Analysis ==="
        python3 test/analyze_param_ranges.py --module "{{module}}" --steps 12
        echo ""
        echo "=== AI Analysis ==="
        python3 test/ai_audio_analysis.py --module "{{module}}" --clap-only
    else
        echo "Usage: just validate ModuleName"
        exit 1
    fi

# Run all tests (Faust compilation + module tests)
test: test-faust test-modules

# --- Showcase Reports ---
# Note: For Gemini AI analysis, create .env file with: GEMINI_API_KEY=your_key
# The scripts automatically load .env from project root

# Generate showcase report for all modules (includes AI analysis if .env has GEMINI_API_KEY)
showcase: build
    python3 test/generate_showcase_report.py -v
    @echo "Open test/output/showcase_report.html to view results"

# Generate showcase report for specific module
showcase-module module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/generate_showcase_report.py --module "{{module}}" -v
        echo "Open test/output/showcase_report.html to view results"
    else
        echo "Usage: just showcase-module ModuleName"
        exit 1
    fi

# Generate showcase report without AI analysis (faster)
showcase-fast: build
    python3 test/generate_showcase_report.py --skip-ai -v
    @echo "Open test/output/showcase_report.html to view results"

# Generate showcase with systematic parameter sweeps (auto-generated comprehensive coverage)
showcase-systematic: build
    python3 test/generate_showcase_report.py --systematic -v
    @echo "Open test/output/showcase_report.html to view results"

# Render showcase audio only (no report)
render-showcase module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        ./build/test/faust_render --module "{{module}}" --showcase --output "test/output/{{module}}_showcase.wav"
        echo "Rendered test/output/{{module}}_showcase.wav"
    else
        echo "Usage: just render-showcase ModuleName"
        exit 1
    fi

# --- Parameter Grid Analysis ---

# Generate parameter grid analysis for a module (all permutations)
param-grid module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/generate_param_grid.py --module "{{module}}" -v
        echo "Open test/output/param_grids/{{module}}.html to view results"
    else
        echo "Usage: just param-grid ModuleName"
        exit 1
    fi

# Generate parameter grid for all modules
param-grid-all: build
    python3 test/generate_param_grid.py --all -v
    @echo "Open test/output/param_grids/ to view results"

# Generate parameter grid without AI analysis (faster)
param-grid-fast module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/generate_param_grid.py --module "{{module}}" --no-ai -v
        echo "Open test/output/param_grids/{{module}}.html to view results"
    else
        python3 test/generate_param_grid.py --all --no-ai -v
        echo "Open test/output/param_grids/ to view results"
    fi

# Generate parameter grid with CLAP only (no Gemini)
param-grid-clap module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 test/generate_param_grid.py --module "{{module}}" --no-gemini -v
        echo "Open test/output/param_grids/{{module}}.html to view results"
    else
        python3 test/generate_param_grid.py --all --no-gemini -v
        echo "Open test/output/param_grids/ to view results"
    fi

# Full analysis: showcase + parameter grid
full-analysis module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        echo "=== Showcase Report ==="
        python3 test/generate_showcase_report.py --module "{{module}}" -v
        echo ""
        echo "=== Parameter Grid Analysis ==="
        python3 test/generate_param_grid.py --module "{{module}}" -v
        echo ""
        echo "Reports:"
        echo "  - test/output/showcase_report.html"
        echo "  - test/output/param_grids/{{module}}.html"
    else
        echo "Usage: just full-analysis ModuleName"
        exit 1
    fi

# --- Agent System ---

# Run single verification + judgment iteration
agent-verify module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 -m test.agents.orchestrator "{{module}}" --single -v
    else
        echo "Usage: just agent-verify ModuleName"
        exit 1
    fi

# Run verification and get fix instructions (for Claude)
agent-fix module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 -m test.agents.orchestrator "{{module}}" --single
    else
        echo "Usage: just agent-fix ModuleName"
        exit 1
    fi

# Run full development loop (iterates until pass or max iterations)
agent-loop module="" iterations="10": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 -m test.agents.orchestrator "{{module}}" --max-iterations {{iterations}} -v
    else
        echo "Usage: just agent-loop ModuleName [iterations]"
        exit 1
    fi

# Get JSON output from agents
agent-json module="": build
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 -m test.agents.orchestrator "{{module}}" --single --json
    else
        echo "Usage: just agent-json ModuleName"
        exit 1
    fi

# --- Faceplate Generation ---

# Generate AI-powered faceplate PNG for a module (uses Gemini image generation)
# Requires GEMINI_API_KEY in .env file
# PNG files are loaded via NanoVG ImagePanel widget at runtime
faceplate module="":
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 scripts/generate_faceplate.py "{{module}}"
    else
        echo "Usage: just faceplate ModuleName"
        echo "       just faceplate --all"
        python3 scripts/generate_faceplate.py --list
    fi

# Preview faceplate prompt (no generation)
faceplate-prompt module="":
    #!/usr/bin/env bash
    set -e
    if [ -n "{{module}}" ]; then
        python3 scripts/generate_faceplate.py "{{module}}" --prompt-only
    else
        echo "Usage: just faceplate-prompt ModuleName"
        exit 1
    fi

# Generate faceplates for all Faust DSP modules
faceplate-all:
    python3 scripts/generate_faceplate.py --all

# --- Development Helpers ---

# Watch for changes and rebuild (requires entr)
watch:
    find src -name "*.cpp" -o -name "*.hpp" | entr -c just build

# Format all C++ source files (requires clang-format)
format:
    find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

# Run cppcheck static analysis (requires cppcheck)
lint:
    cppcheck --enable=all --suppress=missingIncludeSystem src/

# Show VCV Rack log filtered for this plugin (macOS)
[macos]
log:
    @tail -100 ~/Library/Application\ Support/Rack2/log.txt | grep -i "{{slug}}\|error" || echo "No recent log entries"

# Show full VCV Rack log (macOS)
[macos]
log-full:
    @tail -200 ~/Library/Application\ Support/Rack2/log.txt
