#include "euclogic/EucSeqCore.hpp"

namespace WiggleRoom {
struct EucSeq3Widget : EucSeqWidgetT<3> {
    EucSeq3Widget(EucSeqModuleT<3>* m) : EucSeqWidgetT<3>(m, "res/EucSeq3.png") {}
};
}

Model* modelEucSeq3 = createModel<WiggleRoom::EucSeqModuleT<3>, WiggleRoom::EucSeq3Widget>("EucSeq3");
