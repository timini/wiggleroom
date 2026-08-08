#include "euclogic/EucBankCore.hpp"

namespace WiggleRoom {
struct EucBank2Widget : EucBankWidgetT<2> {
    EucBank2Widget(EucBankModuleT<2>* m) : EucBankWidgetT<2>(m, "res/EucBank2.png") {}
};
}

Model* modelEucBank2 = createModel<WiggleRoom::EucBankModuleT<2>, WiggleRoom::EucBank2Widget>("EucBank2");
