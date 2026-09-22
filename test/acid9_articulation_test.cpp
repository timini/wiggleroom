#include "modules/ACID9Voice/Articulation.hpp"
#include <iostream>
#include <cstdlib>
using WiggleRoom::AcidArticulation;
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
int main(){for(double sr:{44100.,48000.,96000.}){
 AcidArticulation a;double dt=1/sr;
 auto tick=[&](float gate,float slide,float accent,float pitch){a.process(dt,gate,slide,accent,pitch);};
 tick(10,0,0,0);check(a.trigger&&!a.gliding,"First note should attack");
 tick(10,10,0,0);tick(10,0,0,0);check(a.armed,"Slide pulse not latched");
 for(int i=0;i<int(sr*.02);++i)tick(0,0,0,1);
 check(a.active&&!a.trigger&&a.pitch==0,"Gap bridge moved to next pitch early");
 tick(10,0,10,1);check(a.gliding&&!a.trigger&&a.accented&&!a.armed,"Tied gate note wrong");
 tick(0,0,0,1);check(!a.active,"Unarmed note should release");
 tick(10,0,0,1);check(a.trigger&&!a.accented,"Repeated pitch not retriggered");
 tick(10,10,0,1);tick(10,0,0,1);
 for(int i=0;i<int(sr*.003);++i)tick(10,0,0,2);
 check(a.gliding&&a.pitch==2&&!a.armed,"Held-gate step did not slide");
 // A slide is consumed once; the following ordinary note must retrigger.
 bool event=false;for(int i=0;i<int(sr*.003);++i){tick(10,0,0,3);event|=a.trigger;}
 check(event&&!a.gliding,"Consumed slide leaked into next note");
 tick(10,10,0,3);for(int i=0;i<int(sr*.11);++i)tick(0,0,0,3);
 check(!a.active&&!a.armed,"Stopped source left voice latched");
 tick(10,10,0,0);tick(0,10,0,0);tick(10,10,0,1);check(a.gliding&&a.armed,"Held slide did not chain");
 a.reset();tick(10,0,0,0);int count=0;
 for(int i=0;i<int(sr*.1);++i){tick(10,0,0,(i%2?1:-1)*.0002f);count+=a.trigger;}
 check(count==0,"Pitch noise triggered notes");
 for(int i=0;i<int(sr*.01);++i){tick(10,0,0,float(i)*.002f);check(!a.trigger,"Moving CV triggered notes");}
 tick(10,0,10,a.pitch);check(a.accented,"Late accent not latched");
 tick(10,0,0,a.pitch);check(a.accented,"Short accent forgotten");
 a.reset();check(!a.active&&!a.armed&&!a.accented,"Reset retained state");
}std::cout<<"PASS: latched slide, held gates, repeated pitches, gaps, timeout, accents and pitch noise at three rates\n";}
