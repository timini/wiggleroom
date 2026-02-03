/************************************************************************
 FAUST Architecture File for VCV Rack

 This architecture generates a C++ class suitable for use with VCV Rack
 modules. It provides indexed parameter access via MapUI.

 IMPORTANT: Define FAUST_MODULE_NAME before including the generated header
 to create a unique namespace and avoid ODR violations when multiple
 Faust modules are linked together.

 Example in your module:
   #define FAUST_MODULE_NAME MoogLPF
   #include "moog_lpf.hpp"

 Usage:
   faust -i -a vcvrack.cpp mydsp.dsp -o MyDSP.hpp

 The generated class provides:
   - init(int sample_rate)
   - compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs)
   - getNumInputs() / getNumOutputs() / getNumParams()
   - setParamValue(int index, FAUSTFLOAT value)
   - getParamValue(int index)
   - getParamPath(int index) / getParamMin/Max/Init(int index)
************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Ensure math functions are in global namespace for Faust-generated code
using std::abs;
using std::acos;
using std::asin;
using std::atan;
using std::atan2;
using std::ceil;
using std::cos;
using std::exp;
using std::fabs;
using std::floor;
using std::fmod;
using std::log;
using std::log10;
using std::pow;
using std::round;
using std::sin;
using std::sqrt;
using std::tan;

// Faust compatibility types
#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

// Helper macros to create unique namespace names
#define FAUST_CONCAT_IMPL(a, b) a##b
#define FAUST_CONCAT(a, b) FAUST_CONCAT_IMPL(a, b)

// Default module name if not defined
#ifndef FAUST_MODULE_NAME
#define FAUST_MODULE_NAME DefaultModule
#endif

// Each Faust file gets its own namespace to avoid ODR violations
namespace FaustGenerated {
namespace FAUST_CONCAT(NS_, FAUST_MODULE_NAME) {

// Import math functions into this namespace
using ::abs;
using ::acos;
using ::asin;
using ::atan;
using ::atan2;
using ::ceil;
using ::cos;
using ::exp;
using ::fabs;
using ::floor;
using ::fmod;
using ::log;
using ::log10;
using ::pow;
using ::round;
using ::sin;
using ::sqrt;
using ::tan;

// Minimal Meta interface (for metadata declarations in DSP)
struct Meta {
    virtual void declare(const char* key, const char* value) = 0;
    virtual ~Meta() = default;
};

// Minimal UI interface for parameter discovery
struct UI {
    virtual ~UI() = default;

    // Box layout (ignored - just for grouping)
    virtual void openTabBox(const char* label) = 0;
    virtual void openHorizontalBox(const char* label) = 0;
    virtual void openVerticalBox(const char* label) = 0;
    virtual void closeBox() = 0;

    // Active widgets (parameters)
    virtual void addButton(const char* label, FAUSTFLOAT* zone) = 0;
    virtual void addCheckButton(const char* label, FAUSTFLOAT* zone) = 0;
    virtual void addVerticalSlider(const char* label, FAUSTFLOAT* zone,
                                   FAUSTFLOAT init, FAUSTFLOAT min,
                                   FAUSTFLOAT max, FAUSTFLOAT step) = 0;
    virtual void addHorizontalSlider(const char* label, FAUSTFLOAT* zone,
                                     FAUSTFLOAT init, FAUSTFLOAT min,
                                     FAUSTFLOAT max, FAUSTFLOAT step) = 0;
    virtual void addNumEntry(const char* label, FAUSTFLOAT* zone,
                            FAUSTFLOAT init, FAUSTFLOAT min,
                            FAUSTFLOAT max, FAUSTFLOAT step) = 0;

    // Passive widgets (outputs/displays - not parameters)
    virtual void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone,
                                       FAUSTFLOAT min, FAUSTFLOAT max) = 0;
    virtual void addVerticalBargraph(const char* label, FAUSTFLOAT* zone,
                                     FAUSTFLOAT min, FAUSTFLOAT max) = 0;

    // Metadata
    virtual void declare(FAUSTFLOAT* zone, const char* key, const char* val) = 0;
};

// MapUI: Provides indexed parameter access for VCV Rack integration
class MapUI : public UI {
private:
    struct ParamInfo {
        FAUSTFLOAT* zone;
        FAUSTFLOAT init;
        FAUSTFLOAT min;
        FAUSTFLOAT max;
        std::string path;
    };

    std::vector<ParamInfo> params;
    std::map<std::string, int> pathToIndex;
    std::vector<std::string> pathStack;

    std::string buildPath(const char* label) {
        std::string path;
        for (const auto& part : pathStack) {
            path += "/" + part;
        }
        path += "/" + std::string(label);
        return path;
    }

    void addParam(const char* label, FAUSTFLOAT* zone,
                 FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max) {
        ParamInfo info;
        info.zone = zone;
        info.init = init;
        info.min = min;
        info.max = max;
        info.path = buildPath(label);

        int index = static_cast<int>(params.size());
        params.push_back(info);
        pathToIndex[info.path] = index;

        // Initialize to default value
        *zone = init;
    }

public:
    MapUI() = default;
    ~MapUI() override = default;

    // Box layout (just track path for parameter naming)
    void openTabBox(const char* label) override { pathStack.push_back(label); }
    void openHorizontalBox(const char* label) override { pathStack.push_back(label); }
    void openVerticalBox(const char* label) override { pathStack.push_back(label); }
    void closeBox() override { if (!pathStack.empty()) pathStack.pop_back(); }

    // Active widgets become parameters
    void addButton(const char* label, FAUSTFLOAT* zone) override {
        addParam(label, zone, 0.0f, 0.0f, 1.0f);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        addParam(label, zone, 0.0f, 0.0f, 1.0f);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone,
                          FAUSTFLOAT init, FAUSTFLOAT min,
                          FAUSTFLOAT max, FAUSTFLOAT step) override {
        addParam(label, zone, init, min, max);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone,
                            FAUSTFLOAT init, FAUSTFLOAT min,
                            FAUSTFLOAT max, FAUSTFLOAT step) override {
        addParam(label, zone, init, min, max);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone,
                    FAUSTFLOAT init, FAUSTFLOAT min,
                    FAUSTFLOAT max, FAUSTFLOAT step) override {
        addParam(label, zone, init, min, max);
    }

    // Passive widgets (outputs) - not added to param list
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}

    // Metadata (ignored for now)
    void declare(FAUSTFLOAT*, const char*, const char*) override {}

    // === Parameter Access API ===

    int getNumParams() const {
        return static_cast<int>(params.size());
    }

    void setParamValue(int index, FAUSTFLOAT value) {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            // Clamp to valid range
            FAUSTFLOAT clamped = std::max(params[index].min,
                                         std::min(params[index].max, value));
            *(params[index].zone) = clamped;
        }
    }

    FAUSTFLOAT getParamValue(int index) const {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            return *(params[index].zone);
        }
        return 0.0f;
    }

    const char* getParamPath(int index) const {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            return params[index].path.c_str();
        }
        return "";
    }

    FAUSTFLOAT getParamMin(int index) const {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            return params[index].min;
        }
        return 0.0f;
    }

    FAUSTFLOAT getParamMax(int index) const {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            return params[index].max;
        }
        return 1.0f;
    }

    FAUSTFLOAT getParamInit(int index) const {
        if (index >= 0 && index < static_cast<int>(params.size())) {
            return params[index].init;
        }
        return 0.0f;
    }

    int getParamIndex(const char* path) const {
        auto it = pathToIndex.find(path);
        if (it != pathToIndex.end()) {
            return it->second;
        }
        // Try matching just the label (last part of path)
        std::string pathStr(path);
        for (size_t i = 0; i < params.size(); i++) {
            const std::string& paramPath = params[i].path;
            size_t lastSlash = paramPath.rfind('/');
            std::string label = (lastSlash != std::string::npos)
                ? paramPath.substr(lastSlash + 1)
                : paramPath;
            if (label == pathStr) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// Faust intrinsics placeholder (math functions)
<<includeIntrinsic>>

// Base DSP class that Faust-generated classes inherit from
class dsp {
public:
    virtual ~dsp() = default;

    virtual int getNumInputs() = 0;
    virtual int getNumOutputs() = 0;

    virtual void buildUserInterface(UI* ui_interface) = 0;

    virtual int getSampleRate() = 0;

    virtual void init(int sample_rate) = 0;
    virtual void instanceInit(int sample_rate) = 0;
    virtual void instanceConstants(int sample_rate) = 0;
    virtual void instanceResetUserInterface() = 0;
    virtual void instanceClear() = 0;

    virtual dsp* clone() = 0;

    virtual void metadata(Meta* m) = 0;

    virtual void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) = 0;
};

// Generated DSP class from Faust
<<includeclass>>

// VCV Rack wrapper class - wraps the generated 'mydsp' class
class VCVRackDSP {
private:
    mydsp dsp;
    MapUI ui;
    int numInputs;
    int numOutputs;

public:
    VCVRackDSP() : numInputs(0), numOutputs(0) {
        numInputs = dsp.getNumInputs();
        numOutputs = dsp.getNumOutputs();
        dsp.buildUserInterface(&ui);
    }

    void init(int sample_rate) {
        dsp.init(sample_rate);
    }

    void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) {
        dsp.compute(count, inputs, outputs);
    }

    // Audio I/O info
    int getNumInputs() const { return numInputs; }
    int getNumOutputs() const { return numOutputs; }

    // Parameter access
    int getNumParams() const { return ui.getNumParams(); }

    void setParamValue(int index, FAUSTFLOAT value) {
        ui.setParamValue(index, value);
    }

    FAUSTFLOAT getParamValue(int index) const {
        return ui.getParamValue(index);
    }

    const char* getParamPath(int index) const {
        return ui.getParamPath(index);
    }

    FAUSTFLOAT getParamMin(int index) const {
        return ui.getParamMin(index);
    }

    FAUSTFLOAT getParamMax(int index) const {
        return ui.getParamMax(index);
    }

    FAUSTFLOAT getParamInit(int index) const {
        return ui.getParamInit(index);
    }

    int getParamIndex(const char* path) const {
        return ui.getParamIndex(path);
    }
};

} // namespace NS_<FAUST_MODULE_NAME>
} // namespace FaustGenerated

// Bring the VCVRackDSP into global scope for easy use (can be disabled)
#ifndef FAUST_NO_GLOBAL_ALIAS
using VCVRackDSP = FaustGenerated::FAUST_CONCAT(NS_, FAUST_MODULE_NAME)::VCVRackDSP;
#endif

// Clean up the module name macro so it can be redefined for the next include
#undef FAUST_MODULE_NAME
