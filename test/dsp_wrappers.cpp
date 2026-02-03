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

// ==== LadderLPF ====
#define FAUST_MODULE_NAME LadderLPF
#include "ladder_lpf.hpp"
std::unique_ptr<AbstractDSP> createLadderLPF() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_LadderLPF::VCVRackDSP>>();
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

// ==== TriPhaseEnsemble ====
#define FAUST_MODULE_NAME TriPhaseEnsemble
#include "tri_phase_ensemble.hpp"
std::unique_ptr<AbstractDSP> createTriPhaseEnsemble() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_TriPhaseEnsemble::VCVRackDSP>>();
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

// ==== TheCauldron ====
#define FAUST_MODULE_NAME TheCauldron
#include "the_cauldron.hpp"
std::unique_ptr<AbstractDSP> createTheCauldron() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_TheCauldron::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== VektorX ====
#define FAUST_MODULE_NAME VektorX
#include "vektorx.hpp"
std::unique_ptr<AbstractDSP> createVektorX() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_VektorX::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== TR808 ====
#define FAUST_MODULE_NAME TR808
#include "tr808.hpp"
std::unique_ptr<AbstractDSP> createTR808() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_TR808::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== ACID9Voice ====
#define FAUST_MODULE_NAME ACID9Voice
#include "acid9voice.hpp"
std::unique_ptr<AbstractDSP> createACID9Voice() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_ACID9Voice::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== TetanusCoil ====
#define FAUST_MODULE_NAME TetanusCoil
#include "tetanus_coil.hpp"
std::unique_ptr<AbstractDSP> createTetanusCoil() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_TetanusCoil::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== NutShaker ====
#define FAUST_MODULE_NAME NutShaker
#include "nutshaker.hpp"
std::unique_ptr<AbstractDSP> createNutShaker() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_NutShaker::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== PhysicalChoir ====
#define FAUST_MODULE_NAME PhysicalChoir
#include "physical_choir.hpp"
std::unique_ptr<AbstractDSP> createPhysicalChoir() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_PhysicalChoir::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== ChaosPad ====
#define FAUST_MODULE_NAME ChaosPad
#include "chaos_pad.hpp"
std::unique_ptr<AbstractDSP> createChaosPad() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_ChaosPad::VCVRackDSP>>();
}

#undef __mydsp_H__

// ==== Linkage ====
#define FAUST_MODULE_NAME Linkage
#include "linkage.hpp"
std::unique_ptr<AbstractDSP> createLinkage() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_Linkage::VCVRackDSP>>();
}
