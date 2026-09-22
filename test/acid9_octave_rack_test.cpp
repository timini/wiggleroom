#include "engine/Engine.hpp"
#undef PRIVATE
#include "modules/ACID9Voice/ACID9Voice.cpp"
#include <iostream>
#include <memory>
rack::Plugin* pluginInstance = nullptr;
struct Voice : WiggleRoom::ACID9Voice {
    float pitch() { return faustDsp.getParamValue(dspParams[22]); }
};
void check(bool ok) { if (!ok) { std::cerr << "ACID9 octave regression failed\n"; std::exit(1); } }
int main() {
    rack::Context context; context.engine = new rack::engine::Engine; rack::contextSet(&context);
    auto a = std::make_unique<Voice>(); auto b = std::make_unique<Voice>();
    rack::Module::ProcessArgs args{}; args.sampleRate = 48000; args.sampleTime = 1.f / 48000;
    static_assert(Voice::OCTAVE_PARAM == 15 && Voice::DELAY_GHOST_PARAM == 14);
    check(a->params[Voice::OCTAVE_PARAM].getValue() == 0);
    check(a->paramQuantities[Voice::OCTAVE_PARAM]->snapEnabled);
    for (int octave = -3; octave <= 3; ++octave) {
        a->params[Voice::OCTAVE_PARAM].setValue(octave);
        a->inputs[Voice::VOCT_INPUT].setVoltage(.25f); a->process(args);
        check(a->pitch() == octave + .25f);
    }
    a->params[Voice::OCTAVE_PARAM].setValue(1.7f); a->process(args); check(a->pitch() == 2.25f);
    a->inputs[Voice::VOCT_INPUT].setVoltage(10); a->process(args); check(a->pitch() == 10);
    a->params[Voice::OCTAVE_PARAM].setValue(-3); a->inputs[Voice::VOCT_INPUT].setVoltage(-5); a->process(args); check(a->pitch() == -5);
    // Transposing the knob must be identical to adding the same voltage externally.
    a->onReset({}); b->onReset({});
    a->params[Voice::OCTAVE_PARAM].setValue(-2); a->inputs[Voice::VOCT_INPUT].setVoltage(.25f);
    b->inputs[Voice::VOCT_INPUT].setVoltage(-1.75f);
    a->inputs[Voice::GATE_INPUT].setVoltage(10); b->inputs[Voice::GATE_INPUT].setVoltage(10);
    for(int i=0;i<4800;++i) { a->process(args); b->process(args); for(int o=0;o<Voice::OUTPUTS_LEN;++o) check(a->outputs[o].getVoltage()==b->outputs[o].getVoltage()); }
    std::cout << "PASS: octave steps, default, pitch limits and audio equivalence to V/oct transpose\n";
}
