// Tests the actual Rack module adapter and the committed Faust DSP together.
// This pinned-SDK test executable acts as a Rack host, so it creates an Engine.
// The distributed plugin itself uses only the public rack.hpp API.
#include "engine/Engine.hpp"
#undef PRIVATE
#include "rack.hpp"
#include <memory>
#include <fstream>
#include <chrono>
#include <limits>
#include <iostream>
rack::Plugin* pluginInstance = nullptr;
#include "../src/modules/ACID9Voice/ACID9Voice.cpp"
using M=WiggleRoom::ACID9Voice;
struct Probe : M {
    float value(const std::string& name) {
        for(int i=0;i<faustDsp.getNumParams();++i) {
            std::string p=faustDsp.getParamPath(i);
            if(p.substr(p.find_last_of('/')+1)==name)return faustDsp.getParamValue(i);
        }
        throw std::runtime_error("Missing Faust parameter: "+name);
    }
};
void check(bool x,const std::string& why){if(!x)throw std::runtime_error(why);}
struct Rig {
    std::unique_ptr<Probe> m{new Probe};
    rack::Module::ProcessArgs a{};
    Rig(float sr=48000){a.sampleRate=sr;a.sampleTime=1/sr;m->model=modelACID9Voice;tick();}
    void input(int id,float v){m->inputs[id].channels=1;m->inputs[id].setVoltage(v);}
    float tick(){m->process(a);for(auto& o:m->outputs)check(std::isfinite(o.getVoltage()),"Non-finite output");return m->outputs[M::LEFT_OUTPUT].getVoltage();}
    double run(int n){double e=0;for(int i=0;i<n;++i){float v=tick();e+=v*v;}return e/std::max(n,1);}
    void rate(float sr){a.sampleRate=sr;a.sampleTime=1/sr;rack::Module::SampleRateChangeEvent e{};e.sampleRate=sr;e.sampleTime=1/sr;m->onSampleRateChange(e);}
};
std::vector<float> note(float sr,int param=-1,float value=0,int input=-1,float voltage=0){
 Rig r(sr);r.input(M::GATE_INPUT,10);r.input(M::VOCT_INPUT,-1);if(param>=0)r.m->params[param].setValue(value);if(input>=0)r.input(input,voltage);
 std::vector<float>a;for(int i=0;i<int(sr*.6);++i)a.push_back(r.tick());return a;
}
double difference(const std::vector<float>&a,const std::vector<float>&b){double v=0;for(size_t i=0;i<a.size();++i)v+=std::pow(a[i]-b[i],2);return v/a.size();}
void wav(const std::string& path,const std::vector<float>& data,int sr){
 std::ofstream f(path,std::ios::binary);auto u16=[&](uint16_t x){f.put(x&255);f.put(x>>8);};auto u32=[&](uint32_t x){u16(x&65535);u16(x>>16);};
 f.write("RIFF",4);u32(36+data.size()*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(sr);u32(sr*2);u16(2);u16(16);f.write("data",4);u32(data.size()*2);for(float x:data)u16(static_cast<int16_t>(std::max(-1.f,std::min(1.f,x/5))*30000));
}
int main(int argc,char**argv){
 try {
 rack::Context context;context.engine=new rack::engine::Engine;rack::contextSet(&context);
 rack::Plugin plugin;plugin.slug="WiggleRoom";plugin.version="2.1.2";plugin.addModel(modelACID9Voice);pluginInstance=&plugin;rack::plugin::plugins.push_back(&plugin);
 for(float sr:{44100.f,48000.f,96000.f,192000.f}){
  Rig r(sr);check(r.run(sr/5)<1e-12,"Unpatched voice should be silent");r.input(M::GATE_INPUT,10);check(r.run(sr/3)>1e-5,"Gate does not sound");r.input(M::GATE_INPUT,0);r.run(sr);check(r.run(sr/4)<1e-8,"Gate release does not settle");r.input(M::GATE_INPUT,10);check(r.run(sr/4)>1e-5,"Gate did not retrigger");
  auto base=note(sr);
  for(auto pv:std::vector<std::pair<int,float>>{{M::SHAPE_PARAM,1},{M::ISOTOPE_PARAM,1},{M::SUB_LEVEL_PARAM,1},{M::CUTOFF_PARAM,4000},{M::RESONANCE_PARAM,.8},{M::ENV_MOD_PARAM,-.8},{M::DECAY_PARAM,1.5},{M::FILTER_MODE_PARAM,1},{M::GRIT_PARAM,1},{M::DELAY_MIX_PARAM,.8}})
   check(difference(base,note(sr,pv.first,pv.second))>1e-7,"Inaudible parameter "+std::to_string(pv.first));
  for(auto iv:std::vector<std::pair<int,float>>{{M::VOCT_INPUT,0},{M::ACCENT_INPUT,10},{M::CUTOFF_CV_INPUT,2},{M::FM_INPUT,2},{M::SHAPE_CV_INPUT,8},{M::ISOTOPE_CV_INPUT,8},{M::DECAY_CV_INPUT,5}})
   check(difference(base,note(sr,-1,0,iv.first,iv.second))>1e-7,"Inaudible CV "+std::to_string(iv.first));
  Rig insert(sr);insert.input(M::GATE_INPUT,10);insert.input(M::RETURN_INPUT,0);check(insert.run(sr/2)<1e-10,"Patched return does not replace dry voice");check(std::abs(insert.m->outputs[M::SEND_OUTPUT].getVoltage())>1e-7,"Send stops when return connected");
  for(float ms:{100.f,2000.f}) {
   Rig delay(sr);delay.input(M::RETURN_INPUT,0);delay.m->params[M::DELAY_MIX_PARAM].setValue(1);delay.m->params[M::DELAY_FB_PARAM].setValue(0);delay.m->params[M::DELAY_TIME_PARAM].setValue(ms);delay.run(sr/2);delay.input(M::RETURN_INPUT,5);
   int firstL=-1,firstR=-1;for(int i=0;i<int(sr*2.2);++i){delay.tick();if(i==0)delay.input(M::RETURN_INPUT,0);if(firstL<0&&std::abs(delay.m->outputs[0].getVoltage())>.01)firstL=i;if(firstR<0&&std::abs(delay.m->outputs[1].getVoltage())>.01)firstR=i;}
   check(std::abs(firstL-sr*ms/1000)<5,"Wrong left delay time");check(std::abs(firstR-sr*ms*1.05/1000)<5,"Wrong right delay time / buffer too short");
  }
  Rig clock(sr);clock.run(sr/3);clock.input(M::CLOCK_INPUT,10);clock.tick();check(!clock.m->clockSynced,"First clock edge used startup time as tempo");clock.input(M::CLOCK_INPUT,0);clock.run(sr/2-1);clock.input(M::CLOCK_INPUT,10);clock.tick();check(clock.m->clockSynced,"Clock failed to sync");
  for(int k=0;k<5;++k){clock.m->params[M::DELAY_RATIO_PARAM].setValue(k);clock.tick();check(std::abs(clock.m->value("delay_time")-std::min(2000.f,125.f*std::pow(2.f,float(k))))<.2,"Wrong delay clock ratio");}
  clock.input(M::CLOCK_INPUT,0);clock.run(int(sr*2.1));check(!clock.m->clockSynced,"Clock timeout failed");
  clock.rate(sr==96000?48000:96000);check(!clock.m->clockSynced,"Sample-rate change retained old sample-count tempo");
  clock.m->clockSynced=true;clock.m->inputs[M::CLOCK_INPUT].channels=0;clock.tick();check(!clock.m->clockSynced,"Unplugged clock did not return to manual delay");
  r.m->params[M::GRIT_PARAM].setValue(.78);json_t* j=r.m->toJson();Rig restored(sr);restored.m->fromJson(j);json_decref(j);check(std::abs(restored.m->params[M::GRIT_PARAM].getValue()-.78)<1e-6,"Patch restore lost parameters");
  r.m->onReset({});check(r.m->params[M::GRIT_PARAM].getValue()==0,"Reset did not restore defaults");check(!r.m->clockSynced,"Reset retained clock state");
  Rig extreme(sr);extreme.input(M::GATE_INPUT,10);for(int p=0;p<M::PARAMS_LEN;++p){auto*q=extreme.m->getParamQuantity(p);extreme.m->params[p].setValue(q->getMaxValue());}extreme.run(sr*2);for(int i=0;i<M::INPUTS_LEN;++i)extreme.input(i,std::numeric_limits<float>::quiet_NaN());extreme.run(sr/10);for(int i=0;i<M::INPUTS_LEN;++i)extreme.input(i,1000);extreme.run(sr/10);
  std::cout<<"PASS "<<sr<<" Hz: note/gate, knobs/CV, return/send, stereo delay lengths, sync ratios, sample-rate changes, patch save/load/reset, extremes\n";
 }
 // Controls requiring a non-default signal path.
 for(auto setting:std::vector<std::pair<int,float>>{{M::SUB_MODE_PARAM,1},{M::DELAY_GHOST_PARAM,1},{M::DELAY_FB_PARAM,.8},{M::DELAY_TIME_PARAM,400}}){
  auto render=[&](bool changed){Rig r;r.input(M::GATE_INPUT,10);r.m->params[M::SUB_LEVEL_PARAM].setValue(.6);r.m->params[M::DELAY_MIX_PARAM].setValue(.7);if(changed)r.m->params[setting.first].setValue(setting.second);std::vector<float>v;for(int i=0;i<48000;++i)v.push_back(r.tick());return v;};
  check(difference(render(false),render(true))>1e-7,"Inaudible sub/delay control");
 }
 auto glide=[](bool slide){Rig r;r.input(M::GATE_INPUT,10);r.input(M::VOCT_INPUT,-2);r.run(24000);r.input(M::SLIDE_INPUT,slide?10:0);r.input(M::VOCT_INPUT,0);std::vector<float>v;for(int i=0;i<4800;++i)v.push_back(r.tick());return v;};
 check(difference(glide(false),glide(true))>1e-5,"Slide does not change pitch transition");
 // Pitch ratio measured from rising zero crossings after settling, with a simple saw tone.
 auto pitch=[](float v){Rig r;r.input(M::VOCT_INPUT,v);r.input(M::GATE_INPUT,10);r.m->params[M::CUTOFF_PARAM].setValue(15000);r.m->params[M::RESONANCE_PARAM].setValue(0);r.m->params[M::ENV_MOD_PARAM].setValue(0);r.run(48000);int crossings=0;float last=r.tick();for(int i=0;i<48000;++i){float x=r.tick();if(last<=0&&x>0)++crossings;last=x;}return crossings;};
 int f0=pitch(-1),f1=pitch(0);check(std::abs(double(f1)/f0-2)<.03,"V/Oct does not double pitch");
 Rig soak;soak.input(M::GATE_INPUT,10);soak.m->params[M::DELAY_FB_PARAM].setValue(.95);soak.m->params[M::DELAY_MIX_PARAM].setValue(.7);auto start=std::chrono::steady_clock::now();soak.run(48000*60);double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<"PASS 60-second feedback soak; "<<sec<<" s CPU wall time, "<<sec/60*100<<"% of one real-time core\n";
 if(argc>1){Rig r;std::vector<float>audio;for(int i=0;i<48000*12;++i){int step=i/6000;r.input(M::GATE_INPUT,i%6000<4000?10:0);r.input(M::VOCT_INPUT,-2+float((step*7)%12)/12);r.input(M::ACCENT_INPUT,step%4==0?10:0);r.input(M::SLIDE_INPUT,step%4==3?10:0);r.m->params[M::CUTOFF_PARAM].setValue(300+3000.f*i/(48000*12));r.m->params[M::RESONANCE_PARAM].setValue(.7);r.m->params[M::GRIT_PARAM].setValue(.4);r.m->params[M::DELAY_MIX_PARAM].setValue(.2);audio.push_back(r.tick());}wav(argv[1],audio,48000);}
 std::cout<<"PASS release integration suite\n";
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
