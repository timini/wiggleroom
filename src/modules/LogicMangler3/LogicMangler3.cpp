#include "euclogic/LogicManglerCore.hpp"

namespace WiggleRoom {
struct LogicMangler3Widget : LogicManglerWidgetT<3> {
    LogicMangler3Widget(LogicManglerModuleT<3>* m) : LogicManglerWidgetT<3>(m, "res/LogicMangler3.png") {}
};
}

Model* modelLogicMangler3 = createModel<WiggleRoom::LogicManglerModuleT<3>, WiggleRoom::LogicMangler3Widget>("LogicMangler3");
