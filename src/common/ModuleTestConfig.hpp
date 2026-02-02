#pragma once

/**
 * ModuleTestConfig - Configuration for module-level testing
 *
 * Each module can have a test_config.json in its directory that specifies:
 * - Module type (instrument, filter, effect, resonator, utility)
 * - Quality thresholds (THD, clipping, HNR limits)
 * - Test scenarios with specific parameter combinations
 * - Whether to skip audio tests (for utilities like LFOs)
 *
 * This header provides a simple JSON parser and config structures
 * that can be used by both the test framework and the Faust render tool.
 */

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace WiggleRoom {
namespace TestConfig {

// Module type classification for test input generation
enum class ModuleType {
    Instrument,  // Generates sound from gate/trigger (no audio input)
    Filter,      // Needs continuous audio input (saw wave)
    Effect,      // Needs audio input (short burst then silence)
    Resonator,   // Needs impulse/noise burst to excite resonances
    Utility      // Non-audio module (LFO, clock, sequencer) - skip audio tests
};

// Quality thresholds for audio analysis
struct QualityThresholds {
    float thd_max_percent = 15.0f;       // Maximum acceptable THD
    float clipping_max_percent = 1.0f;   // Maximum acceptable clipping ratio
    float hnr_min_db = 0.0f;             // Minimum harmonic-to-noise ratio
    bool allow_hot_signal = false;       // Allow higher output levels (wavefolders, etc.)

    // Derived thresholds for hot signal modules
    float effective_clipping_max() const {
        return allow_hot_signal ? 15.0f : clipping_max_percent;
    }
};

// A single test scenario with specific parameters
struct TestScenario {
    std::string name = "default";
    float duration = 2.0f;
    std::map<std::string, float> parameters;
    std::string description;
    std::string trigger_param;  // Custom trigger parameter (e.g., "bd_trig" for drums)
};

// Parameter sweep configuration
struct ParameterSweepConfig {
    std::vector<std::string> exclude;  // Parameters to skip in sweeps
    int steps = 10;                    // Number of steps per parameter
};

// Showcase note for multi-note rendering
struct ShowcaseNote {
    float start = 0.0f;        // Start time in seconds
    float duration = 2.0f;     // Note duration in seconds
    float volts = 0.0f;        // V/Oct pitch (0V = C4)
    float velocity = 1.0f;     // Note velocity (0-1)
};

// Showcase automation for parameter sweeps
struct ShowcaseAutomation {
    std::string param;         // Parameter name
    float start_time = 0.0f;   // Start time in seconds
    float end_time = 10.0f;    // End time in seconds
    float start_value = 0.0f;  // Starting value
    float end_value = 1.0f;    // Ending value
};

// Showcase trigger event for drum machines and trigger-based modules
struct ShowcaseTrigger {
    float time = 0.0f;         // Trigger time in seconds
    std::string param;         // Trigger parameter name (e.g., "bd_trig")
    float value = 10.0f;       // Trigger value (usually 10V for VCV)
    float duration = 0.01f;    // Trigger pulse duration in seconds
};

// Showcase configuration for comprehensive audio rendering
struct ShowcaseConfig {
    bool enabled = false;              // Whether showcase config exists
    float duration = 10.0f;            // Total duration in seconds
    std::vector<ShowcaseNote> notes;   // Notes to play
    std::vector<ShowcaseAutomation> automations;  // Parameter automations
    std::vector<ShowcaseTrigger> trigger_sequence;  // Trigger events for drums
};

// Complete module test configuration
struct ModuleTestConfig {
    // Module identification
    std::string module_name;
    ModuleType module_type = ModuleType::Instrument;

    // Whether to skip audio tests entirely (for utilities)
    bool skip_audio_tests = false;
    std::string skip_reason;

    // Quality thresholds
    QualityThresholds thresholds;

    // Test scenarios
    std::vector<TestScenario> test_scenarios;

    // Parameter sweep config
    ParameterSweepConfig param_sweep;

    // Showcase config for comprehensive audio rendering
    ShowcaseConfig showcase;

    // Description (optional)
    std::string description;

    // Returns default config for a module
    static ModuleTestConfig defaultConfig(const std::string& name) {
        ModuleTestConfig config;
        config.module_name = name;
        config.module_type = ModuleType::Instrument;
        config.thresholds = QualityThresholds{};

        TestScenario defaultScenario;
        defaultScenario.name = "default";
        defaultScenario.duration = 2.0f;
        config.test_scenarios.push_back(defaultScenario);

        return config;
    }
};

// Simple JSON value representation
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;

    bool bool_val = false;
    double number_val = 0.0;
    std::string string_val;
    std::vector<JsonValue> array_val;
    std::map<std::string, JsonValue> object_val;

    bool is_null() const { return type == Null; }
    bool is_bool() const { return type == Bool; }
    bool is_number() const { return type == Number; }
    bool is_string() const { return type == String; }
    bool is_array() const { return type == Array; }
    bool is_object() const { return type == Object; }

    // Getters with defaults
    bool get_bool(bool def = false) const {
        return is_bool() ? bool_val : def;
    }
    double get_number(double def = 0.0) const {
        return is_number() ? number_val : def;
    }
    const std::string& get_string(const std::string& def = "") const {
        static std::string empty;
        return is_string() ? string_val : (def.empty() ? empty : def);
    }

    // Object access
    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null_val;
        if (!is_object()) return null_val;
        auto it = object_val.find(key);
        return it != object_val.end() ? it->second : null_val;
    }

    bool has(const std::string& key) const {
        return is_object() && object_val.count(key) > 0;
    }
};

// Simple recursive descent JSON parser
class JsonParser {
    const std::string& input;
    size_t pos = 0;

    void skip_whitespace() {
        while (pos < input.size() && std::isspace(input[pos])) pos++;
    }

    bool match(char c) {
        skip_whitespace();
        if (pos < input.size() && input[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    bool expect(char c) {
        if (!match(c)) {
            return false;
        }
        return true;
    }

    JsonValue parse_string() {
        JsonValue val;
        val.type = JsonValue::String;
        pos++;  // skip opening quote

        while (pos < input.size() && input[pos] != '"') {
            if (input[pos] == '\\' && pos + 1 < input.size()) {
                pos++;
                switch (input[pos]) {
                    case 'n': val.string_val += '\n'; break;
                    case 't': val.string_val += '\t'; break;
                    case 'r': val.string_val += '\r'; break;
                    case '"': val.string_val += '"'; break;
                    case '\\': val.string_val += '\\'; break;
                    default: val.string_val += input[pos]; break;
                }
            } else {
                val.string_val += input[pos];
            }
            pos++;
        }
        if (pos < input.size()) pos++;  // skip closing quote
        return val;
    }

    JsonValue parse_number() {
        JsonValue val;
        val.type = JsonValue::Number;

        std::string num_str;
        if (input[pos] == '-') {
            num_str += '-';
            pos++;
        }

        while (pos < input.size() && (std::isdigit(input[pos]) ||
               input[pos] == '.' || input[pos] == 'e' || input[pos] == 'E' ||
               input[pos] == '+' || input[pos] == '-')) {
            num_str += input[pos++];
        }

        val.number_val = std::stod(num_str);
        return val;
    }

    JsonValue parse_value() {
        skip_whitespace();

        if (pos >= input.size()) {
            return JsonValue{};
        }

        char c = input[pos];

        // String
        if (c == '"') {
            return parse_string();
        }

        // Number
        if (c == '-' || std::isdigit(c)) {
            return parse_number();
        }

        // Object
        if (c == '{') {
            pos++;
            JsonValue obj;
            obj.type = JsonValue::Object;

            skip_whitespace();
            if (pos < input.size() && input[pos] == '}') {
                pos++;
                return obj;
            }

            while (true) {
                skip_whitespace();
                if (pos >= input.size() || input[pos] != '"') break;

                JsonValue key = parse_string();
                skip_whitespace();
                if (!expect(':')) break;

                obj.object_val[key.string_val] = parse_value();

                skip_whitespace();
                if (pos >= input.size()) break;
                if (input[pos] == '}') {
                    pos++;
                    break;
                }
                if (input[pos] == ',') {
                    pos++;
                }
            }
            return obj;
        }

        // Array
        if (c == '[') {
            pos++;
            JsonValue arr;
            arr.type = JsonValue::Array;

            skip_whitespace();
            if (pos < input.size() && input[pos] == ']') {
                pos++;
                return arr;
            }

            while (true) {
                arr.array_val.push_back(parse_value());

                skip_whitespace();
                if (pos >= input.size()) break;
                if (input[pos] == ']') {
                    pos++;
                    break;
                }
                if (input[pos] == ',') {
                    pos++;
                }
            }
            return arr;
        }

        // Boolean true
        if (input.substr(pos, 4) == "true") {
            pos += 4;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.bool_val = true;
            return val;
        }

        // Boolean false
        if (input.substr(pos, 5) == "false") {
            pos += 5;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.bool_val = false;
            return val;
        }

        // Null
        if (input.substr(pos, 4) == "null") {
            pos += 4;
            return JsonValue{};
        }

        return JsonValue{};
    }

public:
    explicit JsonParser(const std::string& s) : input(s) {}

    JsonValue parse() {
        return parse_value();
    }
};

// Parse JSON string into JsonValue
inline JsonValue parse_json(const std::string& json_str) {
    JsonParser parser(json_str);
    return parser.parse();
}

// Load JSON from file
inline JsonValue load_json_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return JsonValue{};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_json(buffer.str());
}

// Convert string to ModuleType
inline ModuleType string_to_module_type(const std::string& s) {
    if (s == "instrument") return ModuleType::Instrument;
    if (s == "filter") return ModuleType::Filter;
    if (s == "effect") return ModuleType::Effect;
    if (s == "resonator") return ModuleType::Resonator;
    if (s == "utility") return ModuleType::Utility;
    return ModuleType::Instrument;  // default
}

// Convert ModuleType to string
inline std::string module_type_to_string(ModuleType t) {
    switch (t) {
        case ModuleType::Instrument: return "instrument";
        case ModuleType::Filter: return "filter";
        case ModuleType::Effect: return "effect";
        case ModuleType::Resonator: return "resonator";
        case ModuleType::Utility: return "utility";
    }
    return "instrument";
}

// Load ModuleTestConfig from JSON value
inline ModuleTestConfig config_from_json(const JsonValue& json,
                                          const std::string& module_name) {
    ModuleTestConfig config;
    config.module_name = module_name;

    // Module type
    if (json.has("module_type")) {
        config.module_type = string_to_module_type(json["module_type"].get_string());
    }

    // Skip audio tests
    if (json.has("skip_audio_tests")) {
        config.skip_audio_tests = json["skip_audio_tests"].get_bool();
    }
    if (json.has("skip_reason")) {
        config.skip_reason = json["skip_reason"].get_string();
    }

    // Description
    if (json.has("description")) {
        config.description = json["description"].get_string();
    }

    // Quality thresholds
    if (json.has("quality_thresholds")) {
        const auto& qt = json["quality_thresholds"];
        if (qt.has("thd_max_percent")) {
            config.thresholds.thd_max_percent = static_cast<float>(qt["thd_max_percent"].get_number());
        }
        if (qt.has("clipping_max_percent")) {
            config.thresholds.clipping_max_percent = static_cast<float>(qt["clipping_max_percent"].get_number());
        }
        if (qt.has("hnr_min_db")) {
            config.thresholds.hnr_min_db = static_cast<float>(qt["hnr_min_db"].get_number());
        }
        if (qt.has("allow_hot_signal")) {
            config.thresholds.allow_hot_signal = qt["allow_hot_signal"].get_bool();
        }
    }

    // Test scenarios
    if (json.has("test_scenarios") && json["test_scenarios"].is_array()) {
        config.test_scenarios.clear();
        for (const auto& scenario_json : json["test_scenarios"].array_val) {
            TestScenario scenario;
            if (scenario_json.has("name")) {
                scenario.name = scenario_json["name"].get_string();
            }
            if (scenario_json.has("duration")) {
                scenario.duration = static_cast<float>(scenario_json["duration"].get_number());
            }
            if (scenario_json.has("description")) {
                scenario.description = scenario_json["description"].get_string();
            }
            if (scenario_json.has("parameters") && scenario_json["parameters"].is_object()) {
                for (const auto& kv : scenario_json["parameters"].object_val) {
                    scenario.parameters[kv.first] = static_cast<float>(kv.second.get_number());
                }
            }
            if (scenario_json.has("trigger_param")) {
                scenario.trigger_param = scenario_json["trigger_param"].get_string();
            }
            config.test_scenarios.push_back(scenario);
        }
    }

    // If no scenarios defined, add default
    if (config.test_scenarios.empty()) {
        TestScenario defaultScenario;
        defaultScenario.name = "default";
        defaultScenario.duration = 2.0f;
        config.test_scenarios.push_back(defaultScenario);
    }

    // Parameter sweep config
    if (json.has("parameter_sweeps")) {
        const auto& ps = json["parameter_sweeps"];
        if (ps.has("exclude") && ps["exclude"].is_array()) {
            for (const auto& ex : ps["exclude"].array_val) {
                config.param_sweep.exclude.push_back(ex.get_string());
            }
        }
        if (ps.has("steps")) {
            config.param_sweep.steps = static_cast<int>(ps["steps"].get_number());
        }
    }

    // Showcase config
    if (json.has("showcase")) {
        const auto& sc = json["showcase"];
        config.showcase.enabled = true;

        if (sc.has("duration")) {
            config.showcase.duration = static_cast<float>(sc["duration"].get_number());
        }

        // Parse notes array
        if (sc.has("notes") && sc["notes"].is_array()) {
            for (const auto& note_json : sc["notes"].array_val) {
                ShowcaseNote note;
                if (note_json.has("start")) {
                    note.start = static_cast<float>(note_json["start"].get_number());
                }
                if (note_json.has("duration")) {
                    note.duration = static_cast<float>(note_json["duration"].get_number());
                }
                if (note_json.has("volts")) {
                    note.volts = static_cast<float>(note_json["volts"].get_number());
                }
                if (note_json.has("velocity")) {
                    note.velocity = static_cast<float>(note_json["velocity"].get_number());
                }
                config.showcase.notes.push_back(note);
            }
        }

        // Parse automations array
        if (sc.has("automations") && sc["automations"].is_array()) {
            for (const auto& auto_json : sc["automations"].array_val) {
                ShowcaseAutomation automation;
                if (auto_json.has("param")) {
                    automation.param = auto_json["param"].get_string();
                }
                if (auto_json.has("start_time")) {
                    automation.start_time = static_cast<float>(auto_json["start_time"].get_number());
                }
                if (auto_json.has("end_time")) {
                    automation.end_time = static_cast<float>(auto_json["end_time"].get_number());
                }
                if (auto_json.has("start_value")) {
                    automation.start_value = static_cast<float>(auto_json["start_value"].get_number());
                }
                if (auto_json.has("end_value")) {
                    automation.end_value = static_cast<float>(auto_json["end_value"].get_number());
                }
                config.showcase.automations.push_back(automation);
            }
        }

        // Parse trigger_sequence array for drum machines
        if (sc.has("trigger_sequence") && sc["trigger_sequence"].is_array()) {
            for (const auto& trig_json : sc["trigger_sequence"].array_val) {
                ShowcaseTrigger trigger;
                if (trig_json.has("time")) {
                    trigger.time = static_cast<float>(trig_json["time"].get_number());
                }
                if (trig_json.has("param")) {
                    trigger.param = trig_json["param"].get_string();
                }
                if (trig_json.has("value")) {
                    trigger.value = static_cast<float>(trig_json["value"].get_number());
                }
                if (trig_json.has("duration")) {
                    trigger.duration = static_cast<float>(trig_json["duration"].get_number());
                }
                config.showcase.trigger_sequence.push_back(trigger);
            }
        }
    }

    return config;
}

// Load module config from file path
inline ModuleTestConfig load_module_config(const std::string& config_path,
                                            const std::string& module_name) {
    JsonValue json = load_json_file(config_path);
    if (json.is_null()) {
        return ModuleTestConfig::defaultConfig(module_name);
    }
    return config_from_json(json, module_name);
}

}  // namespace TestConfig
}  // namespace WiggleRoom
