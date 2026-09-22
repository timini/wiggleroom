#include "engine/Engine.hpp"
#undef PRIVATE
#include "modules/ACID9Voice/ACID9Voice.cpp"
#include <memory>
#include <iostream>
#include <chrono>
rack::Plugin* pluginInstance=nullptr;
using M=WiggleRoom::ACID9Voice;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){rack::Context context;context.engine=new rack::engine::Engine;rack::contextSet(&context);
 auto m=std::make_unique<M>();rack::Module::ProcessArgs args{};
 auto start=std::chrono::steady_clock::now();
 for(float sr:{44100.f,48000.f,96000.f}){
  args.sampleRate=sr;args.sampleTime=1/sr;rack::Module::SampleRateChangeEvent e{};e.sampleRate=sr;e.sampleTime=1/sr;m->onSampleRateChange(e);
  for(float mode:{0.f,.25f,.5f,.75f,1.f}){m->params[M::FILTER_MODE_PARAM].setValue(mode);
   for(int n=0;n<int(sr);++n){int step=n/int(sr*.05);m->inputs[M::GATE_INPUT].setVoltage(n%int(sr*.05)<int(sr*.04)?10:0);m->inputs[M::VOCT_INPUT].setVoltage(-2+(step%5)/12.f);m->inputs[M::SLIDE_INPUT].setVoltage(step%3==0?10:0);m->inputs[M::ACCENT_INPUT].setVoltage(step%2?10:0);
    m->params[M::CUTOFF_PARAM].setValue(20*std::pow(1000.f,n/sr));m->params[M::RESONANCE_PARAM].setValue(1);m->params[M::GRIT_PARAM].setValue(1);m->process(args);
    for(int o=0;o<3;++o){float v=m->outputs[o].getVoltage();if(!std::isfinite(v)||std::abs(v)>=20){std::cerr<<"mode="<<mode<<" sr="<<sr<<" frame="<<n<<" output="<<o<<" v="<<v<<std::endl;check(false,"Unstable voice output");}}
   }
  }
  m->onBypass({});check(!m->articulation.active&&!m->articulation.armed,"Bypass retained articulation");
 }
 rack::Plugin plugin;plugin.slug="WiggleRoom";plugin.version="2.1.0";rack::Model model;model.slug="ACID9Voice";model.plugin=&plugin;plugin.models.push_back(&model);rack::plugin::plugins.push_back(&plugin);m->model=&model;
 m->params[M::SLIDE_TIME_PARAM].setValue(125);m->params[M::ACCENT_AMOUNT_PARAM].setValue(.8);m->params[M::FILTER_MODE_PARAM].setValue(.375f);auto* j=m->toJson();m->fromJson(j);check(m->params[M::SLIDE_TIME_PARAM].getValue()==125,"Lost slide control");check(!m->articulation.active,"Load retained runtime");check(m->params[M::FILTER_MODE_PARAM].getValue()==.375f,"Lost fractional filter morph");
 auto* ps=json_object_get(j,"params");for(size_t i=json_array_size(ps);i>0;--i)if(json_integer_value(json_object_get(json_array_get(ps,i-1),"id"))>=M::SLIDE_TIME_PARAM)json_array_remove(ps,i-1);
 m->fromJson(j);json_decref(j);check(m->params[M::SLIDE_TIME_PARAM].getValue()==60&&m->params[M::ACCENT_AMOUNT_PARAM].getValue()==.5,"Old patch defaults wrong");rack::plugin::plugins.clear();plugin.models.clear();
 std::cout<<"PASS: both filters, full voice sweeps, lifecycle and patch migration; 15 s audio CPU time "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
}
