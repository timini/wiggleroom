#include "euclogic/EucMixCore.hpp"

namespace WiggleRoom {
struct EucMix3Widget : EucMixWidgetT<3> {
    EucMix3Widget(EucMixModuleT<3>* m) : EucMixWidgetT<3>(m, "res/EucMix3.png") {}
};
}

Model* modelEucMix3 = createModel<WiggleRoom::EucMixModuleT<3>, WiggleRoom::EucMix3Widget>("EucMix3");
