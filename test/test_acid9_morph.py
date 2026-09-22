"""Exercise the dual filter's level, resonance, smoothing and convex blend."""
from pathlib import Path
import subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]

def test_morph():
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d/'probe.dsp').write_text('import("stdfaust.lib");import("acid_filter.lib");process=_ <: dual_filter(hslider("cutoff",1000,20,20000,1),hslider("res",0,0,1,.001),hslider("mode",0,0,1,.001),_);')
        subprocess.run(['faust','-i','-a',str(ROOT/'faust/vcvrack.cpp'),'-I',str(ROOT/'src/modules/ACID9Voice/lib'),str(d/'probe.dsp'),'-o',str(d/'probe.hpp')],check=True)
        (d/'test.cpp').write_text(r'''
#include "probe.hpp"
#include <iostream>
#include <string>
#include <memory>
void set(VCVRackDSP& d,std::string key,float value){for(int p=0;p<d.getNumParams();++p){std::string s=d.getParamPath(p);if(s.substr(s.find_last_of('/')+1)==key)d.setParamValue(p,value);}}
void check(bool b,const char* msg){if(!b){std::cerr<<msg<<std::endl;std::exit(1);}}
int main(){double peak=0,error=0;
 for(int sr:{44100,48000,96000})for(float res:{0.f,.3f,.6f,.99f}){
  auto a=std::make_unique<VCVRackDSP>(),b=std::make_unique<VCVRackDSP>(),m=std::make_unique<VCVRackDSP>();
  for(auto* d:{a.get(),b.get(),m.get()}){d->init(sr);check(d->getNumInputs()==1 && d->getNumOutputs()==1,"Wrong probe channel count");set(*d,"res",res);}set(*b,"mode",1);set(*m,"mode",.5);
  float x=0,oa=0,ob=0,om=0;float* in[]={&x};float* pa[]={&oa};float* pb[]={&ob};float* pm[]={&om};
  double aa=0,bb=0;
  for(int n=0;n<sr*2;++n){float fc=n<sr?1500:20*pow(1000.,double(n-sr)/sr);for(auto* d:{a.get(),b.get(),m.get()})set(*d,"cutoff",fc);
   x=.6*(2*std::fmod(110.*n/sr,1.)-1);a->compute(1,in,pa);b->compute(1,in,pb);m->compute(1,in,pm);
   for(float v:{oa,ob,om}){check(std::isfinite(v)&&fabs(v)<8,"Unstable filter");peak=std::max(peak,fabs(double(v)));}
   if(n>sr/2){double e=fabs(om-(oa+ob)*.5);error=std::max(error,e);check(e<.0001,"Morph midpoint is not a linear blend");}
   if(n>=sr/2&&n<sr){aa+=oa*oa;bb+=ob*ob;}
  }
  check(sqrt(aa/bb)>.7 && sqrt(aa/bb)<1.5,"Endpoint level mismatch on reference saw");
  std::cout<<sr<<" Hz res="<<res<<" Acid RMS="<<sqrt(aa/(sr/2))<<" Lead RMS="<<sqrt(bb/(sr/2))<<std::endl;
  // Rapid cutoff/resonance/morph movements, followed by a silent decay.
  for(int n=0;n<sr*2;++n){x=n<sr?.6*sin(2*M_PI*220*n/sr):0;
   if(n<sr){set(*m,"mode",(n/100)%2);set(*m,"res",(n/73)%2);set(*m,"cutoff",20*pow(1000.,double(n%997)/997));}
   m->compute(1,in,pm);check(std::isfinite(om)&&fabs(om)<8,"Unstable modulation");
  }
  check(fabs(om)<.001,"Filter fails to decay to silence");
 }
 std::cout<<"PASS: level sweep, midpoint, modulation and decay; peak="<<peak<<" midpoint error="<<error<<std::endl;
}
''')
        subprocess.run(['c++','-std=c++17','-O2',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
        subprocess.run([str(d/'test')],check=True)
if __name__ == '__main__': test_morph()
