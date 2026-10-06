#include "DfxDsp.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    DfxDsp dsp;
    if (dsp.setSignalFormat(32, 2, 48000, 32) != 0) return 3;
    dsp.powerOn(true);

    std::wstring p = std::filesystem::absolute(argv[1]).wstring();
    if (dsp.loadPreset(p) != 0) return 4;

    for (int e = (int)DfxDsp::Fidelity; e < (int)DfxDsp::NumEffects; ++e) {
        auto effect = static_cast<DfxDsp::Effect>(e);
        dsp.setEffectValue(effect, dsp.getEffectValue(effect) * 10.0f);
    }
    for (int b = 0; b < dsp.getNumEqBands(); ++b) {
        dsp.setEqBandFrequency(b, dsp.getEqBandFrequency(b));
        dsp.setEqBandBoostCut(b, dsp.getEqBandBoostCut(b));
    }
    dsp.eqOn(true);
    dsp.setVolumeLeveling(0.0f);
    dsp.setNormalization(0.0f);
    dsp.setFilterQ(1.0f);
    dsp.setMasterGain(0.0f);
    dsp.setBalance(0.0f);

    constexpr int frames = 48000;
    std::vector<float> audio(frames * 2);
    for (int n = 0; n < frames; ++n) {
        float s = 0.08f * std::sin(2.0 * M_PI * 440.0 * n / 48000.0);
        audio[n * 2] = s;
        audio[n * 2 + 1] = s;
    }

    double in_energy = 0.0;
    for (float s : audio) in_energy += (double)s * s;

    int rc = dsp.processAudio(reinterpret_cast<short*>(audio.data()),
                              reinterpret_cast<short*>(audio.data()), frames, 0);

    double out_energy = 0.0, peak = 0.0;
    int bad = 0, nonzero = 0;
    for (float s : audio) {
        if (!std::isfinite(s)) { ++bad; continue; }
        if (std::abs(s) > 1.0e-9f) ++nonzero;
        out_energy += (double)s * s;
        peak = std::max(peak, std::abs((double)s));
    }

    std::cout << "rc=" << rc
              << " in_rms=" << std::sqrt(in_energy / audio.size())
              << " out_rms=" << std::sqrt(out_energy / audio.size())
              << " peak=" << peak
              << " nonzero=" << nonzero
              << " bad=" << bad << "\n";
    return (rc == 0 && bad == 0 && nonzero > 0) ? 0 : 5;
}
