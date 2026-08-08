#include "euclogic/EucBankCore.hpp"

namespace WiggleRoom {
struct EucBank3Widget : EucBankWidgetT<3> {
    EucBank3Widget(EucBankModuleT<3>* m) : EucBankWidgetT<3>(m, "res/EucBank3.png") {}
};
}

Model* modelEucBank3 = createModel<WiggleRoom::EucBankModuleT<3>, WiggleRoom::EucBank3Widget>("EucBank3");
