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
	int iRec28[2];
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
		for (int l26 = 0; l26 < 2; l26 = l26 + 1) {
			iVec7[l26] = 0;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			iRec28[l27] = 0;
		}
	}
	
	void fillmydspSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec7[0] = 1;
			iRec28[0] = (iVec7[1] + iRec28[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec28[0]));
			iVec7[1] = iVec7[0];
			iRec28[1] = iRec28[0];
		}
	}

};

static mydspSIG0* newmydspSIG0() { return (mydspSIG0*)new mydspSIG0(); }
static void deletemydspSIG0(mydspSIG0* dsp) { delete dsp; }

static float mydsp_faustpower2_f(float value) {
	return value * value;
}
static float ftbl0mydspSIG0[65536];
static float mydsp_faustpower4_f(float value) {
	return value * value * value * value;
}

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
	float fConst3;
	float fConst4;
	float fConst5;
	FAUSTFLOAT fHslider2;
	FAUSTFLOAT fButton0;
	float fRec4[2];
	float fConst6;
	float fConst7;
	float fConst8;
	float fRec5[2];
	float fConst9;
	float fConst10;
	float fRec3[2];
	float fConst11;
	float fConst12;
	FAUSTFLOAT fHslider3;
	FAUSTFLOAT fHslider4;
	float fConst13;
	float fConst14;
	FAUSTFLOAT fHslider5;
	float fRec7[2];
	float fRec6[2];
	float fRec2[2];
	float fConst15;
	FAUSTFLOAT fHslider6;
	float fRec13[2];
	FAUSTFLOAT fHslider7;
	FAUSTFLOAT fHslider8;
	float fRec14[2];
	FAUSTFLOAT fHslider9;
	float fRec15[2];
	float fConst16;
	float fRec16[2];
	float fConst17;
	float fConst18;
	FAUSTFLOAT fHslider10;
	FAUSTFLOAT fHslider11;
	FAUSTFLOAT fHslider12;
	FAUSTFLOAT fHslider13;
	FAUSTFLOAT fHslider14;
	float fRec19[2];
	float fRec17[2];
	float fConst19;
	float fRec20[2];
	float fVec1[2];
	int IOTA0;
	float fVec2[4096];
	float fConst20;
	FAUSTFLOAT fHslider15;
	float fRec21[2];
	float fRec22[2];
	float fRec24[2];
	float fVec3[2];
	float fVec4[4096];
	float fRec25[2];
	float fRec27[2];
	float fVec5[2];
	float fVec6[4096];
	FAUSTFLOAT fHslider16;
	FAUSTFLOAT fHslider17;
	float fConst21;
	float fRec29[2];
	float fRec30[2];
	float fVec8[2];
	float fVec9[4096];
	FAUSTFLOAT fHslider18;
	float fRec31[2];
	float fRec8[2];
	float fRec9[2];
	float fRec10[2];
	float fRec11[2];
	float fConst22;
	float fConst23;
	FAUSTFLOAT fHslider19;
	float fRec32[2];
	float fRec33[2];
	float fRec34[2];
	float fRec35[2];
	float fRec36[2];
	FAUSTFLOAT fHslider20;
	FAUSTFLOAT fHslider21;
	FAUSTFLOAT fHslider22;
	FAUSTFLOAT fHslider23;
	float fVec10[524288];
	float fRec39[3];
	float fRec38[2];
	float fVec11[2];
	float fRec0[2];
	float fRec41[2];
	float fRec42[2];
	float fRec43[2];
	float fRec44[2];
	float fRec46[2];
	float fRec47[2];
	float fRec48[2];
	float fRec49[2];
	FAUSTFLOAT fHslider24;
	float fConst24;
	float fVec12[524288];
	float fRec52[3];
	float fRec51[2];
	float fVec13[2];
	float fRec40[2];
	float fVec14[2];
	float fRec53[2];
	
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
		m->declare("vaeffects.lib/lowpassLadder4:author", "Dario Sanfilippo");
		m->declare("vaeffects.lib/lowpassLadder4:license", "MIT License");
		m->declare("vaeffects.lib/name", "Faust Virtual Analog Filter Effect Library");
		m->declare("vaeffects.lib/version", "1.5.0");
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
		fConst3 = 1e+03f / fConst0;
		fConst4 = std::exp(-fConst3);
		fConst5 = 1.0f - fConst4;
		fConst6 = 333.33334f / fConst0;
		fConst7 = std::exp(-fConst6);
		fConst8 = 4.0f * fConst0;
		fConst9 = 0.003f * fConst0;
		fConst10 = std::exp(-(0.8333333f / fConst0));
		fConst11 = std::exp(-(644.7453f / fConst0));
		fConst12 = 1.0f - fConst11;
		fConst13 = 0.001f * fConst0;
		fConst14 = 1.0f / fConst0;
		fConst15 = 3.1415927f / fConst0;
		fConst16 = 1.0f - 1e+01f / fConst0;
		fConst17 = 0.45f * fConst0;
		fConst18 = 0.4f * fConst0;
		fConst19 = 0.25f * fConst0;
		fConst20 = 0.5f * fConst0;
		fConst21 = 0.5f / fConst0;
		fConst22 = std::exp(-(1e+02f / fConst0));
		fConst23 = 1.0f - fConst22;
		fConst24 = 0.00105f * fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.0f);
		fButton0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider4 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider5 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider6 = static_cast<FAUSTFLOAT>(1e+03f);
		fHslider7 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider8 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider9 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider10 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider11 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider12 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider13 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider14 = static_cast<FAUSTFLOAT>(6e+01f);
		fHslider15 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider16 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider17 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider18 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider19 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider20 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider21 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider22 = static_cast<FAUSTFLOAT>(2.5e+02f);
		fHslider23 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider24 = static_cast<FAUSTFLOAT>(0.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec4[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec5[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec3[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec7[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec6[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec2[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec13[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec14[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec15[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec16[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec19[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec17[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fRec20[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fVec1[l15] = 0.0f;
		}
		IOTA0 = 0;
		for (int l16 = 0; l16 < 4096; l16 = l16 + 1) {
			fVec2[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			fRec21[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec22[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec24[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fVec3[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 4096; l21 = l21 + 1) {
			fVec4[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec25[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec27[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			fVec5[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 4096; l25 = l25 + 1) {
			fVec6[l25] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec29[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = l29 + 1) {
			fRec30[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = l30 + 1) {
			fVec8[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 4096; l31 = l31 + 1) {
			fVec9[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = l32 + 1) {
			fRec31[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = l33 + 1) {
			fRec8[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = l34 + 1) {
			fRec9[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = l35 + 1) {
			fRec10[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = l36 + 1) {
			fRec11[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = l37 + 1) {
			fRec32[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = l38 + 1) {
			fRec33[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = l39 + 1) {
			fRec34[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = l40 + 1) {
			fRec35[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = l41 + 1) {
			fRec36[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 524288; l42 = l42 + 1) {
			fVec10[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 3; l43 = l43 + 1) {
			fRec39[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = l44 + 1) {
			fRec38[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = l45 + 1) {
			fVec11[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = l46 + 1) {
			fRec0[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = l47 + 1) {
			fRec41[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 2; l48 = l48 + 1) {
			fRec42[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = l49 + 1) {
			fRec43[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 2; l50 = l50 + 1) {
			fRec44[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = l51 + 1) {
			fRec46[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = l52 + 1) {
			fRec47[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = l53 + 1) {
			fRec48[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 2; l54 = l54 + 1) {
			fRec49[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 524288; l55 = l55 + 1) {
			fVec12[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 3; l56 = l56 + 1) {
			fRec52[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = l57 + 1) {
			fRec51[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = l58 + 1) {
			fVec13[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = l59 + 1) {
			fRec40[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 2; l60 = l60 + 1) {
			fVec14[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = l61 + 1) {
			fRec53[l61] = 0.0f;
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
		ui_interface->addHorizontalSlider("accent", &fHslider4, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("accent_amount", &fHslider3, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("cutoff", &fHslider6, FAUSTFLOAT(1e+03f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(2e+04f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("cutoff_cv", &fHslider7, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("decay", &fHslider5, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.01f), FAUSTFLOAT(2.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_fb", &fHslider23, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.95f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_ghost", &fHslider21, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_mix", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("delay_time", &fHslider22, FAUSTFLOAT(2.5e+02f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(2e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("env_mod", &fHslider8, FAUSTFLOAT(0.5f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("filter_mode", &fHslider19, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("fm_in", &fHslider11, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("gate", &fHslider2, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("grit", &fHslider18, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("isotope", &fHslider15, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addButton("note_trigger", &fButton0);
		ui_interface->addHorizontalSlider("resonance", &fHslider9, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("return_connected", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("return_in_l", &fHslider20, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+01f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("return_in_r", &fHslider24, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+01f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("shape", &fHslider10, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("slide", &fHslider13, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("slide_time_ms", &fHslider14, FAUSTFLOAT(6e+01f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(2e+02f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("sub_level", &fHslider16, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("sub_mode", &fHslider17, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("volts", &fHslider12, FAUSTFLOAT(0.0f), FAUSTFLOAT(-5.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		FAUSTFLOAT* output2 = outputs[2];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		int iSlow1 = static_cast<float>(fHslider1) > 0.5f;
		int iSlow2 = static_cast<float>(fHslider2) > 0.9f;
		float fSlow3 = static_cast<float>(fButton0);
		float fSlow4 = 1.0f - fSlow3;
		float fSlow5 = static_cast<float>(fHslider3);
		int iSlow6 = (static_cast<float>(fHslider4) > 0.9f) & (fSlow5 > 0.0f);
		float fSlow7 = fSlow5 * static_cast<float>(iSlow6);
		float fSlow8 = fConst12 * fSlow7;
		float fSlow9 = std::exp(-(fConst14 / ((iSlow6) ? 0.06f : static_cast<float>(fHslider5))));
		float fSlow10 = fConst1 * static_cast<float>(fHslider6);
		float fSlow11 = static_cast<float>(fHslider7);
		float fSlow12 = 4.0f * static_cast<float>(fHslider8);
		float fSlow13 = fConst1 * static_cast<float>(fHslider9);
		float fSlow14 = 0.6802721f * fSlow7;
		float fSlow15 = static_cast<float>(fHslider10);
		int iSlow16 = fSlow15 > 0.5f;
		float fSlow17 = std::min<float>(1.0f, 2.0f * fSlow15);
		float fSlow18 = 1.5707964f * fSlow17;
		float fSlow19 = std::cos(fSlow18);
		float fSlow20 = 0.1f * static_cast<float>(fHslider11);
		float fSlow21 = ((static_cast<float>(fHslider13) > 0.9f) ? 0.0004342945f * static_cast<float>(fHslider14) : 0.001f);
		int iSlow22 = std::fabs(fSlow21) < 1.1920929e-07f;
		float fSlow23 = ((iSlow22) ? 0.0f : std::exp(-(fConst14 / ((iSlow22) ? 1.0f : fSlow21))));
		float fSlow24 = static_cast<float>(fHslider12) * (1.0f - fSlow23);
		float fSlow25 = std::sin(fSlow18);
		float fSlow26 = 0.4f * fSlow17;
		float fSlow27 = fSlow26 + 0.3f;
		float fSlow28 = 2.0f * fSlow17 + 1.0f;
		float fSlow29 = 1.5707964f * std::max<float>(0.0f, 2.0f * (fSlow15 + -0.5f));
		float fSlow30 = std::cos(fSlow29);
		float fSlow31 = fConst19 * std::sin(fSlow29);
		float fSlow32 = fConst1 * static_cast<float>(fHslider15);
		float fSlow33 = static_cast<float>(fHslider16);
		int iSlow34 = static_cast<float>(fHslider17) > 0.5f;
		float fSlow35 = fConst1 * static_cast<float>(fHslider18);
		float fSlow36 = fConst23 * std::max<float>(0.0f, std::min<float>(static_cast<float>(fHslider19), 1.0f));
		float fSlow37 = static_cast<float>(fHslider20);
		float fSlow38 = std::tan(fConst15 * (4e+02f * static_cast<float>(fHslider21) + 8e+01f));
		float fSlow39 = mydsp_faustpower2_f(fSlow38);
		float fSlow40 = 1.0f / fSlow38;
		float fSlow41 = (fSlow40 + 1.4142135f) / fSlow38 + 1.0f;
		float fSlow42 = 1.0f / (fSlow39 * fSlow41);
		float fSlow43 = std::max<float>(1e+01f, std::min<float>(static_cast<float>(fHslider22), 2e+03f));
		float fSlow44 = fConst13 * fSlow43;
		float fSlow45 = std::floor(fSlow44);
		float fSlow46 = fSlow45 + (1.0f - fSlow44);
		float fSlow47 = std::min<float>(static_cast<float>(fHslider23), 0.95f);
		int iSlow48 = static_cast<int>(fSlow44);
		int iSlow49 = std::min<int>(403201, std::max<int>(0, iSlow48));
		float fSlow50 = fSlow44 - fSlow45;
		int iSlow51 = std::min<int>(403201, std::max<int>(0, iSlow48 + 1));
		float fSlow52 = 1.0f / fSlow41;
		float fSlow53 = (fSlow40 + -1.4142135f) / fSlow38 + 1.0f;
		float fSlow54 = 2.0f * (1.0f - 1.0f / fSlow39);
		float fSlow55 = static_cast<float>(fHslider24);
		float fSlow56 = fConst24 * fSlow43;
		float fSlow57 = std::floor(fSlow56);
		float fSlow58 = fSlow57 + (1.0f - fSlow56);
		int iSlow59 = static_cast<int>(fSlow56);
		int iSlow60 = std::min<int>(403201, std::max<int>(0, iSlow59));
		float fSlow61 = fSlow56 - fSlow57;
		int iSlow62 = std::min<int>(403201, std::max<int>(0, iSlow59 + 1));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			fRec1[0] = fSlow0 + fConst2 * fRec1[1];
			float fTemp0 = 1.0f - fRec1[0];
			fRec4[0] = std::max<float>(fSlow3, fRec4[1]);
			int iTemp1 = static_cast<int>(fRec4[0]);
			fRec5[0] = fSlow4 * std::min<float>(fConst8, fRec5[1] + 1.0f);
			fRec3[0] = ((iSlow2 & iTemp1) ? ((fRec5[0] < fConst9) ? std::min<float>(1.0f, fConst6 + fRec3[1]) : fConst10 * fRec3[1]) : fConst7 * fRec3[1]);
			fRec7[0] = ((1 & iTemp1) ? ((fRec5[0] < fConst13) ? std::min<float>(1.0f, fConst3 + fRec7[1]) : fRec7[1] * fSlow9) : fConst7 * fRec7[1]);
			fRec6[0] = fSlow8 * fRec7[0] + fConst11 * fRec6[1];
			fRec2[0] = fConst5 * fRec3[0] * (0.3f * fRec6[0] + 1.0f) + fConst4 * fRec2[1];
			fRec13[0] = fSlow10 + fConst2 * fRec13[1];
			fRec14[0] = fConst5 * fRec7[0] + fConst4 * fRec14[1];
			fRec15[0] = fSlow13 + fConst2 * fRec15[1];
			fRec16[0] = fConst14 * (std::max<float>(0.0f, fSlow7 * fRec7[0] - fRec16[1]) / (0.1f * fRec15[0] + 0.047f)) + fConst16 * fRec16[1];
			float fTemp2 = std::tan(fConst15 * std::max<float>(2e+01f, std::min<float>(std::max<float>(2e+01f, std::min<float>(std::max<float>(2e+01f, std::min<float>(fRec13[0] * std::pow(2.0f, fSlow11 + fSlow12 * fRec14[0] + 2.0f * (fRec15[0] * fRec16[0] + (1.0f - fRec15[0]) * std::max<float>(0.0f, fSlow14 * fRec7[0] - fRec16[0]))), 2e+04f)), fConst17)), fConst18)));
			fRec19[0] = fSlow24 + fSlow23 * fRec19[1];
			float fTemp3 = std::max<float>(2e+01f, std::min<float>(261.62f * std::pow(2.0f, fSlow20 + fRec19[0]), 2e+04f));
			float fTemp4 = std::max<float>(1.1920929e-07f, std::fabs(fTemp3));
			float fTemp5 = fRec17[1] + fConst14 * fTemp4;
			float fTemp6 = fTemp5 + -1.0f;
			int iTemp7 = fTemp6 < 0.0f;
			fRec17[0] = ((iTemp7) ? fTemp5 : fTemp6);
			float fRec18 = ((iTemp7) ? fTemp5 : fTemp5 + fTemp6 * (1.0f - fConst0 / fTemp4));
			float fTemp8 = 2.0f * fRec18;
			float fTemp9 = fTemp8 + -1.0f;
			float fTemp10 = ((fTemp9 > fSlow27) ? fSlow26 + (0.3f - fSlow28 * (fTemp8 + -1.3f - fSlow26)) : fTemp9);
			float fTemp11 = static_cast<float>(iVec0[1]);
			int iTemp12 = 1 - iVec0[1];
			float fTemp13 = std::max<float>(fTemp3, 23.44895f);
			float fTemp14 = std::max<float>(2e+01f, std::fabs(fTemp13));
			float fTemp15 = ((iTemp12) ? 0.0f : fRec20[1] + fConst14 * fTemp14);
			fRec20[0] = fTemp15 - std::floor(fTemp15);
			float fTemp16 = mydsp_faustpower2_f(2.0f * fRec20[0] + -1.0f);
			fVec1[0] = fTemp16;
			float fTemp17 = fTemp11 * (fTemp16 - fVec1[1]) / fTemp14;
			fVec2[IOTA0 & 4095] = fTemp17;
			float fTemp18 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst20 / fTemp13));
			int iTemp19 = static_cast<int>(fTemp18);
			float fTemp20 = std::floor(fTemp18);
			fRec21[0] = fSlow32 + fConst2 * fRec21[1];
			float fTemp21 = mydsp_faustpower2_f(fRec21[0]);
			float fTemp22 = ((iSlow16) ? fSlow30 * fTemp10 + fSlow31 * (fTemp17 - fVec2[(IOTA0 - iTemp19) & 4095] * (fTemp20 + (1.0f - fTemp18)) - (fTemp18 - fTemp20) * fVec2[(IOTA0 - (iTemp19 + 1)) & 4095]) : fSlow19 * fTemp9 + fSlow25 * fTemp10) * (1.0f - 0.3f * fTemp21);
			float fTemp23 = 0.025f * fRec21[0];
			float fTemp24 = fTemp3 * std::pow(2.0f, -fTemp23);
			float fTemp25 = std::max<float>(1.1920929e-07f, std::fabs(fTemp24));
			float fTemp26 = fRec22[1] + fConst14 * fTemp25;
			float fTemp27 = fTemp26 + -1.0f;
			int iTemp28 = fTemp27 < 0.0f;
			fRec22[0] = ((iTemp28) ? fTemp26 : fTemp27);
			float fRec23 = ((iTemp28) ? fTemp26 : fTemp26 + fTemp27 * (1.0f - fConst0 / fTemp25));
			float fTemp29 = 2.0f * fRec23;
			float fTemp30 = fTemp29 + -1.0f;
			float fTemp31 = ((fTemp30 > fSlow27) ? fSlow26 + (0.3f - fSlow28 * (fTemp29 + -1.3f - fSlow26)) : fTemp30);
			float fTemp32 = std::max<float>(fTemp24, 23.44895f);
			float fTemp33 = std::max<float>(2e+01f, std::fabs(fTemp32));
			float fTemp34 = ((iTemp12) ? 0.0f : fRec24[1] + fConst14 * fTemp33);
			fRec24[0] = fTemp34 - std::floor(fTemp34);
			float fTemp35 = mydsp_faustpower2_f(2.0f * fRec24[0] + -1.0f);
			fVec3[0] = fTemp35;
			float fTemp36 = fTemp11 * (fTemp35 - fVec3[1]) / fTemp33;
			fVec4[IOTA0 & 4095] = fTemp36;
			float fTemp37 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst20 / fTemp32));
			int iTemp38 = static_cast<int>(fTemp37);
			float fTemp39 = std::floor(fTemp37);
			float fTemp40 = fTemp21 * ((iSlow16) ? fSlow30 * fTemp31 + fSlow31 * (fTemp36 - fVec4[(IOTA0 - iTemp38) & 4095] * (fTemp39 + (1.0f - fTemp37)) - (fTemp37 - fTemp39) * fVec4[(IOTA0 - (iTemp38 + 1)) & 4095]) : fSlow19 * fTemp30 + fSlow25 * fTemp31);
			float fTemp41 = fRec21[0] + 1.0f;
			float fTemp42 = fTemp3 * std::pow(2.0f, fTemp23);
			float fTemp43 = std::max<float>(1.1920929e-07f, std::fabs(fTemp42));
			float fTemp44 = fRec25[1] + fConst14 * fTemp43;
			float fTemp45 = fTemp44 + -1.0f;
			int iTemp46 = fTemp45 < 0.0f;
			fRec25[0] = ((iTemp46) ? fTemp44 : fTemp45);
			float fRec26 = ((iTemp46) ? fTemp44 : fTemp44 + fTemp45 * (1.0f - fConst0 / fTemp43));
			float fTemp47 = 2.0f * fRec26;
			float fTemp48 = fTemp47 + -1.0f;
			float fTemp49 = ((fTemp48 > fSlow27) ? fSlow26 + (0.3f - fSlow28 * (fTemp47 + -1.3f - fSlow26)) : fTemp48);
			float fTemp50 = std::max<float>(fTemp42, 23.44895f);
			float fTemp51 = std::max<float>(2e+01f, std::fabs(fTemp50));
			float fTemp52 = ((iTemp12) ? 0.0f : fRec27[1] + fConst14 * fTemp51);
			fRec27[0] = fTemp52 - std::floor(fTemp52);
			float fTemp53 = mydsp_faustpower2_f(2.0f * fRec27[0] + -1.0f);
			fVec5[0] = fTemp53;
			float fTemp54 = fTemp11 * (fTemp53 - fVec5[1]) / fTemp51;
			fVec6[IOTA0 & 4095] = fTemp54;
			float fTemp55 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst20 / fTemp50));
			int iTemp56 = static_cast<int>(fTemp55);
			float fTemp57 = std::floor(fTemp55);
			float fTemp58 = ((iSlow16) ? fSlow30 * fTemp49 + fSlow31 * (fTemp54 - fVec6[(IOTA0 - iTemp56) & 4095] * (fTemp57 + (1.0f - fTemp55)) - (fTemp55 - fTemp57) * fVec6[(IOTA0 - (iTemp56 + 1)) & 4095]) : fSlow19 * fTemp48 + fSlow25 * fTemp49);
			float fTemp59 = 1.0f - fRec21[0];
			float fTemp60 = ((iTemp12) ? 0.0f : fRec29[1] + fConst21 * fTemp3);
			fRec29[0] = fTemp60 - std::floor(fTemp60);
			float fTemp61 = std::max<float>(0.5f * fTemp3, 23.44895f);
			float fTemp62 = std::max<float>(2e+01f, std::fabs(fTemp61));
			float fTemp63 = ((iTemp12) ? 0.0f : fRec30[1] + fConst14 * fTemp62);
			fRec30[0] = fTemp63 - std::floor(fTemp63);
			float fTemp64 = mydsp_faustpower2_f(2.0f * fRec30[0] + -1.0f);
			fVec8[0] = fTemp64;
			float fTemp65 = fTemp11 * (fTemp64 - fVec8[1]) / fTemp62;
			fVec9[IOTA0 & 4095] = fTemp65;
			float fTemp66 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst20 / fTemp61));
			int iTemp67 = static_cast<int>(fTemp66);
			float fTemp68 = std::floor(fTemp66);
			float fTemp69 = fSlow33 * ((iSlow34) ? fConst19 * (fTemp65 - fVec9[(IOTA0 - iTemp67) & 4095] * (fTemp68 + (1.0f - fTemp66)) - (fTemp66 - fTemp68) * fVec9[(IOTA0 - (iTemp67 + 1)) & 4095]) : ftbl0mydspSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec29[0]), 65535))]);
			float fTemp70 = fTemp22 + fTemp40 * fTemp41 + fTemp21 * fTemp58 * fTemp59 + fTemp69;
			fRec31[0] = fSlow35 + fConst2 * fRec31[1];
			float fTemp71 = std::max<float>(0.0f, 1.0f - 1.5f * fRec31[0]);
			float fTemp72 = 6.0f * fRec31[0] + 1.0f;
			float fTemp73 = 0.25f * fRec31[0] + 1.0f;
			float fTemp74 = tanhf((0.3f * fTemp70 * fTemp71 + fRec31[0] * tanhf(0.3f * fTemp70 * fTemp72) * (0.054f * fRec31[0] * mydsp_faustpower2_f(fTemp70) + 1.0f)) / fTemp73);
			float fTemp75 = std::max<float>(0.0f, std::min<float>(fRec15[0], 0.99f));
			float fTemp76 = std::max<float>(0.0f, std::min<float>(fTemp75, 1.0f));
			float fTemp77 = fTemp2 + 1.0f;
			float fTemp78 = fRec10[1] + 0.5f * (fTemp2 * fRec11[1] / fTemp77);
			float fTemp79 = fTemp2 / fTemp77;
			float fTemp80 = fTemp2 * (1.0f - 0.25f * fTemp79) + 1.0f;
			float fTemp81 = 0.5f * (fTemp2 * fTemp78 / fTemp80);
			float fTemp82 = fRec9[1] + fTemp81;
			float fTemp83 = fTemp2 * (1.0f - 0.25f * (fTemp2 / fTemp80)) + 1.0f;
			float fTemp84 = fTemp2 * fTemp82 / fTemp83;
			float fTemp85 = fTemp2 * (1.0f - 0.5f * (fTemp2 / fTemp83)) + 1.0f;
			float fTemp86 = fTemp2 * (fRec8[1] + fTemp84) / fTemp85;
			float fTemp87 = mydsp_faustpower2_f(fTemp2);
			float fTemp88 = 0.5f * (fTemp87 / (fTemp83 * fTemp85)) + 1.0f;
			float fTemp89 = mydsp_faustpower4_f(fTemp2);
			float fTemp90 = fTemp77 * fTemp80;
			float fTemp91 = 2.0625f * (fTemp76 * fTemp89 / (fTemp90 * fTemp83 * fTemp85)) + 1.0f;
			float fTemp92 = fTemp2 * ((fTemp74 - 16.5f * (fTemp76 * (fRec11[1] + fTemp2 * (fTemp2 * (0.25f * fTemp82 + 0.125f * fTemp86) / fTemp83 + 0.5f * fTemp78) / fTemp80) / fTemp77)) * fTemp88 / fTemp91 + (fTemp82 + 0.5f * fTemp86) / fTemp83 - fRec8[1]) / fTemp77;
			fRec8[0] = fRec8[1] + 2.0f * fTemp92;
			float fTemp93 = 0.25f * (fTemp87 / (fTemp80 * fTemp83)) + 1.0f;
			float fTemp94 = fTemp2 * (0.5f * ((fRec8[1] + fTemp92) * fTemp93 + (fTemp78 + 0.5f * fTemp84) / fTemp80) - fRec9[1]) / fTemp77;
			fRec9[0] = fRec9[1] + 2.0f * fTemp94;
			float fTemp95 = 0.25f * (fTemp87 / fTemp90) + 1.0f;
			float fTemp96 = fTemp2 * (0.5f * ((fRec9[1] + fTemp94) * fTemp95 + (fRec11[1] + fTemp81) / fTemp77) - fRec10[1]) / fTemp77;
			fRec10[0] = fRec10[1] + 2.0f * fTemp96;
			float fTemp97 = fTemp2 * (0.5f * (fRec10[1] + fTemp96) - fRec11[1]) / fTemp77;
			fRec11[0] = fRec11[1] + 2.0f * fTemp97;
			float fRec12 = fRec11[1] + fTemp97;
			fRec32[0] = fSlow36 + fConst22 * fRec32[1];
			float fTemp98 = 1.0f - fRec32[0];
			float fTemp99 = 4.5f * fTemp75 + 1.0f;
			float fTemp100 = mydsp_faustpower2_f(fTemp75);
			float fTemp101 = 1.5f - 0.8f * fTemp100;
			float fTemp102 = fTemp100 * (1.0f - fTemp79);
			float fTemp103 = 3.6f * (fTemp100 * fTemp89 / mydsp_faustpower4_f(fTemp77)) + 1.0f;
			float fTemp104 = fTemp2 * ((fTemp74 - 3.6f * fTemp102 * (fRec36[1] + fTemp2 * (fRec35[1] + fTemp2 * (fRec34[1] + fTemp2 * fRec33[1] / fTemp77) / fTemp77) / fTemp77)) / fTemp103 - fRec33[1]) / fTemp77;
			fRec33[0] = fRec33[1] + 2.0f * fTemp104;
			float fTemp105 = fTemp2 * (fRec33[1] + fTemp104 - fRec34[1]) / fTemp77;
			fRec34[0] = fRec34[1] + 2.0f * fTemp105;
			float fTemp106 = fTemp2 * (fRec34[1] + fTemp105 - fRec35[1]) / fTemp77;
			fRec35[0] = fRec35[1] + 2.0f * fTemp106;
			float fTemp107 = fTemp2 * (fRec35[1] + fTemp106 - fRec36[1]) / fTemp77;
			fRec36[0] = fRec36[1] + 2.0f * fTemp107;
			float fRec37 = fRec36[1] + fTemp107;
			float fTemp108 = fRec12 * fTemp98 * fTemp99 * fTemp101 + fRec32[0] * fRec37;
			float fTemp109 = ((iSlow1) ? fSlow37 : fRec2[0] * fTemp108);
			float fTemp110 = fTemp109 + fSlow47 * fRec38[1];
			fVec10[IOTA0 & 524287] = fTemp110;
			fRec39[0] = fSlow46 * fVec10[(IOTA0 - iSlow49) & 524287] + fSlow50 * fVec10[(IOTA0 - iSlow51) & 524287] - fSlow52 * (fSlow53 * fRec39[2] + fSlow54 * fRec39[1]);
			fRec38[0] = fSlow42 * (fRec39[2] + (fRec39[0] - 2.0f * fRec39[1]));
			float fTemp111 = fTemp0 * fTemp109 + fRec1[0] * fRec38[0];
			fVec11[0] = fTemp111;
			fRec0[0] = 0.995f * fRec0[1] + 0.7f * (fTemp111 - fVec11[1]);
			output0[i0] = static_cast<FAUSTFLOAT>(tanhf(fRec0[0]));
			float fTemp112 = fTemp69 + fTemp22 + fTemp21 * fTemp41 * fTemp58 + fTemp40 * fTemp59;
			float fTemp113 = tanhf((0.3f * fTemp71 * fTemp112 + fRec31[0] * tanhf(0.3f * fTemp72 * fTemp112) * (0.054f * fRec31[0] * mydsp_faustpower2_f(fTemp112) + 1.0f)) / fTemp73);
			float fTemp114 = fRec43[1] + 0.5f * (fTemp2 * fRec44[1] / fTemp77);
			float fTemp115 = 0.5f * (fTemp2 * fTemp114 / fTemp80);
			float fTemp116 = fRec42[1] + fTemp115;
			float fTemp117 = fTemp2 * fTemp116 / fTemp83;
			float fTemp118 = fTemp2 * (fRec41[1] + fTemp117) / fTemp85;
			float fTemp119 = fTemp2 * (fTemp88 * (fTemp113 - 16.5f * (fTemp76 * (fRec44[1] + fTemp2 * (fTemp2 * (0.25f * fTemp116 + 0.125f * fTemp118) / fTemp83 + 0.5f * fTemp114) / fTemp80) / fTemp77)) / fTemp91 + (fTemp116 + 0.5f * fTemp118) / fTemp83 - fRec41[1]) / fTemp77;
			fRec41[0] = fRec41[1] + 2.0f * fTemp119;
			float fTemp120 = fTemp2 * (0.5f * (fTemp93 * (fRec41[1] + fTemp119) + (fTemp114 + 0.5f * fTemp117) / fTemp80) - fRec42[1]) / fTemp77;
			fRec42[0] = fRec42[1] + 2.0f * fTemp120;
			float fTemp121 = fTemp2 * (0.5f * (fTemp95 * (fRec42[1] + fTemp120) + (fRec44[1] + fTemp115) / fTemp77) - fRec43[1]) / fTemp77;
			fRec43[0] = fRec43[1] + 2.0f * fTemp121;
			float fTemp122 = fTemp2 * (0.5f * (fRec43[1] + fTemp121) - fRec44[1]) / fTemp77;
			fRec44[0] = fRec44[1] + 2.0f * fTemp122;
			float fRec45 = fRec44[1] + fTemp122;
			float fTemp123 = fTemp2 * ((fTemp113 - 3.6f * fTemp102 * (fRec49[1] + fTemp2 * (fRec48[1] + fTemp2 * (fRec47[1] + fTemp2 * fRec46[1] / fTemp77) / fTemp77) / fTemp77)) / fTemp103 - fRec46[1]) / fTemp77;
			fRec46[0] = fRec46[1] + 2.0f * fTemp123;
			float fTemp124 = fTemp2 * (fRec46[1] + fTemp123 - fRec47[1]) / fTemp77;
			fRec47[0] = fRec47[1] + 2.0f * fTemp124;
			float fTemp125 = fTemp2 * (fRec47[1] + fTemp124 - fRec48[1]) / fTemp77;
			fRec48[0] = fRec48[1] + 2.0f * fTemp125;
			float fTemp126 = fTemp2 * (fRec48[1] + fTemp125 - fRec49[1]) / fTemp77;
			fRec49[0] = fRec49[1] + 2.0f * fTemp126;
			float fRec50 = fRec49[1] + fTemp126;
			float fTemp127 = fRec45 * fTemp98 * fTemp99 * fTemp101 + fRec32[0] * fRec50;
			float fTemp128 = ((iSlow1) ? fSlow55 : fRec2[0] * fTemp127);
			float fTemp129 = fTemp128 + fSlow47 * fRec51[1];
			fVec12[IOTA0 & 524287] = fTemp129;
			fRec52[0] = fSlow58 * fVec12[(IOTA0 - iSlow60) & 524287] + fSlow61 * fVec12[(IOTA0 - iSlow62) & 524287] - fSlow52 * (fSlow53 * fRec52[2] + fSlow54 * fRec52[1]);
			fRec51[0] = fSlow42 * (fRec52[2] + (fRec52[0] - 2.0f * fRec52[1]));
			float fTemp130 = fTemp0 * fTemp128 + fRec1[0] * fRec51[0];
			fVec13[0] = fTemp130;
			fRec40[0] = 0.995f * fRec40[1] + 0.7f * (fTemp130 - fVec13[1]);
			output1[i0] = static_cast<FAUSTFLOAT>(tanhf(fRec40[0]));
			float fTemp131 = fRec2[0] * (fTemp108 + fTemp127);
			fVec14[0] = fTemp131;
			fRec53[0] = 0.995f * fRec53[1] + 0.35f * (fTemp131 - fVec14[1]);
			output2[i0] = static_cast<FAUSTFLOAT>(tanhf(fRec53[0]));
			iVec0[1] = iVec0[0];
			fRec1[1] = fRec1[0];
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec3[1] = fRec3[0];
			fRec7[1] = fRec7[0];
			fRec6[1] = fRec6[0];
			fRec2[1] = fRec2[0];
			fRec13[1] = fRec13[0];
			fRec14[1] = fRec14[0];
			fRec15[1] = fRec15[0];
			fRec16[1] = fRec16[0];
			fRec19[1] = fRec19[0];
			fRec17[1] = fRec17[0];
			fRec20[1] = fRec20[0];
			fVec1[1] = fVec1[0];
			IOTA0 = IOTA0 + 1;
			fRec21[1] = fRec21[0];
			fRec22[1] = fRec22[0];
			fRec24[1] = fRec24[0];
			fVec3[1] = fVec3[0];
			fRec25[1] = fRec25[0];
			fRec27[1] = fRec27[0];
			fVec5[1] = fVec5[0];
			fRec29[1] = fRec29[0];
			fRec30[1] = fRec30[0];
			fVec8[1] = fVec8[0];
			fRec31[1] = fRec31[0];
			fRec8[1] = fRec8[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			fRec11[1] = fRec11[0];
			fRec32[1] = fRec32[0];
			fRec33[1] = fRec33[0];
			fRec34[1] = fRec34[0];
			fRec35[1] = fRec35[0];
			fRec36[1] = fRec36[0];
			fRec39[2] = fRec39[1];
			fRec39[1] = fRec39[0];
			fRec38[1] = fRec38[0];
			fVec11[1] = fVec11[0];
			fRec0[1] = fRec0[0];
			fRec41[1] = fRec41[0];
			fRec42[1] = fRec42[0];
			fRec43[1] = fRec43[0];
			fRec44[1] = fRec44[0];
			fRec46[1] = fRec46[0];
			fRec47[1] = fRec47[0];
			fRec48[1] = fRec48[0];
			fRec49[1] = fRec49[0];
			fRec52[2] = fRec52[1];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fVec13[1] = fVec13[0];
			fRec40[1] = fRec40[0];
			fVec14[1] = fVec14[0];
			fRec53[1] = fRec53[0];
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
