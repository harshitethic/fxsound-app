#include "DfxDsp.h"

#include <cmath>
#include <filesystem>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    const fs::path out = fs::temp_directory_path() / "fxsound-linux-roundtrip";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    if (ec) return 2;

    const fs::path factory =
        fs::absolute(fs::path("linux") / "factory-presets" / "1.fac");

    DfxDsp source;
    if (source.setSignalFormat(32, 2, 48000, 32) != 0) return 3;
    source.setNumBands(10);
    if (source.loadPreset(factory.wstring()) != 0) return 4;

    for (int e = static_cast<int>(DfxDsp::Fidelity);
         e < static_cast<int>(DfxDsp::NumEffects); ++e)
    {
        const auto effect = static_cast<DfxDsp::Effect>(e);
        source.setEffectValue(effect, source.getEffectValue(effect) * 10.0f);
    }

    source.setEffectValue(DfxDsp::Fidelity, 7.5f);
    source.setEqBandBoostCut(0, 3.0f);

    if (source.savePreset(L"RoundTrip", out.wstring()) != 0) return 5;

    const fs::path saved = out / "RoundTrip.fac";
    if (!fs::exists(saved)) return 6;

    DfxDsp restored;
    if (restored.setSignalFormat(32, 2, 48000, 32) != 0) return 7;
    restored.setNumBands(10);
    if (restored.loadPreset(saved.wstring()) != 0) return 8;

    const float clarity = restored.getEffectValue(DfxDsp::Fidelity) * 10.0f;
    const float eq0 = restored.getEqBandBoostCut(0);

    std::cout << "clarity=" << clarity << " eq0=" << eq0 << "\n";
    fs::remove_all(out, ec);

    return (std::fabs(clarity - 7.5f) < 0.2f &&
            std::fabs(eq0 - 3.0f) < 0.2f) ? 0 : 9;
}
