#include "euclogic/EucMixCore.hpp"

namespace WiggleRoom {
struct EucMix2Widget : EucMixWidgetT<2> {
    EucMix2Widget(EucMixModuleT<2>* m) : EucMixWidgetT<2>(m, "res/EucMix2.png") {}
};
}

Model* modelEucMix2 = createModel<WiggleRoom::EucMixModuleT<2>, WiggleRoom::EucMix2Widget>("EucMix2");
