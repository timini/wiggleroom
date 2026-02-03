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

// Standard includes - MUST be before any namespace to avoid GCC 13 issues
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <math.h>
#include <string>
#include <vector>

// Fast math function aliases for Faust -fm arch mode
// These map fast_* functions to standard math functions
// Wrapped in include guard to avoid redefinition when multiple headers are included
#ifndef FAUST_FAST_MATH_DEFINED
#define FAUST_FAST_MATH_DEFINED
inline float fast_acosf(float x) { return std::acos(x); }
inline float fast_asinf(float x) { return std::asin(x); }
inline float fast_atanf(float x) { return std::atan(x); }
inline float fast_atan2f(float y, float x) { return std::atan2(y, x); }
inline float fast_ceilf(float x) { return std::ceil(x); }
inline float fast_cosf(float x) { return std::cos(x); }
inline float fast_coshf(float x) { return std::cosh(x); }
inline float fast_expf(float x) { return std::exp(x); }
inline float fast_exp2f(float x) { return std::exp2(x); }
inline float fast_exp10f(float x) { return std::pow(10.0f, x); }
inline float fast_fabsf(float x) { return std::fabs(x); }
inline float fast_floorf(float x) { return std::floor(x); }
inline float fast_fmodf(float x, float y) { return std::fmod(x, y); }
inline float fast_logf(float x) { return std::log(x); }
inline float fast_log2f(float x) { return std::log2(x); }
inline float fast_log10f(float x) { return std::log10(x); }
inline float fast_powf(float x, float y) { return std::pow(x, y); }
inline float fast_remainderf(float x, float y) { return std::remainder(x, y); }
inline float fast_rintf(float x) { return std::rint(x); }
inline float fast_roundf(float x) { return std::round(x); }
inline float fast_sinf(float x) { return std::sin(x); }
inline float fast_sinhf(float x) { return std::sinh(x); }
inline float fast_sqrtf(float x) { return std::sqrt(x); }
inline float fast_tanf(float x) { return std::tan(x); }
inline float fast_tanhf(float x) { return std::tanh(x); }

// Double precision versions
inline double fast_acos(double x) { return std::acos(x); }
inline double fast_asin(double x) { return std::asin(x); }
inline double fast_atan(double x) { return std::atan(x); }
inline double fast_atan2(double y, double x) { return std::atan2(y, x); }
inline double fast_ceil(double x) { return std::ceil(x); }
inline double fast_cos(double x) { return std::cos(x); }
inline double fast_cosh(double x) { return std::cosh(x); }
inline double fast_exp(double x) { return std::exp(x); }
inline double fast_exp2(double x) { return std::exp2(x); }
inline double fast_exp10(double x) { return std::pow(10.0, x); }
inline double fast_fabs(double x) { return std::fabs(x); }
inline double fast_floor(double x) { return std::floor(x); }
inline double fast_fmod(double x, double y) { return std::fmod(x, y); }
inline double fast_log(double x) { return std::log(x); }
inline double fast_log2(double x) { return std::log2(x); }
inline double fast_log10(double x) { return std::log10(x); }
inline double fast_pow(double x, double y) { return std::pow(x, y); }
inline double fast_remainder(double x, double y) { return std::remainder(x, y); }
inline double fast_rint(double x) { return std::rint(x); }
inline double fast_round(double x) { return std::round(x); }
inline double fast_sin(double x) { return std::sin(x); }
inline double fast_sinh(double x) { return std::sinh(x); }
inline double fast_sqrt(double x) { return std::sqrt(x); }
inline double fast_tan(double x) { return std::tan(x); }
inline double fast_tanh(double x) { return std::tanh(x); }
#endif // FAUST_FAST_MATH_DEFINED

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
