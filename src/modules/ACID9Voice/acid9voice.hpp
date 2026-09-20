/* ------------------------------------------------------------
name: "acid9voice"
Code generated with Faust 2.85.9 (https://faust.grame.fr)
Compilation options: -a faust/vcvrack.cpp -lang cpp -i -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __mydsp_H__
#define  __mydsp_H__

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
#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

/* link with : "" */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS 
#define FAUSTCLASS mydsp
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class mydspSIG0 {
	
  private:
	
	int iVec7[2];
	int iRec14[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsmydspSIG0() {
		return 0;
	}
	int getNumOutputsmydspSIG0() {
		return 1;
	}
	
	void instanceInitmydspSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			iVec7[l16] = 0;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			iRec14[l17] = 0;
		}
	}
	
	void fillmydspSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec7[0] = 1;
			iRec14[0] = (iVec7[1] + iRec14[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec14[0]));
			iVec7[1] = iVec7[0];
			iRec14[1] = iRec14[0];
		}
	}

};

static mydspSIG0* newmydspSIG0() { return (mydspSIG0*)new mydspSIG0(); }
static void deletemydspSIG0(mydspSIG0* dsp) { delete dsp; }

static float mydsp_faustpower2_f(float value) {
	return value * value;
}
static float ftbl0mydspSIG0[65536];

class mydsp : public dsp {
	
 private:
	
	int iVec0[2];
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	float fRec1[2];
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	FAUSTFLOAT fHslider3;
	float fConst3;
	FAUSTFLOAT fHslider4;
	FAUSTFLOAT fHslider5;
	FAUSTFLOAT fHslider6;
	float fRec5[2];
	float fRec3[2];
	float fConst4;
	float fRec6[2];
	float fVec1[2];
	int IOTA0;
	float fVec2[4096];
	float fConst5;
	FAUSTFLOAT fHslider7;
	float fRec7[2];
	float fRec8[2];
	float fRec10[2];
	float fVec3[2];
	float fVec4[4096];
	float fRec11[2];
	float fRec13[2];
	float fVec5[2];
	float fVec6[4096];
	FAUSTFLOAT fHslider8;
	FAUSTFLOAT fHslider9;
	float fConst6;
	float fRec15[2];
	float fRec16[2];
	float fVec8[2];
	float fVec9[4096];
	FAUSTFLOAT fHslider10;
	float fRec17[2];
	float fConst7;
	FAUSTFLOAT fHslider11;
	float fRec18[2];
	FAUSTFLOAT fHslider12;
	FAUSTFLOAT fHslider13;
	float fConst8;
	float fConst9;
	float fConst10;
	float fConst11;
	FAUSTFLOAT fHslider14;
	float fRec20[2];
	int iVec10[2];
	int iRec19[2];
	FAUSTFLOAT fHslider15;
	float fConst12;
	int iRec21[2];
	float fConst13;
	FAUSTFLOAT fHslider16;
	float fRec22[2];
	float fRec2[3];
	FAUSTFLOAT fHslider17;
	float fRec23[2];
	float fRec25[3];
	float fRec24[3];
	float fConst14;
	float fConst15;
	float fConst16;
	FAUSTFLOAT fHslider18;
	FAUSTFLOAT fHslider19;
	float fConst17;
	FAUSTFLOAT fHslider20;
	FAUSTFLOAT fHslider21;
	float fVec11[524288];
	float fRec27[3];
	float fRec26[2];
	float fVec12[2];
	float fRec0[2];
	float fRec29[3];
	float fRec31[3];
	float fRec30[3];
	FAUSTFLOAT fHslider22;
	float fConst18;
	float fVec13[524288];
	float fRec33[3];
	float fRec32[2];
	float fVec14[2];
	float fRec28[2];
	
 public:
	mydsp() {
	}
	
	mydsp(const mydsp&) = default;
	
	virtual ~mydsp() = default;
	
	mydsp& operator=(const mydsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-a faust/vcvrack.cpp -lang cpp -i -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LGPL with exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "acid9voice.dsp");
		m->declare("filters.lib/dcblocker:author", "Julius O. Smith III");
		m->declare("filters.lib/dcblocker:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/dcblocker:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/highpass:author", "Julius O. Smith III");
		m->declare("filters.lib/highpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/resonlp:author", "Julius O. Smith III");
		m->declare("filters.lib/resonlp:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/resonlp:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("filters.lib/zero:author", "Julius O. Smith III");
		m->declare("filters.lib/zero:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/zero:license", "MIT-style STK-4.3 license");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "acid9voice");
		m->declare("oscillators.lib/lf_sawpos:author", "Bart Brouns, revised by Stéphane Letz");
		m->declare("oscillators.lib/lf_sawpos:licence", "STK-4.3");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/saw2ptr:author", "Julius O. Smith III");
		m->declare("oscillators.lib/saw2ptr:license", "STK-4.3");
		m->declare("oscillators.lib/sawN:author", "Julius O. Smith III");
		m->declare("oscillators.lib/sawN:license", "STK-4.3");
		m->declare("oscillators.lib/version", "1.7.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 3;
	}
	
	static void classInit(int sample_rate) {
		mydspSIG0* sig0 = newmydspSIG0();
		sig0->instanceInitmydspSIG0(sample_rate);
		sig0->fillmydspSIG0(65536, ftbl0mydspSIG0);
		deletemydspSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 1.0f / fConst0;
		fConst4 = 0.25f * fConst0;
		fConst5 = 0.5f * fConst0;
		fConst6 = 0.5f / fConst0;
		fConst7 = 3.1415927f / fConst0;
		fConst8 = std::max<float>(1.0f, 0.008f * fConst0);
		fConst9 = 1.0f / fConst8;
		fConst10 = std::exp(-(5e+02f / fConst0));
		fConst11 = 1.0f - fConst10;
		fConst12 = 1.0f / std::max<float>(1.0f, 0.05f * fConst0);
		fConst13 = 0.45f * fConst0;
		fConst14 = std::max<float>(1.0f, 0.15f * fConst0);
		fConst15 = 0.3f / fConst14;
		fConst16 = 1.0f / fConst14;
		fConst17 = 0.001f * fConst0;
		fConst18 = 0.00105f * fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider5 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider6 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider7 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider8 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider9 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider10 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider11 = static_cast<FAUSTFLOAT>(1e+03f);
		fHslider12 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider13 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider14 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider15 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider16 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider17 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider18 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider19 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider20 = static_cast<FAUSTFLOAT>(2.5e+02f);
		fHslider21 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider22 = static_cast<FAUSTFLOAT>(0.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec5[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec3[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec6[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fVec1[l5] = 0.0f;
		}
		IOTA0 = 0;
		for (int l6 = 0; l6 < 4096; l6 = l6 + 1) {
			fVec2[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec7[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec8[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec10[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fVec3[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 4096; l11 = l11 + 1) {
			fVec4[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec11[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec13[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fVec5[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 4096; l15 = l15 + 1) {
			fVec6[l15] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec15[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec16[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fVec8[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 4096; l21 = l21 + 1) {
			fVec9[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec17[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec18[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			fRec20[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = l25 + 1) {
			iVec10[l25] = 0;
		}
		for (int l26 = 0; l26 < 2; l26 = l26 + 1) {
			iRec19[l26] = 0;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			iRec21[l27] = 0;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec22[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 3; l29 = l29 + 1) {
			fRec2[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = l30 + 1) {
			fRec23[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 3; l31 = l31 + 1) {
			fRec25[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 3; l32 = l32 + 1) {
			fRec24[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 524288; l33 = l33 + 1) {
			fVec11[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 3; l34 = l34 + 1) {
			fRec27[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = l35 + 1) {
			fRec26[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = l36 + 1) {
			fVec12[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = l37 + 1) {
			fRec0[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 3; l38 = l38 + 1) {
			fRec29[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 3; l39 = l39 + 1) {
			fRec31[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 3; l40 = l40 + 1) {
			fRec30[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 524288; l41 = l41 + 1) {
			fVec13[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 3; l42 = l42 + 1) {
			fRec33[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = l43 + 1) {
			fRec32[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = l44 + 1) {
			fVec14[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = l45 + 1) {
			fRec28[l45] = 0.0f;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual mydsp* clone() {
		return new mydsp(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("acid9voice");
		ui_interface->addHorizontalSlider("accent", &fHslider2, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("cutoff", &fHslider11, FAUSTFLOAT(1e+03f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(2e+04f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("cutoff_cv", &fHslider12, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("decay", &fHslider15, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.01f), FAUSTFLOAT(2.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_fb", &fHslider21, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.95f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_ghost", &fHslider19, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_mix", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_time", &fHslider20, FAUSTFLOAT(2.5e+02f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(2e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("env_mod", &fHslider13, FAUSTFLOAT(0.5f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("filter_mode", &fHslider17, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("fm_in", &fHslider4, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("gate", &fHslider14, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("grit", &fHslider10, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("isotope", &fHslider7, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("resonance", &fHslider16, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("return_connected", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("return_in_l", &fHslider18, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+01f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("return_in_r", &fHslider22, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+01f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("shape", &fHslider3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("slide", &fHslider6, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("sub_level", &fHslider8, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("sub_mode", &fHslider9, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("volts", &fHslider5, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		FAUSTFLOAT* output2 = outputs[2];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		int iSlow1 = static_cast<float>(fHslider1) > 0.5f;
		int iSlow2 = static_cast<float>(fHslider2) > 0.9f;
		float fSlow3 = ((iSlow2) ? 1.3f : 1.0f);
		float fSlow4 = static_cast<float>(fHslider3);
		int iSlow5 = fSlow4 > 0.5f;
		float fSlow6 = std::min<float>(1.0f, 2.0f * fSlow4);
		float fSlow7 = 1.5707964f * fSlow6;
		float fSlow8 = std::cos(fSlow7);
		float fSlow9 = 0.1f * static_cast<float>(fHslider4);
		float fSlow10 = ((static_cast<float>(fHslider6) > 0.9f) ? 0.06f : 0.001f);
		int iSlow11 = std::fabs(fSlow10) < 1.1920929e-07f;
		float fSlow12 = ((iSlow11) ? 0.0f : std::exp(-(fConst3 / ((iSlow11) ? 1.0f : fSlow10))));
		float fSlow13 = static_cast<float>(fHslider5) * (1.0f - fSlow12);
		float fSlow14 = std::sin(fSlow7);
		float fSlow15 = 0.4f * fSlow6;
		float fSlow16 = fSlow15 + 0.3f;
		float fSlow17 = 2.0f * fSlow6 + 1.0f;
		float fSlow18 = 1.5707964f * std::max<float>(0.0f, 2.0f * (fSlow4 + -0.5f));
		float fSlow19 = std::cos(fSlow18);
		float fSlow20 = fConst4 * std::sin(fSlow18);
		float fSlow21 = fConst1 * static_cast<float>(fHslider7);
		float fSlow22 = static_cast<float>(fHslider8);
		int iSlow23 = static_cast<float>(fHslider9) > 0.5f;
		float fSlow24 = fConst1 * static_cast<float>(fHslider10);
		float fSlow25 = fConst1 * static_cast<float>(fHslider11);
		float fSlow26 = std::pow(2.0f, static_cast<float>(fHslider12));
		float fSlow27 = 4.0f * static_cast<float>(fHslider13);
		float fSlow28 = ((iSlow2) ? 1.5f : 1.0f);
		float fSlow29 = fConst11 * static_cast<float>(fHslider14);
		float fSlow30 = 1.0f / std::max<float>(1.0f, fConst0 * static_cast<float>(fHslider15));
		float fSlow31 = fConst1 * static_cast<float>(fHslider16);
		float fSlow32 = fConst1 * static_cast<float>(fHslider17);
		float fSlow33 = static_cast<float>(fHslider18);
		float fSlow34 = std::tan(fConst7 * (4e+02f * static_cast<float>(fHslider19) + 8e+01f));
		float fSlow35 = mydsp_faustpower2_f(fSlow34);
		float fSlow36 = 1.0f / fSlow34;
		float fSlow37 = (fSlow36 + 1.4142135f) / fSlow34 + 1.0f;
		float fSlow38 = 1.0f / (fSlow35 * fSlow37);
		float fSlow39 = std::max<float>(1e+01f, std::min<float>(static_cast<float>(fHslider20), 2e+03f));
		float fSlow40 = fConst17 * fSlow39;
		float fSlow41 = std::floor(fSlow40);
		float fSlow42 = fSlow41 + (1.0f - fSlow40);
		float fSlow43 = std::min<float>(static_cast<float>(fHslider21), 0.95f);
		int iSlow44 = static_cast<int>(fSlow40);
		int iSlow45 = std::min<int>(403201, std::max<int>(0, iSlow44));
		float fSlow46 = fSlow40 - fSlow41;
		int iSlow47 = std::min<int>(403201, std::max<int>(0, iSlow44 + 1));
		float fSlow48 = 1.0f / fSlow37;
		float fSlow49 = (fSlow36 + -1.4142135f) / fSlow34 + 1.0f;
		float fSlow50 = 2.0f * (1.0f - 1.0f / fSlow35);
		float fSlow51 = static_cast<float>(fHslider22);
		float fSlow52 = fConst18 * fSlow39;
		float fSlow53 = std::floor(fSlow52);
		float fSlow54 = fSlow53 + (1.0f - fSlow52);
		int iSlow55 = static_cast<int>(fSlow52);
		int iSlow56 = std::min<int>(403201, std::max<int>(0, iSlow55));
		float fSlow57 = fSlow52 - fSlow53;
		int iSlow58 = std::min<int>(403201, std::max<int>(0, iSlow55 + 1));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			fRec1[0] = fSlow0 + fConst2 * fRec1[1];
			float fTemp0 = 1.0f - fRec1[0];
			fRec5[0] = fSlow13 + fSlow12 * fRec5[1];
			float fTemp1 = std::max<float>(2e+01f, std::min<float>(261.62f * std::pow(2.0f, fSlow9 + fRec5[0]), 2e+04f));
			float fTemp2 = std::max<float>(1.1920929e-07f, std::fabs(fTemp1));
			float fTemp3 = fRec3[1] + fConst3 * fTemp2;
			float fTemp4 = fTemp3 + -1.0f;
			int iTemp5 = fTemp4 < 0.0f;
			fRec3[0] = ((iTemp5) ? fTemp3 : fTemp4);
			float fRec4 = ((iTemp5) ? fTemp3 : fTemp3 + fTemp4 * (1.0f - fConst0 / fTemp2));
			float fTemp6 = 2.0f * fRec4;
			float fTemp7 = fTemp6 + -1.0f;
			float fTemp8 = ((fTemp7 > fSlow16) ? fSlow15 + (0.3f - fSlow17 * (fTemp6 + -1.3f - fSlow15)) : fTemp7);
			float fTemp9 = static_cast<float>(iVec0[1]);
			int iTemp10 = 1 - iVec0[1];
			float fTemp11 = std::max<float>(fTemp1, 23.44895f);
			float fTemp12 = std::max<float>(2e+01f, std::fabs(fTemp11));
			float fTemp13 = ((iTemp10) ? 0.0f : fRec6[1] + fConst3 * fTemp12);
			fRec6[0] = fTemp13 - std::floor(fTemp13);
			float fTemp14 = mydsp_faustpower2_f(2.0f * fRec6[0] + -1.0f);
			fVec1[0] = fTemp14;
			float fTemp15 = fTemp9 * (fTemp14 - fVec1[1]) / fTemp12;
			fVec2[IOTA0 & 4095] = fTemp15;
			float fTemp16 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst5 / fTemp11));
			int iTemp17 = static_cast<int>(fTemp16);
			float fTemp18 = std::floor(fTemp16);
			fRec7[0] = fSlow21 + fConst2 * fRec7[1];
			float fTemp19 = mydsp_faustpower2_f(fRec7[0]);
			float fTemp20 = ((iSlow5) ? fSlow19 * fTemp8 + fSlow20 * (fTemp15 - fVec2[(IOTA0 - iTemp17) & 4095] * (fTemp18 + (1.0f - fTemp16)) - (fTemp16 - fTemp18) * fVec2[(IOTA0 - (iTemp17 + 1)) & 4095]) : fSlow8 * fTemp7 + fSlow14 * fTemp8) * (1.0f - 0.3f * fTemp19);
			float fTemp21 = 0.025f * fRec7[0];
			float fTemp22 = fTemp1 * std::pow(2.0f, -fTemp21);
			float fTemp23 = std::max<float>(1.1920929e-07f, std::fabs(fTemp22));
			float fTemp24 = fRec8[1] + fConst3 * fTemp23;
			float fTemp25 = fTemp24 + -1.0f;
			int iTemp26 = fTemp25 < 0.0f;
			fRec8[0] = ((iTemp26) ? fTemp24 : fTemp25);
			float fRec9 = ((iTemp26) ? fTemp24 : fTemp24 + fTemp25 * (1.0f - fConst0 / fTemp23));
			float fTemp27 = 2.0f * fRec9;
			float fTemp28 = fTemp27 + -1.0f;
			float fTemp29 = ((fTemp28 > fSlow16) ? fSlow15 + (0.3f - fSlow17 * (fTemp27 + -1.3f - fSlow15)) : fTemp28);
			float fTemp30 = std::max<float>(fTemp22, 23.44895f);
			float fTemp31 = std::max<float>(2e+01f, std::fabs(fTemp30));
			float fTemp32 = ((iTemp10) ? 0.0f : fRec10[1] + fConst3 * fTemp31);
			fRec10[0] = fTemp32 - std::floor(fTemp32);
			float fTemp33 = mydsp_faustpower2_f(2.0f * fRec10[0] + -1.0f);
			fVec3[0] = fTemp33;
			float fTemp34 = fTemp9 * (fTemp33 - fVec3[1]) / fTemp31;
			fVec4[IOTA0 & 4095] = fTemp34;
			float fTemp35 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst5 / fTemp30));
			int iTemp36 = static_cast<int>(fTemp35);
			float fTemp37 = std::floor(fTemp35);
			float fTemp38 = fTemp19 * ((iSlow5) ? fSlow19 * fTemp29 + fSlow20 * (fTemp34 - fVec4[(IOTA0 - iTemp36) & 4095] * (fTemp37 + (1.0f - fTemp35)) - (fTemp35 - fTemp37) * fVec4[(IOTA0 - (iTemp36 + 1)) & 4095]) : fSlow8 * fTemp28 + fSlow14 * fTemp29);
			float fTemp39 = fRec7[0] + 1.0f;
			float fTemp40 = fTemp1 * std::pow(2.0f, fTemp21);
			float fTemp41 = std::max<float>(1.1920929e-07f, std::fabs(fTemp40));
			float fTemp42 = fRec11[1] + fConst3 * fTemp41;
			float fTemp43 = fTemp42 + -1.0f;
			int iTemp44 = fTemp43 < 0.0f;
			fRec11[0] = ((iTemp44) ? fTemp42 : fTemp43);
			float fRec12 = ((iTemp44) ? fTemp42 : fTemp42 + fTemp43 * (1.0f - fConst0 / fTemp41));
			float fTemp45 = 2.0f * fRec12;
			float fTemp46 = fTemp45 + -1.0f;
			float fTemp47 = ((fTemp46 > fSlow16) ? fSlow15 + (0.3f - fSlow17 * (fTemp45 + -1.3f - fSlow15)) : fTemp46);
			float fTemp48 = std::max<float>(fTemp40, 23.44895f);
			float fTemp49 = std::max<float>(2e+01f, std::fabs(fTemp48));
			float fTemp50 = ((iTemp10) ? 0.0f : fRec13[1] + fConst3 * fTemp49);
			fRec13[0] = fTemp50 - std::floor(fTemp50);
			float fTemp51 = mydsp_faustpower2_f(2.0f * fRec13[0] + -1.0f);
			fVec5[0] = fTemp51;
			float fTemp52 = fTemp9 * (fTemp51 - fVec5[1]) / fTemp49;
			fVec6[IOTA0 & 4095] = fTemp52;
			float fTemp53 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst5 / fTemp48));
			int iTemp54 = static_cast<int>(fTemp53);
			float fTemp55 = std::floor(fTemp53);
			float fTemp56 = ((iSlow5) ? fSlow19 * fTemp47 + fSlow20 * (fTemp52 - fVec6[(IOTA0 - iTemp54) & 4095] * (fTemp55 + (1.0f - fTemp53)) - (fTemp53 - fTemp55) * fVec6[(IOTA0 - (iTemp54 + 1)) & 4095]) : fSlow8 * fTemp46 + fSlow14 * fTemp47);
			float fTemp57 = 1.0f - fRec7[0];
			float fTemp58 = ((iTemp10) ? 0.0f : fRec15[1] + fConst6 * fTemp1);
			fRec15[0] = fTemp58 - std::floor(fTemp58);
			float fTemp59 = std::max<float>(0.5f * fTemp1, 23.44895f);
			float fTemp60 = std::max<float>(2e+01f, std::fabs(fTemp59));
			float fTemp61 = ((iTemp10) ? 0.0f : fRec16[1] + fConst3 * fTemp60);
			fRec16[0] = fTemp61 - std::floor(fTemp61);
			float fTemp62 = mydsp_faustpower2_f(2.0f * fRec16[0] + -1.0f);
			fVec8[0] = fTemp62;
			float fTemp63 = fTemp9 * (fTemp62 - fVec8[1]) / fTemp60;
			fVec9[IOTA0 & 4095] = fTemp63;
			float fTemp64 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst5 / fTemp59));
			int iTemp65 = static_cast<int>(fTemp64);
			float fTemp66 = std::floor(fTemp64);
			float fTemp67 = fSlow22 * ((iSlow23) ? fConst4 * (fTemp63 - fVec9[(IOTA0 - iTemp65) & 4095] * (fTemp66 + (1.0f - fTemp64)) - (fTemp64 - fTemp66) * fVec9[(IOTA0 - (iTemp65 + 1)) & 4095]) : ftbl0mydspSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec15[0]), 65535))]);
			float fTemp68 = fTemp20 + fTemp38 * fTemp39 + fTemp19 * fTemp56 * fTemp57 + fTemp67;
			fRec17[0] = fSlow24 + fConst2 * fRec17[1];
			float fTemp69 = std::max<float>(0.0f, 1.0f - 1.5f * fRec17[0]);
			float fTemp70 = 6.0f * fRec17[0] + 1.0f;
			float fTemp71 = 0.25f * fRec17[0] + 1.0f;
			float fTemp72 = (0.3f * fTemp68 * fTemp69 + fRec17[0] * tanhf(0.3f * fTemp68 * fTemp70) * (0.054f * fRec17[0] * mydsp_faustpower2_f(fTemp68) + 1.0f)) / fTemp71;
			fRec18[0] = fSlow25 + fConst2 * fRec18[1];
			fRec20[0] = fSlow29 + fConst10 * fRec20[1];
			int iTemp73 = fRec20[0] > 0.9f;
			iVec10[0] = iTemp73;
			iRec19[0] = iTemp73 + iRec19[1] * (iVec10[1] >= iTemp73);
			float fTemp74 = static_cast<float>(iRec19[0]);
			float fTemp75 = fConst9 * fTemp74;
			float fTemp76 = fConst8 - fTemp74;
			iRec21[0] = (iTemp73 == 0) * (iRec21[1] + 1);
			float fTemp77 = static_cast<float>(iRec21[0]);
			float fTemp78 = std::tan(fConst7 * std::max<float>(2e+01f, std::min<float>(std::max<float>(2e+01f, std::min<float>(std::max<float>(2e+01f, std::min<float>(fRec18[0] * (fSlow26 + fSlow27 * fSlow28 * std::max<float>(0.0f, std::min<float>(fTemp75, std::max<float>(fSlow30 * fTemp76 + 1.0f, 0.0f)) * (1.0f - fConst12 * fTemp77))), 2e+04f)), fConst13)), fConst13)));
			float fTemp79 = 1.0f / fTemp78;
			fRec22[0] = fSlow31 + fConst2 * fRec22[1];
			float fTemp80 = std::max<float>(0.0f, std::min<float>(fRec22[0], 0.99f));
			float fTemp81 = 1.0f / (1e+01f * fTemp80 + 0.5f);
			float fTemp82 = (fTemp79 - fTemp81) / fTemp78 + 1.0f;
			float fTemp83 = 1.0f - 1.0f / mydsp_faustpower2_f(fTemp78);
			float fTemp84 = (fTemp79 + fTemp81) / fTemp78 + 1.0f;
			fRec2[0] = fTemp72 - (fRec2[2] * fTemp82 + 2.0f * fRec2[1] * fTemp83) / fTemp84;
			fRec23[0] = fSlow32 + fConst2 * fRec23[1];
			float fTemp85 = 1.5707964f * fRec23[0];
			float fTemp86 = std::cos(fTemp85);
			float fTemp87 = 15.0f * fTemp80 + 0.5f;
			float fTemp88 = 1.0f / fTemp87;
			float fTemp89 = (fTemp79 - fTemp88) / fTemp78 + 1.0f;
			float fTemp90 = (fTemp79 + fTemp88) / fTemp78 + 1.0f;
			fRec25[0] = fTemp72 - (fRec25[2] * fTemp89 + 2.0f * fTemp83 * fRec25[1]) / fTemp90;
			float fTemp91 = 1.4285715f / fTemp87;
			float fTemp92 = (fTemp79 - fTemp91) / fTemp78 + 1.0f;
			float fTemp93 = (fTemp79 + fTemp91) / fTemp78 + 1.0f;
			fRec24[0] = (fRec25[2] + fRec25[0] + 2.0f * fRec25[1]) / fTemp90 - (fRec24[2] * fTemp92 + 2.0f * fTemp83 * fRec24[1]) / fTemp93;
			float fTemp94 = std::sin(fTemp85);
			float fTemp95 = (fRec2[2] + fRec2[0] + 2.0f * fRec2[1]) * fTemp86 / fTemp84 + (fRec24[2] + fRec24[0] + 2.0f * fRec24[1]) * fTemp94 / fTemp93;
			float fTemp96 = std::max<float>(0.0f, std::min<float>(fTemp75, std::max<float>(fConst15 * fTemp76 + 1.0f, 0.7f)) * (1.0f - fConst16 * fTemp77));
			float fTemp97 = ((iSlow1) ? fSlow33 : fSlow3 * fTemp95 * fTemp96);
			float fTemp98 = fTemp97 + fSlow43 * fRec26[1];
			fVec11[IOTA0 & 524287] = fTemp98;
			fRec27[0] = fSlow42 * fVec11[(IOTA0 - iSlow45) & 524287] + fSlow46 * fVec11[(IOTA0 - iSlow47) & 524287] - fSlow48 * (fSlow49 * fRec27[2] + fSlow50 * fRec27[1]);
			fRec26[0] = fSlow38 * (fRec27[2] + (fRec27[0] - 2.0f * fRec27[1]));
			float fTemp99 = fTemp0 * fTemp97 + fRec1[0] * fRec26[0];
			fVec12[0] = fTemp99;
			fRec0[0] = 0.995f * fRec0[1] + 0.7f * (fTemp99 - fVec12[1]);
			output0[i0] = static_cast<FAUSTFLOAT>(tanhf(fRec0[0]));
			float fTemp100 = fSlow3 * fTemp96;
			float fTemp101 = fTemp67 + fTemp20 + fTemp19 * fTemp39 * fTemp56 + fTemp38 * fTemp57;
			float fTemp102 = (0.3f * fTemp69 * fTemp101 + fRec17[0] * tanhf(0.3f * fTemp70 * fTemp101) * (0.054f * fRec17[0] * mydsp_faustpower2_f(fTemp101) + 1.0f)) / fTemp71;
			fRec29[0] = fTemp102 - (fTemp82 * fRec29[2] + 2.0f * fTemp83 * fRec29[1]) / fTemp84;
			fRec31[0] = fTemp102 - (fTemp89 * fRec31[2] + 2.0f * fTemp83 * fRec31[1]) / fTemp90;
			fRec30[0] = (fRec31[2] + fRec31[0] + 2.0f * fRec31[1]) / fTemp90 - (fTemp92 * fRec30[2] + 2.0f * fTemp83 * fRec30[1]) / fTemp93;
			float fTemp103 = fTemp86 * (fRec29[2] + fRec29[0] + 2.0f * fRec29[1]) / fTemp84 + fTemp94 * (fRec30[2] + fRec30[0] + 2.0f * fRec30[1]) / fTemp93;
			float fTemp104 = ((iSlow1) ? fSlow51 : fTemp100 * fTemp103);
			float fTemp105 = fTemp104 + fSlow43 * fRec32[1];
			fVec13[IOTA0 & 524287] = fTemp105;
			fRec33[0] = fSlow54 * fVec13[(IOTA0 - iSlow56) & 524287] + fSlow57 * fVec13[(IOTA0 - iSlow58) & 524287] - fSlow48 * (fSlow49 * fRec33[2] + fSlow50 * fRec33[1]);
			fRec32[0] = fSlow38 * (fRec33[2] + (fRec33[0] - 2.0f * fRec33[1]));
			float fTemp106 = fTemp0 * fTemp104 + fRec1[0] * fRec32[0];
			fVec14[0] = fTemp106;
			fRec28[0] = 0.995f * fRec28[1] + 0.7f * (fTemp106 - fVec14[1]);
			output1[i0] = static_cast<FAUSTFLOAT>(tanhf(fRec28[0]));
			output2[i0] = static_cast<FAUSTFLOAT>(0.35f * fTemp100 * (fTemp95 + fTemp103));
			iVec0[1] = iVec0[0];
			fRec1[1] = fRec1[0];
			fRec5[1] = fRec5[0];
			fRec3[1] = fRec3[0];
			fRec6[1] = fRec6[0];
			fVec1[1] = fVec1[0];
			IOTA0 = IOTA0 + 1;
			fRec7[1] = fRec7[0];
			fRec8[1] = fRec8[0];
			fRec10[1] = fRec10[0];
			fVec3[1] = fVec3[0];
			fRec11[1] = fRec11[0];
			fRec13[1] = fRec13[0];
			fVec5[1] = fVec5[0];
			fRec15[1] = fRec15[0];
			fRec16[1] = fRec16[0];
			fVec8[1] = fVec8[0];
			fRec17[1] = fRec17[0];
			fRec18[1] = fRec18[0];
			fRec20[1] = fRec20[0];
			iVec10[1] = iVec10[0];
			iRec19[1] = iRec19[0];
			iRec21[1] = iRec21[0];
			fRec22[1] = fRec22[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec23[1] = fRec23[0];
			fRec25[2] = fRec25[1];
			fRec25[1] = fRec25[0];
			fRec24[2] = fRec24[1];
			fRec24[1] = fRec24[0];
			fRec27[2] = fRec27[1];
			fRec27[1] = fRec27[0];
			fRec26[1] = fRec26[0];
			fVec12[1] = fVec12[0];
			fRec0[1] = fRec0[0];
			fRec29[2] = fRec29[1];
			fRec29[1] = fRec29[0];
			fRec31[2] = fRec31[1];
			fRec31[1] = fRec31[0];
			fRec30[2] = fRec30[1];
			fRec30[1] = fRec30[0];
			fRec33[2] = fRec33[1];
			fRec33[1] = fRec33[0];
			fRec32[1] = fRec32[0];
			fVec14[1] = fVec14[0];
			fRec28[1] = fRec28[0];
		}
	}

};

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

#endif
