# Adding a New Module

This guide walks through creating a new Faust DSP module from scratch.

## Overview

Adding a new module requires:
1. Faust DSP file
2. C++ wrapper
3. CMakeLists.txt
4. Registration in plugin.cpp and plugin.json
5. Panel graphic
6. Test harness integration

## Step 1: Create Faust DSP

Create `src/modules/MyModule/my_module.dsp`:

```faust
import("stdfaust.lib");

declare name "My Module";
declare author "WiggleRoom";
declare description "Description here";

// Parameters (alphabetically indexed)
amount = hslider("amount", 0.5, 0, 1, 0.01) : si.smoo;
freq = hslider("freq", 440, 20, 20000, 1) : si.smoo;

// DSP
process = os.osc(freq) * amount;
```

## Step 2: Create C++ Wrapper

Create `src/modules/MyModule/MyModule.cpp`:

```cpp
#include "rack.hpp"
#include "FaustModule.hpp"
#define FAUST_MODULE_NAME MyModule
#include "my_module.hpp"

using namespace rack;

extern Plugin* pluginInstance;

namespace WiggleRoom {

struct MyModule : FaustModule<VCVRackDSP> {
    enum ParamId { AMOUNT_PARAM, FREQ_PARAM, PARAMS_LEN };
    enum InputId { FREQ_CV_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    MyModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(AMOUNT_PARAM, 0.f, 1.f, 0.5f, "Amount");
        configParam(FREQ_PARAM, 20.f, 20000.f, 440.f, "Frequency", " Hz");
        configInput(FREQ_CV_INPUT, "Frequency CV");
        configOutput(AUDIO_OUTPUT, "Audio");

        // Faust params alphabetically: amount=0, freq=1
        mapParam(AMOUNT_PARAM, 0);
        mapParam(FREQ_PARAM, 1);
        mapCVInput(FREQ_CV_INPUT, 1, true);  // V/Oct
    }

    void process(const ProcessArgs& args) override {
        if (!initialized) {
            faustDsp.init(static_cast<int>(args.sampleRate));
            initialized = true;
        }

        updateFaustParams();

        float* nullInputs[1] = { nullptr };
        float output = 0.f;
        float* outputPtr = &output;

        faustDsp.compute(1, nullInputs, &outputPtr);
        outputs[AUDIO_OUTPUT].setVoltage(output * 5.f);
    }
};

struct MyModuleWidget : ModuleWidget {
    MyModuleWidget(MyModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MyModule.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));

        float x = box.size.x / 2.f;
        addParam(createParamCentered<RoundBlackKnob>(Vec(x, 60), module, MyModule::FREQ_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(x, 120), module, MyModule::AMOUNT_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x, 180), module, MyModule::FREQ_CV_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(x, 240), module, MyModule::AUDIO_OUTPUT));
    }
};

} // namespace WiggleRoom

Model* modelMyModule = createModel<WiggleRoom::MyModule, WiggleRoom::MyModuleWidget>("MyModule");
```

## Step 3: Create CMakeLists.txt

Create `src/modules/MyModule/CMakeLists.txt`:

```cmake
add_faust_dsp(my_module.dsp my_module.hpp)

add_library(MyModule_Module STATIC MyModule.cpp ${FAUST_HEADER})
target_link_libraries(MyModule_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(MyModule_Module PRIVATE CommonLib)
endif()
target_include_directories(MyModule_Module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/faust_gen)
```

## Step 4: Register Module

### In `src/plugin.cpp`:

```cpp
#ifdef HAS_MYMODULE
extern Model* modelMyModule;
#endif

void init(Plugin* p) {
    // ...
    #ifdef HAS_MYMODULE
    p->addModel(modelMyModule);
    #endif
}
```

### In root `CMakeLists.txt`:

Find the section with other `HAS_*` definitions and add:

```cmake
if(MyModule_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MYMODULE=1)
endif()
```

**Without this step, the module will not be registered!**

### In `plugin.json`:

```json
{
  "slug": "MyModule",
  "name": "My Module",
  "description": "Description here",
  "tags": ["Oscillator"]
}
```

## Step 5: Create Panel

Create `res/MyModule.svg` (or use AI-generated PNG):

```bash
# Add to scripts/generate_faceplate.py MODULE_HP dict first
just faceplate MyModule
```

Standard panel dimensions:
- Width: `HP × 15.24` mm (6-12 HP recommended)
- Height: 128.5 mm (3U)

## Step 6: Add to Test Harness

### In `test/faust_render.cpp`:

```cpp
// Add forward declaration
std::unique_ptr<AbstractDSP> createMyModule();

// Add to factory
if (moduleName == "MyModule") return createMyModule();

// Add to module list
return {"...", "MyModule"};

// Add to type detection if needed
if (name == "MyModule") return ModuleType::Instrument;
```

### In `test/dsp_wrappers.cpp`:

```cpp
#undef __mydsp_H__

#define FAUST_MODULE_NAME MyModule
#include "my_module.hpp"
std::unique_ptr<AbstractDSP> createMyModule() {
    return std::make_unique<DSPWrapper<FaustGenerated::NS_MyModule::VCVRackDSP>>();
}
```

### In `test/CMakeLists.txt`:

```cmake
set(FAUST_MODULES
    # ...existing modules...
    MyModule
)

# Add mapping
elseif(MODULE STREQUAL "MyModule")
    set(DSP_FILE "my_module.dsp")
    set(HPP_FILE "my_module.hpp")
endif()
```

## Step 7: Create Test Config (Optional)

Create `src/modules/MyModule/test_config.json`:

```json
{
  "module_type": "instrument",
  "description": "Brief description of your module",
  "quality_thresholds": {
    "thd_max_percent": 15.0,
    "clipping_max_percent": 1.0
  },
  "test_scenarios": [
    {"name": "default", "duration": 2.0}
  ]
}
```

Module types: `instrument`, `filter`, `effect`, `resonator`, `utility`

## Step 8: Build and Test

```bash
just build
python3 test/test_framework.py --module MyModule -v
just install
```

## Checklist

- [ ] Faust DSP file created
- [ ] C++ wrapper created
- [ ] CMakeLists.txt created
- [ ] Module registered in plugin.cpp
- [ ] Compile definition added to root CMakeLists.txt
- [ ] Module added to plugin.json
- [ ] Panel graphic created
- [ ] Test harness integration
- [ ] Build succeeds
- [ ] Tests pass

## Next Steps

- [Testing](testing.md) - Run quality tests
- [CI Pipeline](../ci/pipeline.md) - Understand CI checks
