/**
 * DSP Wrapper implementations for all Faust modules
 *
 * Uses the namespaced classes directly to avoid type alias conflicts.
 */

// Disable global VCVRackDSP alias since we include multiple Faust headers
#define FAUST_NO_GLOBAL_ALIAS

#include "AbstractDSP.hpp"
#include <memory>

// Template wrapper that bridges Faust DSP to AbstractDSP interface
template<typename DSP>
class DSPWrapper : public AbstractDSP {
    DSP dsp;
public:
    void init(int sampleRate) override { dsp.init(sampleRate); }
    void compute(int count, float** inputs, float** outputs) override {
        dsp.compute(count, inputs, outputs);
    }
    int getNumInputs() const override { return dsp.getNumInputs(); }
    int getNumOutputs() const override { return dsp.getNumOutputs(); }
    int getNumParams() const override { return dsp.getNumParams(); }
    void setParamValue(int index, float value) override { dsp.setParamValue(index, value); }
    float getParamValue(int index) const override { return dsp.getParamValue(index); }
    const char* getParamPath(int index) const override { return dsp.getParamPath(index); }
    float getParamMin(int index) const override { return dsp.getParamMin(index); }
    float getParamMax(int index) const override { return dsp.getParamMax(index); }
    float getParamInit(int index) const override { return dsp.getParamInit(index); }
    int getParamIndex(const char* name) const override { return dsp.getParamIndex(name); }
};

// Include each Faust header with its unique module name
// Use the namespaced class directly to avoid global VCVRackDSP conflicts

// ==== MoogLPF ====
#define FAUST_MODULE_NAME MoogLPF
#include "moog_lpf.hpp"
std::unique_ptr<AbstractDSP> createMoogLPF() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_MoogLPF::VCVRackDSP>>();
}

// Reset preprocessor state
#undef __mydsp_H__

// ==== BigReverb ====
#define FAUST_MODULE_NAME BigReverb
#include "big_reverb.hpp"
std::unique_ptr<AbstractDSP> createBigReverb() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_BigReverb::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== SaturationEcho ====
#define FAUST_MODULE_NAME SaturationEcho
#include "saturation_echo.hpp"
std::unique_ptr<AbstractDSP> createSaturationEcho() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_SaturationEcho::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== SpectralResonator ====
#define FAUST_MODULE_NAME SpectralResonator
#include "spectral_resonator.hpp"
std::unique_ptr<AbstractDSP> createSpectralResonator() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_SpectralResonator::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== ModalBell ====
#define FAUST_MODULE_NAME ModalBell
#include "modal_bell.hpp"
std::unique_ptr<AbstractDSP> createModalBell() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_ModalBell::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== PluckedString ====
#define FAUST_MODULE_NAME PluckedString
#include "plucked_string.hpp"
std::unique_ptr<AbstractDSP> createPluckedString() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_PluckedString::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== ChaosFlute ====
#define FAUST_MODULE_NAME ChaosFlute
#include "chaos_flute.hpp"
std::unique_ptr<AbstractDSP> createChaosFlute() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_ChaosFlute::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== SolinaEnsemble ====
#define FAUST_MODULE_NAME SolinaEnsemble
#include "solina_ensemble.hpp"
std::unique_ptr<AbstractDSP> createSolinaEnsemble() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_SolinaEnsemble::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== InfiniteFolder ====
#define FAUST_MODULE_NAME InfiniteFolder
#include "infinite_folder.hpp"
std::unique_ptr<AbstractDSP> createInfiniteFolder() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_InfiniteFolder::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== SpaceCello ====
#define FAUST_MODULE_NAME SpaceCello
#include "space_cello.hpp"
std::unique_ptr<AbstractDSP> createSpaceCello() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_SpaceCello::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== TheAbyss ====
#define FAUST_MODULE_NAME TheAbyss
#include "the_abyss.hpp"
std::unique_ptr<AbstractDSP> createTheAbyss() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_TheAbyss::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== Matter ====
#define FAUST_MODULE_NAME Matter
#include "matter.hpp"
std::unique_ptr<AbstractDSP> createMatter() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_Matter::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== Linkage ====
#define FAUST_MODULE_NAME Linkage
#include "linkage.hpp"
std::unique_ptr<AbstractDSP> createLinkage() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_Linkage::VCVRackDSP>>();
}
