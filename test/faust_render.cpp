/**
 * Faust DSP Audio Renderer
 *
 * Standalone tool to render audio from Faust DSP modules for testing.
 * Generates WAV files with various parameter combinations.
 *
 * Usage:
 *   ./faust_render --module TheAbyss --output test.wav --duration 2.0 \
 *       --param decay=0.8 --param pressure=0.6
 *   ./faust_render --module MoogLPF --list-params
 */

#include "AbstractDSP.hpp"
#include "ModuleTestConfig.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace WiggleRoom::TestConfig;

// Forward declarations for factory functions (defined in separate source files)
std::unique_ptr<AbstractDSP> createMoogLPF();
std::unique_ptr<AbstractDSP> createBigReverb();
std::unique_ptr<AbstractDSP> createSaturationEcho();
std::unique_ptr<AbstractDSP> createSpectralResonator();
std::unique_ptr<AbstractDSP> createModalBell();
std::unique_ptr<AbstractDSP> createPluckedString();
std::unique_ptr<AbstractDSP> createChaosFlute();
std::unique_ptr<AbstractDSP> createSolinaEnsemble();
std::unique_ptr<AbstractDSP> createInfiniteFolder();
std::unique_ptr<AbstractDSP> createSpaceCello();
std::unique_ptr<AbstractDSP> createTheAbyss();
std::unique_ptr<AbstractDSP> createMatter();
std::unique_ptr<AbstractDSP> createTheCauldron();
std::unique_ptr<AbstractDSP> createVektorX();
std::unique_ptr<AbstractDSP> createTR808();
std::unique_ptr<AbstractDSP> createACID9Voice();
std::unique_ptr<AbstractDSP> createTetanusCoil();
std::unique_ptr<AbstractDSP> createNutShaker();
std::unique_ptr<AbstractDSP> createPhysicalChoir();
std::unique_ptr<AbstractDSP> createChaosPad();

// ============================================================================
// WAV File Writer (no external dependencies)
// ============================================================================

struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

bool writeWav(const std::string& filename, const std::vector<float>& samples,
              int sampleRate, int numChannels) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for writing: " << filename << std::endl;
        return false;
    }

    uint32_t numSamples = samples.size();
    uint32_t dataSize = numSamples * sizeof(int16_t);

    WavHeader header;
    header.numChannels = numChannels;
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * numChannels * sizeof(int16_t);
    header.blockAlign = numChannels * sizeof(int16_t);
    header.dataSize = dataSize;
    header.fileSize = sizeof(WavHeader) - 8 + dataSize;

    file.write(reinterpret_cast<char*>(&header), sizeof(header));

    // Convert float samples to 16-bit PCM
    for (float sample : samples) {
        // Clamp to [-1, 1]
        sample = std::max(-1.0f, std::min(1.0f, sample));
        int16_t pcmSample = static_cast<int16_t>(sample * 32767.0f);
        file.write(reinterpret_cast<char*>(&pcmSample), sizeof(pcmSample));
    }

    return true;
}

// ============================================================================
// DSP Factory
// ============================================================================

std::unique_ptr<AbstractDSP> createDSP(const std::string& moduleName) {
    if (moduleName == "MoogLPF") return createMoogLPF();
    if (moduleName == "BigReverb") return createBigReverb();
    if (moduleName == "SaturationEcho") return createSaturationEcho();
    if (moduleName == "SpectralResonator") return createSpectralResonator();
    if (moduleName == "ModalBell") return createModalBell();
    if (moduleName == "PluckedString") return createPluckedString();
    if (moduleName == "ChaosFlute") return createChaosFlute();
    if (moduleName == "SolinaEnsemble") return createSolinaEnsemble();
    if (moduleName == "InfiniteFolder") return createInfiniteFolder();
    if (moduleName == "SpaceCello") return createSpaceCello();
    if (moduleName == "TheAbyss") return createTheAbyss();
    if (moduleName == "Matter") return createMatter();
    if (moduleName == "TheCauldron") return createTheCauldron();
    if (moduleName == "VektorX") return createVektorX();
    if (moduleName == "TR808") return createTR808();
    if (moduleName == "ACID9Voice") return createACID9Voice();
    if (moduleName == "TetanusCoil") return createTetanusCoil();
    if (moduleName == "NutShaker") return createNutShaker();
    if (moduleName == "PhysicalChoir") return createPhysicalChoir();
    if (moduleName == "ChaosPad") return createChaosPad();
    return nullptr;
}

std::vector<std::string> getModuleNames() {
    return {"MoogLPF", "BigReverb", "SaturationEcho", "SpectralResonator",
            "ModalBell", "PluckedString", "ChaosFlute", "SolinaEnsemble",
            "InfiniteFolder", "SpaceCello", "TheAbyss", "Matter",
            "TheCauldron", "VektorX", "TR808", "ACID9Voice", "TetanusCoil",
            "NutShaker", "PhysicalChoir", "ChaosPad"};
}

// ============================================================================
// Module Config Loading
// ============================================================================

// Find the project root by looking for CMakeLists.txt or plugin.json
std::string findProjectRoot() {
    // Try relative paths from where faust_render is typically located
    std::vector<std::string> candidates = {
        "..",           // build/test -> build -> project_root
        "../..",        // build/test -> project_root
        "../../..",     // nested build dirs
        "."             // current directory
    };

    for (const auto& candidate : candidates) {
        std::string test_path = candidate + "/plugin.json";
        std::ifstream f(test_path);
        if (f.good()) {
            return candidate;
        }
    }
    return ".";  // fallback to current directory
}

// Load module config from its directory
ModuleTestConfig loadModuleConfig(const std::string& moduleName) {
    std::string projectRoot = findProjectRoot();
    std::string configPath = projectRoot + "/src/modules/" + moduleName + "/test_config.json";

    ModuleTestConfig config = load_module_config(configPath, moduleName);

    // If config file not found, use defaults based on legacy hardcoded rules
    if (config.module_type == ModuleType::Instrument &&
        config.test_scenarios.size() == 1 &&
        config.test_scenarios[0].name == "default") {

        // Apply legacy type detection for backwards compatibility
        if (moduleName == "MoogLPF" || moduleName == "InfiniteFolder") {
            config.module_type = ModuleType::Filter;
        } else if (moduleName == "SpectralResonator") {
            config.module_type = ModuleType::Resonator;
        } else if (moduleName == "BigReverb" || moduleName == "SaturationEcho" ||
                   moduleName == "SolinaEnsemble") {
            config.module_type = ModuleType::Effect;
        }

        // Apply legacy hot signal detection
        if (moduleName == "InfiniteFolder") {
            config.thresholds.allow_hot_signal = true;
        }
    }

    return config;
}

// List available scenarios for a module
void listScenarios(const ModuleTestConfig& config) {
    std::cout << "Test scenarios for " << config.module_name << ":\n";
    for (const auto& scenario : config.test_scenarios) {
        std::cout << "  " << scenario.name;
        if (!scenario.description.empty()) {
            std::cout << " - " << scenario.description;
        }
        std::cout << " (duration: " << scenario.duration << "s)\n";
        if (!scenario.parameters.empty()) {
            std::cout << "    Parameters: ";
            bool first = true;
            for (const auto& kv : scenario.parameters) {
                if (!first) std::cout << ", ";
                std::cout << kv.first << "=" << kv.second;
                first = false;
            }
            std::cout << "\n";
        }
    }
}

// ============================================================================
// Audio Rendering
// ============================================================================

std::vector<float> renderAudio(AbstractDSP& dsp, int sampleRate, float duration,
                               ModuleType type, bool noAutoGate = false,
                               const TestScenario* scenario = nullptr) {
    int numSamples = static_cast<int>(duration * sampleRate);
    int numInputs = dsp.getNumInputs();
    int numOutputs = dsp.getNumOutputs();

    // Allocate buffers
    std::vector<float> inputBuffer(numInputs, 0.0f);
    std::vector<float> outputBuffer(numOutputs, 0.0f);
    std::vector<float*> inputPtrs(numInputs);
    std::vector<float*> outputPtrs(numOutputs);
    for (int i = 0; i < numInputs; i++) inputPtrs[i] = &inputBuffer[i];
    for (int i = 0; i < numOutputs; i++) outputPtrs[i] = &outputBuffer[i];

    // Output samples (interleaved if stereo)
    std::vector<float> output;
    output.reserve(numSamples * numOutputs);

    // Random generator for noise
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    // Gate timing: on for first half, then release
    int gateOnSamples = static_cast<int>(duration * 0.4f * sampleRate);
    float sawPhase = 0.0f;
    float sawFreq = 440.0f / sampleRate;

    // Find gate/trigger parameter if exists
    // Check for custom trigger_param in scenario first
    int gateParamIdx = dsp.getParamIndex("gate");
    int triggerParamIdx = dsp.getParamIndex("trigger");
    int velocityParamIdx = dsp.getParamIndex("velocity");
    int customTriggerIdx = -1;

    // If scenario specifies a custom trigger_param, use that instead
    if (scenario && !scenario->trigger_param.empty()) {
        customTriggerIdx = dsp.getParamIndex(scenario->trigger_param.c_str());
        if (customTriggerIdx >= 0) {
            // Use custom trigger, disable generic gate/trigger
            gateParamIdx = -1;
            triggerParamIdx = -1;
        }
    }

    // Trigger pulse duration in samples (10ms)
    int triggerPulseSamples = static_cast<int>(0.01f * sampleRate);

    for (int i = 0; i < numSamples; i++) {
        bool gateOn = (i < gateOnSamples);

        // Set gate/trigger parameters (unless disabled for testing)
        if (!noAutoGate) {
            // Custom trigger parameter (for drums, etc.)
            if (customTriggerIdx >= 0) {
                // Send trigger pulse at sample 0
                if (i < triggerPulseSamples) {
                    dsp.setParamValue(customTriggerIdx, 10.0f);  // VCV standard trigger level
                } else {
                    dsp.setParamValue(customTriggerIdx, 0.0f);
                }
            }

            // Generic gate/trigger (for standard instruments)
            if (gateParamIdx >= 0) {
                dsp.setParamValue(gateParamIdx, gateOn ? 1.0f : 0.0f);
            }
            if (triggerParamIdx >= 0 && i == 0) {
                dsp.setParamValue(triggerParamIdx, 1.0f);
            } else if (triggerParamIdx >= 0) {
                dsp.setParamValue(triggerParamIdx, 0.0f);
            }
            if (velocityParamIdx >= 0) {
                dsp.setParamValue(velocityParamIdx, gateOn ? 1.0f : 0.0f);
            }
        }

        // Generate input based on module type
        if (numInputs > 0) {
            switch (type) {
                case ModuleType::Filter:
                    // Saw wave for filters
                    inputBuffer[0] = sawPhase * 2.0f - 1.0f;
                    sawPhase += sawFreq;
                    if (sawPhase >= 1.0f) sawPhase -= 1.0f;
                    break;

                case ModuleType::Resonator:
                    // Repeated noise bursts to excite resonances
                    // Burst every 0.3 seconds for 20ms
                    {
                        int burstPeriod = static_cast<int>(0.3f * sampleRate);
                        int burstLength = static_cast<int>(0.02f * sampleRate);
                        int posInCycle = i % burstPeriod;
                        if (posInCycle < burstLength) {
                            inputBuffer[0] = noiseDist(rng) * 0.8f;
                        } else {
                            inputBuffer[0] = 0.0f;
                        }
                    }
                    break;

                case ModuleType::Effect:
                    // Short burst for effects (first 100ms)
                    // For multi-input effects, provide different signals to each input
                    if (i < sampleRate / 10) {
                        float noise = noiseDist(rng) * 0.5f;
                        for (int ch = 0; ch < numInputs; ch++) {
                            // Different frequencies/phases per input
                            float phase = (sawPhase + ch * 0.25f);
                            if (phase >= 1.0f) phase -= 1.0f;
                            inputBuffer[ch] = phase * 2.0f - 1.0f + noise * 0.1f;
                        }
                        sawPhase += sawFreq;
                        if (sawPhase >= 1.0f) sawPhase -= 1.0f;
                    } else {
                        for (int ch = 0; ch < numInputs; ch++) {
                            inputBuffer[ch] = 0.0f;
                        }
                    }
                    break;

                case ModuleType::Instrument:
                case ModuleType::Utility:
                    // No audio input needed
                    inputBuffer[0] = 0.0f;
                    break;
            }
        }

        // Process one sample
        dsp.compute(1, inputPtrs.data(), outputPtrs.data());

        // Collect output (interleave if stereo)
        for (int ch = 0; ch < numOutputs; ch++) {
            output.push_back(outputBuffer[ch]);
        }
    }

    return output;
}

// ============================================================================
// Showcase Audio Rendering
// ============================================================================

// Generate default showcase config based on module type
ShowcaseConfig getDefaultShowcaseConfig(ModuleType type) {
    ShowcaseConfig config;
    config.enabled = true;

    switch (type) {
        case ModuleType::Instrument:
            // 10s with 4 notes at different octaves + parameter sweep
            config.duration = 10.0f;
            config.notes = {
                {0.0f, 2.0f, -1.0f, 1.0f},   // C3 at 0s
                {2.5f, 2.0f, 0.0f, 0.9f},    // C4 at 2.5s
                {5.0f, 2.0f, 1.0f, 0.8f},    // C5 at 5s
                {7.5f, 2.0f, 0.583f, 0.85f}  // G4 at 7.5s (7 semitones = 7/12 volts)
            };
            break;

        case ModuleType::Filter:
            // 8s with continuous saw input and cutoff sweep
            config.duration = 8.0f;
            // Filters don't use notes, but we'll add an automation for cutoff
            config.automations = {
                {"cutoff", 0.0f, 4.0f, 0.1f, 0.9f},   // Sweep up
                {"cutoff", 4.0f, 8.0f, 0.9f, 0.1f},   // Sweep down
            };
            break;

        case ModuleType::Effect:
            // 10s with repeated bursts
            config.duration = 10.0f;
            // Effects will get bursts + wet/dry sweep if available
            config.automations = {
                {"mix", 0.0f, 10.0f, 0.3f, 1.0f},
            };
            break;

        case ModuleType::Resonator:
            // 10s with varied excitation
            config.duration = 10.0f;
            config.automations = {
                {"decay", 0.0f, 5.0f, 0.3f, 0.9f},
                {"decay", 5.0f, 10.0f, 0.9f, 0.3f},
            };
            break;

        case ModuleType::Utility:
            // 5s basic render
            config.duration = 5.0f;
            break;
    }

    return config;
}

// Render showcase audio with multiple notes and parameter automations
std::vector<float> renderShowcaseAudio(AbstractDSP& dsp, int sampleRate,
                                        const ShowcaseConfig& showcase,
                                        ModuleType type) {
    int numSamples = static_cast<int>(showcase.duration * sampleRate);
    int numInputs = dsp.getNumInputs();
    int numOutputs = dsp.getNumOutputs();

    // Allocate buffers
    std::vector<float> inputBuffer(numInputs, 0.0f);
    std::vector<float> outputBuffer(numOutputs, 0.0f);
    std::vector<float*> inputPtrs(numInputs);
    std::vector<float*> outputPtrs(numOutputs);
    for (int i = 0; i < numInputs; i++) inputPtrs[i] = &inputBuffer[i];
    for (int i = 0; i < numOutputs; i++) outputPtrs[i] = &outputBuffer[i];

    // Output samples
    std::vector<float> output;
    output.reserve(numSamples * numOutputs);

    // Random generator for noise
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    // Find common parameter indices
    int gateParamIdx = dsp.getParamIndex("gate");
    int triggerParamIdx = dsp.getParamIndex("trigger");
    int velocityParamIdx = dsp.getParamIndex("velocity");
    int voltsParamIdx = dsp.getParamIndex("volts");

    // Precompute automation parameter indices
    std::vector<std::pair<int, const ShowcaseAutomation*>> automationIndices;
    for (const auto& automation : showcase.automations) {
        int idx = dsp.getParamIndex(automation.param.c_str());
        if (idx >= 0) {
            automationIndices.push_back({idx, &automation});
        }
    }

    // Precompute trigger event information (param index, start sample, end sample)
    struct TriggerEvent {
        int paramIdx;
        int startSample;
        int endSample;
        float value;
    };
    std::vector<TriggerEvent> triggerEvents;
    for (const auto& trigger : showcase.trigger_sequence) {
        int idx = dsp.getParamIndex(trigger.param.c_str());
        if (idx >= 0) {
            TriggerEvent event;
            event.paramIdx = idx;
            event.startSample = static_cast<int>(trigger.time * sampleRate);
            event.endSample = event.startSample + static_cast<int>(trigger.duration * sampleRate);
            event.value = trigger.value;
            triggerEvents.push_back(event);
        }
    }

    // Track which trigger params need to be reset to 0 when not active
    std::map<int, bool> triggerParamActive;
    for (const auto& event : triggerEvents) {
        triggerParamActive[event.paramIdx] = false;
    }

    float sawPhase = 0.0f;
    float sawFreq = 440.0f / sampleRate;
    int burstPeriod = static_cast<int>(0.5f * sampleRate);  // Burst every 0.5s for effects
    int burstLength = static_cast<int>(0.05f * sampleRate); // 50ms burst

    for (int i = 0; i < numSamples; i++) {
        float currentTime = static_cast<float>(i) / sampleRate;

        // Determine current note state (for instruments)
        bool gateOn = false;
        float currentVolts = 0.0f;
        float currentVelocity = 1.0f;

        for (const auto& note : showcase.notes) {
            if (currentTime >= note.start && currentTime < (note.start + note.duration)) {
                gateOn = true;
                currentVolts = note.volts;
                currentVelocity = note.velocity;
                break;
            }
        }

        // Set gate/trigger/velocity for instruments
        if (gateParamIdx >= 0) {
            dsp.setParamValue(gateParamIdx, gateOn ? 1.0f : 0.0f);
        }
        if (velocityParamIdx >= 0) {
            dsp.setParamValue(velocityParamIdx, gateOn ? currentVelocity : 0.0f);
        }
        if (voltsParamIdx >= 0 && gateOn) {
            dsp.setParamValue(voltsParamIdx, currentVolts);
        }

        // Handle trigger at note start
        if (triggerParamIdx >= 0) {
            bool shouldTrigger = false;
            for (const auto& note : showcase.notes) {
                int noteStartSample = static_cast<int>(note.start * sampleRate);
                if (i == noteStartSample) {
                    shouldTrigger = true;
                    break;
                }
            }
            dsp.setParamValue(triggerParamIdx, shouldTrigger ? 1.0f : 0.0f);
        }

        // Handle trigger_sequence events (for drums and other trigger-based modules)
        // First, mark all trigger params as inactive for this sample
        for (auto& [paramIdx, active] : triggerParamActive) {
            active = false;
        }
        // Check which triggers should be active at this sample
        for (const auto& event : triggerEvents) {
            if (i >= event.startSample && i < event.endSample) {
                dsp.setParamValue(event.paramIdx, event.value);
                triggerParamActive[event.paramIdx] = true;
            }
        }
        // Set inactive trigger params to 0
        for (const auto& [paramIdx, active] : triggerParamActive) {
            if (!active) {
                dsp.setParamValue(paramIdx, 0.0f);
            }
        }

        // Apply automations (linear interpolation)
        for (const auto& [idx, automation] : automationIndices) {
            if (currentTime >= automation->start_time && currentTime <= automation->end_time) {
                float t = (currentTime - automation->start_time) /
                          (automation->end_time - automation->start_time);
                float value = automation->start_value + t * (automation->end_value - automation->start_value);
                dsp.setParamValue(idx, value);
            }
        }

        // Generate input based on module type
        if (numInputs > 0) {
            switch (type) {
                case ModuleType::Filter:
                    // Continuous saw wave
                    inputBuffer[0] = sawPhase * 2.0f - 1.0f;
                    sawPhase += sawFreq;
                    if (sawPhase >= 1.0f) sawPhase -= 1.0f;
                    break;

                case ModuleType::Resonator:
                    // Repeated noise bursts
                    {
                        int burstPeriodRes = static_cast<int>(0.3f * sampleRate);
                        int burstLengthRes = static_cast<int>(0.02f * sampleRate);
                        int posInCycle = i % burstPeriodRes;
                        if (posInCycle < burstLengthRes) {
                            inputBuffer[0] = noiseDist(rng) * 0.8f;
                        } else {
                            inputBuffer[0] = 0.0f;
                        }
                    }
                    break;

                case ModuleType::Effect:
                    // Repeated bursts for effects
                    {
                        int posInCycle = i % burstPeriod;
                        if (posInCycle < burstLength) {
                            inputBuffer[0] = noiseDist(rng) * 0.5f;
                        } else {
                            inputBuffer[0] = 0.0f;
                        }
                    }
                    break;

                case ModuleType::Instrument:
                case ModuleType::Utility:
                default:
                    inputBuffer[0] = 0.0f;
                    break;
            }
        }

        // Process one sample
        dsp.compute(1, inputPtrs.data(), outputPtrs.data());

        // Collect output
        for (int ch = 0; ch < numOutputs; ch++) {
            output.push_back(outputBuffer[ch]);
        }
    }

    return output;
}

// ============================================================================
// Command Line Parsing
// ============================================================================

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  --module NAME       Module to render (required)\n"
              << "  --output FILE       Output WAV file (default: output.wav)\n"
              << "  --duration SECS     Duration in seconds (default: 2.0)\n"
              << "  --sample-rate RATE  Sample rate (default: 48000)\n"
              << "  --param NAME=VALUE  Set parameter value (can repeat)\n"
              << "  --scenario NAME     Use a pre-defined test scenario\n"
              << "  --showcase          Render showcase audio with multiple notes and automations\n"
              << "  --showcase-config   Custom config file for showcase (overrides test_config.json)\n"
              << "  --list-modules      List available modules\n"
              << "  --list-params       List parameters for module\n"
              << "  --list-scenarios    List test scenarios for module\n"
              << "  --show-config       Show module test configuration\n"
              << "  --no-auto-gate      Disable automatic gate/trigger handling\n"
              << "  --help              Show this help\n\n"
              << "Examples:\n"
              << "  " << programName << " --module MoogLPF --list-params\n"
              << "  " << programName << " --module ChaosFlute --list-scenarios\n"
              << "  " << programName << " --module ChaosFlute --scenario high_chaos\n"
              << "  " << programName << " --module ChaosFlute --showcase --output showcase.wav\n"
              << "  " << programName << " --module TheAbyss --output test.wav --param decay=0.8\n";
}

struct Options {
    std::string moduleName;
    std::string outputFile = "output.wav";
    std::string scenario;  // Named scenario from test_config.json
    std::string showcaseConfigFile;  // Override config file for showcase
    float duration = 2.0f;
    int sampleRate = 48000;
    std::map<std::string, float> params;
    bool listModules = false;
    bool listParams = false;
    bool listScenarios = false;
    bool showConfig = false;
    bool noAutoGate = false;  // Disable automatic gate/trigger handling
    bool showcase = false;    // Render showcase audio with multiple notes/automations
};

bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        if (arg == "--list-modules") {
            opts.listModules = true;
            continue;
        }
        if (arg == "--list-params") {
            opts.listParams = true;
            continue;
        }
        if (arg == "--list-scenarios") {
            opts.listScenarios = true;
            continue;
        }
        if (arg == "--show-config") {
            opts.showConfig = true;
            continue;
        }
        if (arg == "--no-auto-gate") {
            opts.noAutoGate = true;
            continue;
        }
        if (arg == "--showcase") {
            opts.showcase = true;
            continue;
        }
        if (arg == "--showcase-config" && i + 1 < argc) {
            opts.showcaseConfigFile = argv[++i];
            continue;
        }
        if (arg == "--scenario" && i + 1 < argc) {
            opts.scenario = argv[++i];
            continue;
        }
        if (arg == "--module" && i + 1 < argc) {
            opts.moduleName = argv[++i];
            continue;
        }
        if (arg == "--output" && i + 1 < argc) {
            opts.outputFile = argv[++i];
            continue;
        }
        if (arg == "--duration" && i + 1 < argc) {
            opts.duration = std::stof(argv[++i]);
            continue;
        }
        if (arg == "--sample-rate" && i + 1 < argc) {
            opts.sampleRate = std::stoi(argv[++i]);
            continue;
        }
        if (arg == "--param" && i + 1 < argc) {
            std::string param = argv[++i];
            size_t eq = param.find('=');
            if (eq != std::string::npos) {
                std::string name = param.substr(0, eq);
                float value = std::stof(param.substr(eq + 1));
                opts.params[name] = value;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        return 1;
    }

    // List modules
    if (opts.listModules) {
        std::cout << "Available modules:\n";
        for (const auto& name : getModuleNames()) {
            std::cout << "  " << name << "\n";
        }
        return 0;
    }

    // Check module name
    if (opts.moduleName.empty()) {
        std::cerr << "Error: --module is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // Load module test config
    ModuleTestConfig config = loadModuleConfig(opts.moduleName);

    // Handle --show-config
    if (opts.showConfig) {
        std::cout << "Test configuration for " << opts.moduleName << ":\n";
        std::cout << "  Type: " << module_type_to_string(config.module_type) << "\n";
        std::cout << "  Skip audio tests: " << (config.skip_audio_tests ? "yes" : "no") << "\n";
        if (config.skip_audio_tests && !config.skip_reason.empty()) {
            std::cout << "  Skip reason: " << config.skip_reason << "\n";
        }
        if (!config.description.empty()) {
            std::cout << "  Description: " << config.description << "\n";
        }
        std::cout << "\n  Quality thresholds:\n";
        std::cout << "    THD max: " << config.thresholds.thd_max_percent << "%\n";
        std::cout << "    Clipping max: " << config.thresholds.clipping_max_percent << "%\n";
        std::cout << "    HNR min: " << config.thresholds.hnr_min_db << " dB\n";
        std::cout << "    Allow hot signal: " << (config.thresholds.allow_hot_signal ? "yes" : "no") << "\n";
        listScenarios(config);
        return 0;
    }

    // Handle --list-scenarios
    if (opts.listScenarios) {
        listScenarios(config);
        return 0;
    }

    // Create DSP
    auto dsp = createDSP(opts.moduleName);
    if (!dsp) {
        std::cerr << "Error: Unknown module: " << opts.moduleName << "\n";
        std::cerr << "Use --list-modules to see available modules\n";
        return 1;
    }

    // Initialize
    dsp->init(opts.sampleRate);

    // List params
    if (opts.listParams) {
        std::cout << "Parameters for " << opts.moduleName << ":\n";
        int numParams = dsp->getNumParams();
        for (int i = 0; i < numParams; i++) {
            std::cout << "  [" << i << "] " << dsp->getParamPath(i)
                      << " (min=" << dsp->getParamMin(i)
                      << ", max=" << dsp->getParamMax(i)
                      << ", init=" << dsp->getParamInit(i) << ")\n";
        }
        std::cout << "\nInputs: " << dsp->getNumInputs()
                  << ", Outputs: " << dsp->getNumOutputs() << "\n";
        return 0;
    }

    // Find the scenario to use (if any)
    const TestScenario* scenario = nullptr;
    if (!opts.scenario.empty()) {
        for (const auto& s : config.test_scenarios) {
            if (s.name == opts.scenario) {
                scenario = &s;
                break;
            }
        }
        if (!scenario) {
            std::cerr << "Error: Unknown scenario: " << opts.scenario << "\n";
            std::cerr << "Use --list-scenarios to see available scenarios\n";
            return 1;
        }
        std::cout << "Using scenario: " << scenario->name;
        if (!scenario->description.empty()) {
            std::cout << " (" << scenario->description << ")";
        }
        std::cout << "\n";

        // Apply scenario duration if not overridden
        if (opts.duration == 2.0f && scenario->duration != 2.0f) {
            opts.duration = scenario->duration;
        }
    }

    // Apply scenario parameters first
    if (scenario) {
        for (const auto& kv : scenario->parameters) {
            int idx = dsp->getParamIndex(kv.first.c_str());
            if (idx >= 0) {
                dsp->setParamValue(idx, kv.second);
                std::cout << "Set (scenario) " << kv.first << " = " << kv.second << "\n";
            }
        }
    }

    // Set command-line parameters (override scenario)
    for (const auto& kv : opts.params) {
        int idx = dsp->getParamIndex(kv.first.c_str());
        if (idx >= 0) {
            dsp->setParamValue(idx, kv.second);
            std::cout << "Set " << kv.first << " = " << kv.second << "\n";
        } else {
            std::cerr << "Warning: Unknown parameter: " << kv.first << "\n";
        }
    }

    // Warn if this module skips audio tests
    if (config.skip_audio_tests) {
        std::cout << "\nNote: This module has skip_audio_tests=true";
        if (!config.skip_reason.empty()) {
            std::cout << " (" << config.skip_reason << ")";
        }
        std::cout << "\n";
    }

    ModuleType type = config.module_type;
    std::vector<float> samples;

    // Render using showcase mode or standard mode
    if (opts.showcase) {
        // Get showcase config (from custom file, test_config.json, or generate default)
        ShowcaseConfig showcase;
        if (!opts.showcaseConfigFile.empty()) {
            // Load from custom config file
            ModuleTestConfig customConfig = load_module_config(opts.showcaseConfigFile, opts.moduleName);
            if (customConfig.showcase.enabled) {
                showcase = customConfig.showcase;
                type = customConfig.module_type;  // Also use the type from custom config
                std::cout << "Using showcase config from " << opts.showcaseConfigFile << "\n";
            } else {
                showcase = getDefaultShowcaseConfig(type);
                std::cout << "Custom config has no showcase, using default\n";
            }
        } else if (config.showcase.enabled) {
            showcase = config.showcase;
            std::cout << "Using showcase config from test_config.json\n";
        } else {
            showcase = getDefaultShowcaseConfig(type);
            std::cout << "Using default showcase config for " << module_type_to_string(type) << "\n";
        }

        std::cout << "Rendering showcase for " << opts.moduleName << " ("
                  << showcase.duration << "s at " << opts.sampleRate << "Hz)...\n";
        std::cout << "  Notes: " << showcase.notes.size() << "\n";
        std::cout << "  Automations: " << showcase.automations.size() << "\n";
        std::cout << "  Triggers: " << showcase.trigger_sequence.size() << "\n";

        samples = renderShowcaseAudio(*dsp, opts.sampleRate, showcase, type);
    } else {
        // Standard render
        std::cout << "Rendering " << opts.moduleName << " for " << opts.duration
                  << "s at " << opts.sampleRate << "Hz...\n";

        samples = renderAudio(*dsp, opts.sampleRate, opts.duration, type, opts.noAutoGate, scenario);
    }

    // Analyze audio for distortion/clipping
    float peakAbs = 0.0f;
    float sumSquares = 0.0f;
    int clipCount = 0;
    const float clipThreshold = 0.99f;  // Consider clipped if >= 99% of max

    for (float sample : samples) {
        float absSample = std::abs(sample);
        peakAbs = std::max(peakAbs, absSample);
        sumSquares += sample * sample;
        if (absSample >= clipThreshold) {
            clipCount++;
        }
    }

    float rms = std::sqrt(sumSquares / samples.size());
    float crestFactor = (rms > 0.001f) ? (peakAbs / rms) : 0.0f;
    float clipPercent = 100.0f * clipCount / samples.size();

    std::cout << "\n=== Audio Analysis ===\n";
    std::cout << "Peak amplitude: " << peakAbs << " (" << (20.0f * std::log10(std::max(peakAbs, 0.0001f))) << " dB)\n";
    std::cout << "RMS level: " << rms << " (" << (20.0f * std::log10(std::max(rms, 0.0001f))) << " dB)\n";
    std::cout << "Crest factor: " << crestFactor << " (" << (20.0f * std::log10(std::max(crestFactor, 0.0001f))) << " dB)\n";
    std::cout << "Clipped samples: " << clipCount << " (" << clipPercent << "%)\n";

    float clipThresholdPercent = config.thresholds.effective_clipping_max();
    if (clipPercent > clipThresholdPercent) {
        std::cout << "WARNING: Clipping exceeds threshold (" << clipPercent << "% > "
                  << clipThresholdPercent << "%)!\n";
    } else if (peakAbs > 1.0f) {
        std::cout << "WARNING: Output exceeds unity gain (peak=" << peakAbs << ")!\n";
    }
    std::cout << "Clipping threshold: " << clipThresholdPercent << "%";
    if (config.thresholds.allow_hot_signal) {
        std::cout << " (hot signal module)";
    }
    std::cout << "\n======================\n\n";

    // Write WAV
    int numChannels = dsp->getNumOutputs();
    if (writeWav(opts.outputFile, samples, opts.sampleRate, numChannels)) {
        std::cout << "Wrote " << opts.outputFile << " ("
                  << samples.size() / numChannels << " samples, "
                  << numChannels << " channels)\n";
    } else {
        return 1;
    }

    return 0;
}
