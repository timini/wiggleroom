#include "rack.hpp"

using namespace rack;

// Global plugin instance
Plugin* pluginInstance;

// Declare models from modules — all guarded so RELEASE_MODULE_LIST works
#ifdef HAS_INTERSECT
extern Model* modelIntersect;
#endif
#ifdef HAS_CYCLOID
extern Model* modelCycloid;
#endif
#ifdef HAS_GRAVITYCLOCK
extern Model* modelGravityClock;
#endif
#ifdef HAS_OCTOLFO
extern Model* modelOctoLFO;
#endif
#ifdef HAS_THEARCHITECT
extern Model* modelTheArchitect;
#endif
#ifdef HAS_EUCLOGIC
extern Model* modelEuclogic;
#endif
#ifdef HAS_EUCLOGIC2
extern Model* modelEuclogic2;
#endif
#ifdef HAS_EUCLOGIC3
extern Model* modelEuclogic3;
#endif
#ifdef HAS_LADDERLPF
extern Model* modelLadderLPF;
#endif
#ifdef HAS_MODALBELL
extern Model* modelModalBell;
#endif
#ifdef HAS_BIGREVERB
extern Model* modelBigReverb;
#endif
#ifdef HAS_SATURATIONECHO
extern Model* modelSaturationEcho;
#endif
#ifdef HAS_SPECTRALRESONATOR
extern Model* modelSpectralResonator;
#endif
#ifdef HAS_PLUCKEDSTRING
extern Model* modelPluckedString;
#endif
#ifdef HAS_CHAOSFLUTE
extern Model* modelChaosFlute;
#endif
#ifdef HAS_TRIPHASEENSEMBLE
extern Model* modelTriPhaseEnsemble;
#endif
#ifdef HAS_INFINITEFOLDER
extern Model* modelInfiniteFolder;
#endif
#ifdef HAS_SPACECELLO
extern Model* modelSpaceCello;
#endif
#ifdef HAS_THEABYSS
extern Model* modelTheAbyss;
#endif
#ifdef HAS_MATTER
extern Model* modelMatter;
#endif
#ifdef HAS_THECAULDRON
extern Model* modelTheCauldron;
#endif
#ifdef HAS_VEKTORX
extern Model* modelVektorX;
#endif
#ifdef HAS_ANALOGDRUMS
extern Model* modelAnalogDrums;
#endif
#ifdef HAS_ACID9VOICE
extern Model* modelACID9Voice;
#endif
#ifdef HAS_ACID9SEQ
extern Model* modelACID9Seq;
#endif
#ifdef HAS_TETANUSCOIL
extern Model* modelTetanusCoil;
#endif
#ifdef HAS_NUTSHAKER
extern Model* modelNutShaker;
#endif
#ifdef HAS_PHYSICALCHOIR
extern Model* modelPhysicalChoir;
#endif
#ifdef HAS_CHAOSPAD
extern Model* modelChaosPad;
#endif
#ifdef HAS_LINKAGE
extern Model* modelLinkage;
#endif
#ifdef HAS_XFADE
extern Model* modelXFade;
#endif

void init(Plugin* p) {
    pluginInstance = p;

#ifdef HAS_INTERSECT
    p->addModel(modelIntersect);
#endif
#ifdef HAS_CYCLOID
    p->addModel(modelCycloid);
#endif
#ifdef HAS_GRAVITYCLOCK
    p->addModel(modelGravityClock);
#endif
#ifdef HAS_OCTOLFO
    p->addModel(modelOctoLFO);
#endif
#ifdef HAS_THEARCHITECT
    p->addModel(modelTheArchitect);
#endif
#ifdef HAS_EUCLOGIC
    p->addModel(modelEuclogic);
#endif
#ifdef HAS_EUCLOGIC2
    p->addModel(modelEuclogic2);
#endif
#ifdef HAS_EUCLOGIC3
    p->addModel(modelEuclogic3);
#endif
#ifdef HAS_LADDERLPF
    p->addModel(modelLadderLPF);
#endif
#ifdef HAS_MODALBELL
    p->addModel(modelModalBell);
#endif
#ifdef HAS_BIGREVERB
    p->addModel(modelBigReverb);
#endif
#ifdef HAS_SATURATIONECHO
    p->addModel(modelSaturationEcho);
#endif
#ifdef HAS_SPECTRALRESONATOR
    p->addModel(modelSpectralResonator);
#endif
#ifdef HAS_PLUCKEDSTRING
    p->addModel(modelPluckedString);
#endif
#ifdef HAS_CHAOSFLUTE
    p->addModel(modelChaosFlute);
#endif
#ifdef HAS_TRIPHASEENSEMBLE
    p->addModel(modelTriPhaseEnsemble);
#endif
#ifdef HAS_INFINITEFOLDER
    p->addModel(modelInfiniteFolder);
#endif
#ifdef HAS_SPACECELLO
    p->addModel(modelSpaceCello);
#endif
#ifdef HAS_THEABYSS
    p->addModel(modelTheAbyss);
#endif
#ifdef HAS_MATTER
    p->addModel(modelMatter);
#endif
#ifdef HAS_THECAULDRON
    p->addModel(modelTheCauldron);
#endif
#ifdef HAS_VEKTORX
    p->addModel(modelVektorX);
#endif
#ifdef HAS_ANALOGDRUMS
    p->addModel(modelAnalogDrums);
#endif
#ifdef HAS_ACID9VOICE
    p->addModel(modelACID9Voice);
#endif
#ifdef HAS_ACID9SEQ
    p->addModel(modelACID9Seq);
#endif
#ifdef HAS_TETANUSCOIL
    p->addModel(modelTetanusCoil);
#endif
#ifdef HAS_NUTSHAKER
    p->addModel(modelNutShaker);
#endif
#ifdef HAS_PHYSICALCHOIR
    p->addModel(modelPhysicalChoir);
#endif
#ifdef HAS_CHAOSPAD
    p->addModel(modelChaosPad);
#endif
#ifdef HAS_LINKAGE
    p->addModel(modelLinkage);
#endif
#ifdef HAS_XFADE
    p->addModel(modelXFade);
#endif
}
