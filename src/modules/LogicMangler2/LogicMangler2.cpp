#include "euclogic/LogicManglerCore.hpp"

namespace WiggleRoom {
struct LogicMangler2Widget : LogicManglerWidgetT<2> {
    LogicMangler2Widget(LogicManglerModuleT<2>* m) : LogicManglerWidgetT<2>(m, "res/LogicMangler2.png") {}
};
}

Model* modelLogicMangler2 = createModel<WiggleRoom::LogicManglerModuleT<2>, WiggleRoom::LogicMangler2Widget>("LogicMangler2");
