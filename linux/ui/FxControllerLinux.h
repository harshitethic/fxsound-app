/*
 * FxSound Linux UI controller.
 * Bridges the original FxSound JUCE widgets to the Linux DSP daemon.
 * AGPL-3.0-or-later, matching upstream FxSound.
 */
#pragma once

#include <JuceHeader.h>
#include "FxAudioControls.h"

#include <array>
#include <string>
#include <vector>

class FxController
{
public:
    static constexpr int NUM_SPECTRUM_BANDS = 10;
    static constexpr int DEFAULT_NUM_EQ_BANDS = 10;
    static constexpr float DEFAULT_NORMALIZATION = 0.0f;
    static constexpr float DEFAULT_VOLUME_LEVELING = 0.0f;
    static constexpr float DEFAULT_BALANCE = 0.0f;
    static constexpr float DEFAULT_FILTER_Q = 1.0f;
    static constexpr float DEFAULT_MASTER_GAIN = 0.0f;

    static FxController& getInstance()
    {
        static FxController instance;
        return instance;
    }

    FxController(const FxController&) = delete;
    FxController& operator=(const FxController&) = delete;

    bool refresh();
    bool isConnected() const { return connected_; }
    bool isPowerOn() const { return power_on_; }
    const juce::String& getCurrentPreset() const { return current_preset_; }
    bool isPresetModified() const { return preset_modified_; }
    const juce::StringArray& getPresets() const { return presets_; }

    bool setPresetName(const juce::String& name);
    bool resetCurrentPreset();
    void setPowerState(bool on);

    int getOutputCount() const { return static_cast<int>(outputs_.size()); }
    juce::String getOutputDescription(int index) const;
    juce::String getOutputNodeName(int index) const;
    int getSelectedOutputIndex() const;
    bool setOutputDevice(int index);

    float getEffectValue(FxEffects::EffectType effect) const;
    void setEffectValue(FxEffects::EffectType effect, float value);

    int getNumEqBands() const { return num_eq_bands_; }
    void setNumEqBands(int num_bands);

    float getVolumeLeveling() const { return volume_leveling_; }
    void setVolumeLeveling(float value);
    float getBalance() const { return balance_; }
    void setBalance(float value);
    float getMasterGain() const { return master_gain_; }
    void setMasterGain(float value);
    float getFilterQ() const { return filter_q_; }
    void setFilterQ(float value);

    float getEqBandFrequency(int band_num) const;
    void setEqBandFrequency(int band_num, float freq);
    void getEqBandFrequencyRange(int band_num, float* min_freq, float* max_freq) const;
    float getEqBandBoostCut(int band_num) const;
    void setEqBandBoostCut(int band_num, float boost);

    bool isHelpTooltipsHidden() const { return false; }
    bool isAudioProcessing() const { return connected_ && power_on_; }
    void getSpectrumBandValues(juce::Array<float>& values) const;

private:
    FxController();

    juce::String sendCommand(const juce::String& command) const;
    struct OutputDevice {
        juce::String node_name;
        juce::String description;
    };

    bool parseStatus(const juce::String& json);
    bool loadPresets();
    bool loadOutputs();

    static const char* effectCommandName(FxEffects::EffectType effect);

    bool connected_{false};
    bool power_on_{true};
    bool preset_modified_{false};
    juce::String current_preset_{"General"};
    juce::StringArray presets_;
    juce::String current_output_node_;
    std::vector<OutputDevice> outputs_;

    std::array<float, FxEffects::NumEffects> effects_{};
    int num_eq_bands_{31};
    std::vector<float> eq_frequency_;
    std::vector<float> eq_gain_;

    float volume_leveling_{0.0f};
    float master_gain_{0.0f};
    float balance_{0.0f};
    float filter_q_{1.0f};
};
