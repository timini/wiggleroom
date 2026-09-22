#pragma once
#include <algorithm>
#include <cmath>
namespace WiggleRoom {
// One clock-free voice: gate edges identify repeated pitches; settled V/oct
// steps also identify notes under a held gate. Never inspect FM or octave knobs.
struct AcidArticulation {
    bool gateHigh=false,slideHigh=false,accentHigh=false;
    bool active=false,armed=false,gliding=false,accented=false,trigger=false;
    double gap=0,settled=0;
    float accepted=0,candidate=0,pitch=0;
    bool candidateReady=false;
    static bool schmitt(bool old,float v){return old?v>.1f:v>=1.f;}
    void reset(){*this={};}
    void process(double dt,float gate,float slide,float accent,float voct) {
        bool gh=schmitt(gateHigh,gate),sh=schmitt(slideHigh,slide),ah=schmitt(accentHigh,accent);
        bool rise=gh&&!gateHigh,accentRise=ah&&!accentHigh;
        trigger=false;
        constexpr float cent=1.f/1200.f;
        bool step=false;
        if(gh&&!rise&&active) {
            if(!candidateReady||std::abs(voct-candidate)>cent){candidate=voct;settled=0;candidateReady=true;}
            else settled+=dt;
            step=settled>=.001 && std::abs(candidate-accepted)>cent;
        }
        if(rise||step) {
            bool tie=active&&armed;
            trigger=!tie;gliding=tie;active=true;armed=false;gap=0;
            accepted=voct;candidate=voct;candidateReady=true;settled=0;
            accented=ah;
        }
        if(active) {
            if(sh)armed=true;
            if(accentRise)accented=true;
            if(gh)gap=0;
            else {
                gap+=dt;
                if(!armed||gap>=.1){active=false;armed=false;gliding=false;accented=false;}
            }
        }
        // Hold the source pitch during the 1 ms note-classification window.
        pitch=(active&&(!gh||(armed&&std::abs(voct-accepted)>cent)))?accepted:voct;
        if(rise||step)pitch=voct;
        gateHigh=gh;slideHigh=sh;accentHigh=ah;
    }
};
}
