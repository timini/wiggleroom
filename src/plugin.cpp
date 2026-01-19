#include "rack.hpp"

using namespace rack;

// Global plugin instance
Plugin* pluginInstance;

// Declare models from modules
extern Model* modelIntersect;
extern Model* modelCycloid;
extern Model* modelGravityClock;
extern Model* modelOctoLFO;
extern Model* modelTheArchitect;
#ifdef HAS_MOOGLPF
extern Model* modelMoogLPF;
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
#ifdef HAS_SOLINAENSEMBLE
extern Model* modelSolinaEnsemble;
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

void init(Plugin* p) {
    pluginInstance = p;

    // Register all modules
    p->addModel(modelIntersect);
    p->addModel(modelCycloid);
    p->addModel(modelGravityClock);
    p->addModel(modelOctoLFO);
    p->addModel(modelTheArchitect);
#ifdef HAS_MOOGLPF
    p->addModel(modelMoogLPF);
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
#ifdef HAS_SOLINAENSEMBLE
    p->addModel(modelSolinaEnsemble);
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
}
