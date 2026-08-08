#include "euclogic/EucSeqCore.hpp"

namespace WiggleRoom {
struct EucSeq2Widget : EucSeqWidgetT<2> {
    EucSeq2Widget(EucSeqModuleT<2>* m) : EucSeqWidgetT<2>(m, "res/EucSeq2.png") {}
};
}

Model* modelEucSeq2 = createModel<WiggleRoom::EucSeqModuleT<2>, WiggleRoom::EucSeq2Widget>("EucSeq2");
