"""Check Faust ladder against the scalar Csound/Pirkle formulation and stress modulation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def test_filter():
 with tempfile.TemporaryDirectory() as tmp:
  d=Path(tmp)
  (d/'probe.dsp').write_text('import("stdfaust.lib");import("acid_filter.lib");process=diode_ladder_zdf(hslider("cutoff",1000,20,20000,1),hslider("res",0,0,1,.001),_);')
  subprocess.run(['faust','-i','-a',str(root/'faust/vcvrack.cpp'),'-I',str(root/'src/modules/ACID9Voice/lib'),str(d/'probe.dsp'),'-o',str(d/'probe.hpp')],check=True)
  (d/'test.cpp').write_text(r'''
#include "probe.hpp"
#include <iostream>
#include <string>
// Scalar reference: Csound wpfilters.c, Steven Yi, LGPL-2.1-or-later.
struct Ref {
 double z[4]={};
 double process(double x,double fc,double res,double sr){
  double g=tan(M_PI*std::clamp(fc,20.,sr*.4)/sr),p=1+g,a=g/p,k=16.5*res;
  double G4=.5*g/p,G3=.5*g/(p-.5*g*G4),G2=.5*g/(p-.5*g*G3),G1=g/(p-g*G2);
  double b1=1/(p-g*G2),b2=1/(p-.5*g*G3),b3=1/(p-.5*g*G4),b4=1/p;
  double f4=b4*z[3],f3=b3*(z[2]+.5*g*f4),f2=b2*(z[1]+.5*g*f3),f1=b1*(z[0]+g*f2);
  double u=(tanh(x)-k*(G4*G3*G2*f1+G4*G3*f2+G4*f3+f4))/(1+k*G1*G2*G3*G4);
  double v=a*(u*(1+G1*G2)+f2+G2*f1-z[0]),y=v+z[0];z[0]=y+v;
  v=a*(.5*(y*(1+G2*G3)+f3+G3*f2)-z[1]);y=v+z[1];z[1]=y+v;
  v=a*(.5*(y*(1+G3*G4)+f4+G4*f3)-z[2]);y=v+z[2];z[2]=y+v;
  v=a*(.5*y-z[3]);y=v+z[3];z[3]=y+v;return y;
 }
};
int main(){double error=0,peak=0;
 for(int sr:{44100,48000,96000})for(float res:{0.f,.5f,1.f})for(float drive:{.01f,1.f,5.f}){
  VCVRackDSP d;d.init(sr);if(d.getNumInputs()!=1||d.getNumOutputs()!=1){std::cerr<<"IO "<<d.getNumInputs()<<" "<<d.getNumOutputs()<<std::endl;return 2;}Ref ref;float in=0,out=0;float* ip[]={&in};float* op[]={&out};
  for(int n=0;n<sr;++n){float fc=20*pow(1000.,double(n)/sr);d.setParamValue(0,fc);d.setParamValue(1,res);
   in=drive*(.6*sin(2*M_PI*110*n/sr)+.4*sin(2*M_PI*3000*n/sr));d.compute(1,ip,op);
   double expected=ref.process(in,fc,res,sr);error=std::max(error,std::abs(out-expected));peak=std::max(peak,std::abs(double(out)));
   if(!std::isfinite(out)||std::abs(out)>8||std::abs(out-expected)>.003){std::cerr<<"Ladder stability/reference failure "<<sr<<" "<<fc<<" "<<out<<" "<<expected<<std::endl;return 1;}
  }
 }
 std::cout<<"PASS: ladder reference and sweeps at three rates, max error="<<error<<" peak="<<peak<<std::endl;
}
''')
  subprocess.run(['c++','-std=c++17','-O2',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':test_filter()
