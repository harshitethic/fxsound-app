#include "DfxDsp.h"
#include <filesystem>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv){
    if(argc<2){std::cerr<<"usage: preset_dump <fac> [fac...]\n";return 2;}
    for(int ai=1;ai<argc;++ai){
        auto path=std::filesystem::absolute(argv[ai]);
        std::wstring wpath=path.wstring();
        DfxDsp dsp;
        if(dsp.setSignalFormat(32,2,48000,32)!=0) return 3;
        auto info=dsp.getPresetInfo(wpath);
        int rc=dsp.loadPreset(wpath);
        std::cout << "PRESET|" << path.filename().string() << "|rc=" << rc << "\n";
        std::cout<<std::fixed<<std::setprecision(4)
            <<"FX|clarity="<<dsp.getEffectValue(DfxDsp::Fidelity)*10.0f
            <<"|ambience="<<dsp.getEffectValue(DfxDsp::Ambience)*10.0f
            <<"|surround="<<dsp.getEffectValue(DfxDsp::Surround)*10.0f
            <<"|dynamic="<<dsp.getEffectValue(DfxDsp::DynamicBoost)*10.0f
            <<"|bass="<<dsp.getEffectValue(DfxDsp::Bass)*10.0f
            <<"|bands="<<dsp.getNumEqBands()<<"\n";
        for(int b=0;b<dsp.getNumEqBands();++b)
            std::cout<<"EQ|"<<b+1<<"|f="<<dsp.getEqBandFrequency(b)<<"|g="<<dsp.getEqBandBoostCut(b)<<"\n";
    }
    return 0;
}
