"""Compile the production envelope definitions and check retrigger/accent continuity."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
def test_envelope():
    source = (root/'src/modules/ACID9Voice/acid9voice.dsp').read_text()
    source = source[:source.rindex('process =')] + 'process = vca_env_val, filter_env_val, pitched_volts, accent_sweep, filter_env_raw;\n'
    with tempfile.TemporaryDirectory() as directory:
        d=Path(directory); (d/'probe.dsp').write_text(source)
        subprocess.run(['faust','-i','-a',str(root/'faust/vcvrack.cpp'),'-I',str(root/'src/modules/ACID9Voice'),'-I',str(root/'src/modules/ACID9Voice/lib'),str(d/'probe.dsp'),'-o',str(d/'probe.hpp')],check=True)
        (d/'test.cpp').write_text(r'''
#include "probe.hpp"
#include <iostream>
#include <string>
int main() {
 for(int sr : {44100,48000,96000}) {
  VCVRackDSP dsp; dsp.init(sr);
  auto set=[&](std::string name,float v){for(int p=0;p<dsp.getNumParams();++p){std::string path=dsp.getParamPath(p);if(path.substr(path.find_last_of('/')+1)==name)dsp.setParamValue(p,v);}};
  float a=0,f=0,pa=0,pf=0,stepA=0,stepF=0,peak=0;float pitch=0,sweep=0,raw=0;float* out[]={&a,&f,&pitch,&sweep,&raw};
  for(int n=0;n<sr;++n) {
   // Repeated notes while the 150 ms release is still audible, alternating accents.
   int note=n/(sr/10),phase=n%(sr/10);
   set("note_trigger",phase==0?1:0);set("gate",phase<sr*8/100?10:0);set("accent",note%2?10:0);
   dsp.compute(1,nullptr,out);
   stepA=std::max(stepA,std::abs(a-pa));stepF=std::max(stepF,std::abs(f-pf));peak=std::max(peak,a);pa=a;pf=f;
   if(!std::isfinite(a)||!std::isfinite(f))return 2;
  }
  std::cout<<sr<<" Hz max envelope step: VCA="<<stepA<<" filter="<<stepF<<" peak="<<peak<<std::endl;
  if(stepA>1.3f/(.001f*sr)+.0001f || stepF>1.5f/(.001f*sr)+.0001f || peak<.9f)return 1;
  set("note_trigger",0);set("gate",0);for(int n=0;n<sr/2;++n)dsp.compute(1,nullptr,out);
  if(a>1e-5f)return 3;
  dsp.init(sr);set("volts",1);set("slide",10);set("slide_time_ms",60);
  for(int n=0;n<int(sr*.06);++n)dsp.compute(1,nullptr,out);
  if(std::abs(pitch-.9f)>.002f)return 4;
  // Accent amount zero must preserve normal decay, even with ACCENT high.
  dsp.init(sr);set("decay",.5);set("accent",10);set("accent_amount",0);set("gate",10);set("note_trigger",1);dsp.compute(1,nullptr,out);set("note_trigger",0);
  for(int n=0;n<int(sr*.061);++n)dsp.compute(1,nullptr,out);
  float noAccent=raw;
  if(noAccent<.85f||sweep!=0)return 5;
  dsp.init(sr);set("decay",.5);set("accent",10);set("accent_amount",1);set("resonance",1);set("gate",10);set("note_trigger",1);dsp.compute(1,nullptr,out);set("note_trigger",0);
  for(int n=0;n<int(sr*.061);++n)dsp.compute(1,nullptr,out);
  if(raw>.4f||raw<.3f)return 6;
  // Successive accents charge a persistent sweep rather than resetting it.
  dsp.init(sr);set("accent",10);set("accent_amount",1);set("resonance",1);set("gate",10);
  set("note_trigger",0);for(int n=0;n<sr/5;++n)dsp.compute(1,nullptr,out);
  float first=0,second=0;
  for(int n=0;n<int(sr*.16);++n){set("note_trigger",n==0||n==int(sr*.08));dsp.compute(1,nullptr,out);if(n<int(sr*.08))first=std::max(first,sweep);else second=std::max(second,sweep);}
  std::cout<<"accent sweep peaks "<<first<<" "<<second<<std::endl;
  if(second<=first*1.05f)return 7;
 }
}
''')
        subprocess.run(['c++','-std=c++17','-O2',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
        subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':test_envelope()
