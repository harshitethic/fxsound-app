#include "DfxDsp.h"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

struct Result { int rc=-999; bool finite=true; int bad_index=-1; double rms=0.0; double peak=0.0; };

static std::vector<float> tone() {
    constexpr int sr=48000, frames=480;
    std::vector<float> a(frames*2);
    for(int n=0;n<frames;++n){ float s=0.15f*std::sin(2.0*M_PI*440.0*n/sr); a[n*2]=s; a[n*2+1]=s; }
    return a;
}

static Result run_case(const std::string& name,bool power,bool eq,DfxDsp::Effect effect=DfxDsp::Fidelity,float value=0.0f,bool set=false){
    constexpr int frames=480;
    auto audio=tone();
    DfxDsp dsp;
    int fmt=dsp.setSignalFormat(32,2,48000,32);
    dsp.powerOn(power); dsp.eqOn(eq);
    for(int e=(int)DfxDsp::Fidelity;e<=(int)DfxDsp::Bass;++e) dsp.setEffectValue((DfxDsp::Effect)e,0.0f);
    if(set) dsp.setEffectValue(effect,value);
    Result r;
    r.rc=fmt==0?dsp.processAudio((short*)audio.data(),(short*)audio.data(),frames,0):fmt;
    double energy=0.0;
    for(int i=0;i<(int)audio.size();++i){float s=audio[i];if(!std::isfinite(s)){if(r.bad_index<0)r.bad_index=i;r.finite=false;continue;}energy+=(double)s*s;r.peak=std::max(r.peak,std::abs((double)s));}
    r.rms=std::sqrt(energy/audio.size());
    std::cout<<name<<" rc="<<r.rc<<" finite="<<(r.finite?"yes":"NO")<<" bad_index="<<r.bad_index<<" rms="<<r.rms<<" peak="<<r.peak<<"\n";
    return r;
}

int main(){
    bool ok=true;
    ok&=run_case("bypass",false,false).finite;
    ok&=run_case("powered-flat",true,false).finite;
    ok&=run_case("eq-only",true,true).finite;
    ok&=run_case("fidelity",true,false,DfxDsp::Fidelity,4.0f,true).finite;
    ok&=run_case("ambience",true,false,DfxDsp::Ambience,4.0f,true).finite;
    ok&=run_case("surround",true,false,DfxDsp::Surround,4.0f,true).finite;
    ok&=run_case("dynamic-boost",true,false,DfxDsp::DynamicBoost,4.0f,true).finite;
    ok&=run_case("bass",true,false,DfxDsp::Bass,4.0f,true).finite;
    return ok?0:4;
}
