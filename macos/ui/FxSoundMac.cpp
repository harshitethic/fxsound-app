/*
 * FxSound macOS desktop UI.
 * Reuses the upstream FxSound JUCE window, theme and controls.
 * AGPL-3.0-or-later, matching upstream FxSound.
 */
#include <JuceHeader.h>

#include "FxTheme.h"
#include "FxWindow.h"
#include "FxComboBox.h"
#include "FxPowerButton.h"
#include "FxAudioControls.h"
#include "FxEqualizer.h"
#include "FxVisualizer.h"
#include "FxControllerLinux.h"

#include <cstdlib>
#include <cmath>
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" void fxsoundMacMiniaturize(void* native_handle);
extern "C" void fxsoundMacDeminiaturize(void* native_handle);
extern "C" bool fxsoundMacIsMiniaturized(void* native_handle);

namespace {

struct UiPreferences {
    FxThemeMode theme{FxThemeMode::Dark};
    bool compact{false};
    bool always_on_top{false};
};

std::filesystem::path ui_preferences_path()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return "fxsound-macos-ui.conf";
    return std::filesystem::path(home) / "Library/Application Support/FxSound/state/ui.conf";
}

UiPreferences loadUiPreferences()
{
    UiPreferences prefs;
    std::ifstream input(ui_preferences_path());
    std::string line;

    while (std::getline(input, line))
    {
        const auto split = line.find('=');
        if (split == std::string::npos)
            continue;

        const auto key = line.substr(0, split);
        const auto value = line.substr(split + 1);

        if (key == "theme")
            prefs.theme = value == "light" ? FxThemeMode::Light : FxThemeMode::Dark;
        else if (key == "compact")
            prefs.compact = value == "1";
        else if (key == "always_on_top")
            prefs.always_on_top = value == "1";
    }

    return prefs;
}

void saveUiPreferences(const UiPreferences& prefs)
{
    const auto path = ui_preferences_path();
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return;

    output << "theme=" << (prefs.theme == FxThemeMode::Light ? "light" : "dark") << '\n'
           << "compact=" << (prefs.compact ? 1 : 0) << '\n'
           << "always_on_top=" << (prefs.always_on_top ? 1 : 0) << '\n';
}

} // namespace

class FxLinuxContent final : public juce::Component,
                             private juce::ComboBox::Listener
{
public:
    FxLinuxContent()
        : equalizer_(FxEqualizer::getInstance())
    {
        setOpaque(false);

        auto& controller = FxController::getInstance();
        controller.refresh();

        preset_list_.setJustificationType(juce::Justification::centredLeft);
        preset_list_.setTextWhenNoChoicesAvailable("No presets");
        preset_list_.setDescription("Preset List");
        preset_list_.setWantsKeyboardFocus(true);
        preset_list_.addListener(this);
        addAndMakeVisible(preset_list_);

        output_list_.setJustificationType(juce::Justification::centredLeft);
        output_list_.setTextWhenNoChoicesAvailable("No playback devices");
        output_list_.setDescription("Playback Device List");
        output_list_.setWantsKeyboardFocus(true);
        output_list_.addListener(this);
        addAndMakeVisible(output_list_);

        refreshPresetList();
        refreshOutputList();

        addAndMakeVisible(visualizer_);
        addAndMakeVisible(audio_controls_);
        addAndMakeVisible(equalizer_);

        audio_controls_.setLookAndFeel();
        equalizer_.reinit(controller.getNumEqBands());
        audio_controls_.update();
        equalizer_.update();
        equalizer_.showValues(true);
        audio_controls_.showValues(true);
        visualizer_.start();

        setPowerEnabled(controller.isPowerOn());
        setCompact(false);
    }

    ~FxLinuxContent() override
    {
        visualizer_.pause();
        preset_list_.removeListener(this);
        output_list_.removeListener(this);
    }

    void setPowerEnabled(bool enabled)
    {
        preset_list_.setEnabled(enabled);
        audio_controls_.setEnabled(enabled);
        equalizer_.setEnabled(enabled);
        visualizer_.setEnabled(enabled);
        repaint();
    }

    void setCompact(bool compact)
    {
        compact_ = compact;

        visualizer_.setVisible(!compact);
        audio_controls_.setVisible(!compact);
        equalizer_.setVisible(!compact);

        if (compact)
            setSize(550, 112);
        else
            setSize(1040, 511);

        resized();
        repaint();
    }

    bool isCompact() const noexcept { return compact_; }

    void refreshFromDaemon()
    {
        auto& controller = FxController::getInstance();
        if (!controller.refresh())
            return;

        refreshPresetList();
        refreshOutputList();
        equalizer_.reinit(controller.getNumEqBands());
        audio_controls_.update();
        equalizer_.update();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = dynamic_cast<juce::LookAndFeel_V4&>(getLookAndFeel());

        g.setColour(theme.getCurrentColourScheme().getUIColour(
            juce::LookAndFeel_V4::ColourScheme::windowBackground));
        g.fillAll();

        g.setColour(juce::Colour(FXCOLOR(PanelBackground)).withAlpha(0.2f));

        if (compact_)
            g.fillRoundedRectangle(20.0f, 22.0f, 510.0f, 90.0f, 10.0f);
        else
            g.fillRoundedRectangle(20.0f, 16.0f, 1000.0f, 487.0f, 8.0f);
    }

    void resized() override
    {
        if (compact_)
        {
            preset_list_.setBounds(40, 42, 225, 50);
            output_list_.setBounds(285, 42, 225, 50);
            return;
        }

        preset_list_.setBounds(40, 28, 470, 40);
        output_list_.setBounds(530, 28, 470, 40);

        visualizer_.setBounds(40, 88, 960, 120);
        audio_controls_.setBounds(40, 228, 168, 257);
        equalizer_.setBounds(224, 228, 776, 257);
    }

private:
    void refreshPresetList()
    {
        auto& controller = FxController::getInstance();

        preset_list_.clear(juce::dontSendNotification);
        for (int i = 0; i < controller.getPresets().size(); ++i)
            preset_list_.addItem(controller.getPresets()[i], i + 1);

        const int selected = controller.getPresets().indexOf(controller.getCurrentPreset());
        if (selected >= 0)
            preset_list_.setSelectedId(selected + 1, juce::dontSendNotification);
    }

    void refreshOutputList()
    {
        auto& controller = FxController::getInstance();

        output_list_.clear(juce::dontSendNotification);
        for (int i = 0; i < controller.getOutputCount(); ++i)
            output_list_.addItem(controller.getOutputDescription(i), i + 1);

        const int selected = controller.getSelectedOutputIndex();
        if (selected >= 0)
            output_list_.setSelectedId(selected + 1, juce::dontSendNotification);
        else
            output_list_.setSelectedId(0, juce::dontSendNotification);

        output_list_.setEnabled(controller.getOutputCount() > 0);
    }

    void comboBoxChanged(juce::ComboBox* box) override
    {
        auto& controller = FxController::getInstance();

        if (box == &preset_list_)
        {
            const int index = preset_list_.getSelectedItemIndex();
            if (index < 0 || index >= controller.getPresets().size())
                return;

            if (controller.setPresetName(controller.getPresets()[index]))
            {
                refreshOutputList();
                equalizer_.reinit(controller.getNumEqBands());
                audio_controls_.update();
                equalizer_.update();
                repaint();
            }
            return;
        }

        if (box == &output_list_)
        {
            const int index = output_list_.getSelectedItemIndex();
            if (index < 0 || index >= controller.getOutputCount())
                return;

            if (!controller.setOutputDevice(index))
                refreshOutputList();
            else
                output_list_.setSelectedId(index + 1, juce::dontSendNotification);
        }
    }

public:
    bool qaSelectPreset(const juce::String& name)
    {
        auto& controller = FxController::getInstance();
        const auto& presets = controller.getPresets();
        for (int i = 0; i < presets.size(); ++i)
        {
            if (presets[i] == name)
            {
                preset_list_.setSelectedId(i + 1, juce::sendNotificationSync);
                return controller.getCurrentPreset() == name;
            }
        }
        return false;
    }

    bool qaReselectCurrentOutput()
    {
        auto& controller = FxController::getInstance();
        const int index = controller.getSelectedOutputIndex();
        if (index < 0)
            return false;

        output_list_.setSelectedId(0, juce::dontSendNotification);
        output_list_.setSelectedId(index + 1, juce::sendNotificationSync);
        return controller.getSelectedOutputIndex() == index;
    }

    bool qaToggleAudioView()
    {
        const bool before = audio_controls_.qaEffectsShown();
        if (!audio_controls_.qaToggleView())
            return false;
        return audio_controls_.qaEffectsShown() != before;
    }

    bool qaRestoreAudioView()
    {
        if (audio_controls_.qaEffectsShown())
            return true;
        return audio_controls_.qaToggleView() && audio_controls_.qaEffectsShown();
    }

    bool qaClickRestoreDefaults()
    {
        return audio_controls_.qaClickRestoreDefaults();
    }

    bool qaSetEffect(FxEffects::EffectType effect, double value)
    {
        return audio_controls_.qaSetEffect(effect, value);
    }

    bool qaSelectBands(int bands)
    {
        return audio_controls_.qaSelectBands(bands);
    }

    void qaSetMasterGain(double value) { audio_controls_.qaSetMasterGain(value); }
    void qaSetVolumeLeveling(double value) { audio_controls_.qaSetVolumeLeveling(value); }
    void qaSetFilterQ(double value) { audio_controls_.qaSetFilterQ(value); }
    void qaSetBalance(double value) { audio_controls_.qaSetBalance(value); }
    bool qaSetEqGain(int band, double value) { return equalizer_.qaSetBandGain(band, value); }
    bool qaSetEqFrequency(int band, double value) { return equalizer_.qaSetBandFrequency(band, value); }

private:
    bool compact_{false};
    FxComboBox preset_list_;
    FxComboBox output_list_;
    FxVisualizer visualizer_;
    FxAudioControls audio_controls_;
    FxEqualizer& equalizer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxLinuxContent)
};

class FxLinuxWindow final : public FxWindow
{
public:
    explicit FxLinuxWindow(bool start_minimized = false)
        : FxWindow(),
          power_button_("powerButton"),
          menu_button_("menuButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          resize_button_("resizeButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          minimize_button_("minimizeButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          donate_button_("donateButton", juce::DrawableButton::ButtonStyle::ImageFitted)
    {
        setName("FxSound");
        setOpaque(false);
        enableShadow(false);

        content_.setCompact(prefs_.compact);
        setAlwaysOnTop(prefs_.always_on_top);
        configureToolbar();

        setContent(&content_);
        centreWithSize(getWidth(), getHeight());
        addToDesktop(juce::ComponentPeer::windowAppearsOnTaskbar
                     | juce::ComponentPeer::windowHasMinimiseButton);
        setVisible(true);
        toFront(true);

        if (start_minimized)
            if (auto* peer = getPeer())
            {
                peer->setMinimised(true);
                fxsoundMacMiniaturize(peer->getNativeHandle());
            }
    }

    ~FxLinuxWindow() override
    {
        removeFromDesktop();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void restoreAndBringToFront()
    {
        setVisible(true);
        if (auto* peer = getPeer())
        {
            peer->setMinimised(false);
            fxsoundMacDeminiaturize(peer->getNativeHandle());
        }
        toFront(true);
        grabKeyboardFocus();
    }

    void qaClickMinimizeButton()
    {
        if (minimize_button_.onClick)
            minimize_button_.onClick();
    }

    void qaClickMenuButton()
    {
        if (menu_button_.onClick)
            menu_button_.onClick();
    }

    bool runQaSelfTest(juce::String& report)
    {
        bool all_ok = true;
        std::ostringstream out;

        auto check = [&](bool ok, const char* name)
        {
            out << (ok ? "PASS " : "FAIL ") << name << "\n";
            all_ok = all_ok && ok;
        };

        auto& controller = FxController::getInstance();
        controller.refresh();

        const juce::String original_preset = controller.getCurrentPreset();
        const bool original_preset_modified = controller.isPresetModified();
        std::array<float, FxEffects::NumEffects> original_effects{};
        for (int i = 0; i < FxEffects::NumEffects; ++i)
            original_effects[static_cast<size_t>(i)] =
                controller.getEffectValue(static_cast<FxEffects::EffectType>(i));

        const int original_bands = controller.getNumEqBands();
        std::vector<float> original_eq_frequency;
        std::vector<float> original_eq_gain;
        original_eq_frequency.reserve(static_cast<size_t>(original_bands));
        original_eq_gain.reserve(static_cast<size_t>(original_bands));
        for (int i = 0; i < original_bands; ++i)
        {
            original_eq_frequency.push_back(controller.getEqBandFrequency(i));
            original_eq_gain.push_back(controller.getEqBandBoostCut(i));
        }

        const float original_volume = controller.getVolumeLeveling();
        const float original_master = controller.getMasterGain();
        const float original_balance = controller.getBalance();
        const float original_filter_q = controller.getFilterQ();

        auto eq_matches_original = [&]()
        {
            if (controller.getNumEqBands() != original_bands)
                return false;
            for (int i = 0; i < original_bands; ++i)
            {
                if (std::fabs(controller.getEqBandFrequency(i)
                              - original_eq_frequency[static_cast<size_t>(i)]) >= 0.25f
                    || std::fabs(controller.getEqBandBoostCut(i)
                                 - original_eq_gain[static_cast<size_t>(i)]) >= 0.01f)
                    return false;
            }
            return true;
        };

        const bool original_compact = content_.isCompact();
        const int original_width = getWidth();
        if (resize_button_.onClick) resize_button_.onClick();
        check(content_.isCompact() != original_compact && getWidth() != original_width,
              "resize_toggle");
        if (resize_button_.onClick) resize_button_.onClick();
        check(content_.isCompact() == original_compact && getWidth() == original_width,
              "resize_restore");

        const bool original_power = controller.isPowerOn();
        if (power_button_.onClick) power_button_.onClick();
        controller.refresh();
        check(controller.isPowerOn() != original_power, "power_toggle");
        if (power_button_.onClick) power_button_.onClick();
        controller.refresh();
        check(controller.isPowerOn() == original_power, "power_restore");

        const juce::String probe_preset =
            original_preset == "Music" ? "Voice" : "Music";
        check(content_.qaSelectPreset(probe_preset), "preset_combo_change");
        check(content_.qaSelectPreset(original_preset), "preset_combo_restore_original");
        check(content_.qaReselectCurrentOutput(), "output_combo_reselect");

        // Drive all five visible effect sliders through their actual JUCE
        // valueChanged callbacks and verify the daemon/controller received them.
        const FxEffects::EffectType effect_types[] = {
            FxEffects::Fidelity, FxEffects::Ambience, FxEffects::Surround,
            FxEffects::DynamicBoost, FxEffects::Bass
        };
        const double effect_targets[] = { 6.0, 2.0, 3.0, 7.0, 8.0 };
        bool effect_sliders_ok = true;
        for (int i = 0; i < FxEffects::NumEffects; ++i)
        {
            effect_sliders_ok = content_.qaSetEffect(effect_types[i], effect_targets[i])
                && effect_sliders_ok;
            controller.refresh();
            effect_sliders_ok =
                std::fabs(controller.getEffectValue(effect_types[i]) * 10.0f
                          - static_cast<float>(effect_targets[i])) < 0.01f
                && effect_sliders_ok;
        }
        check(effect_sliders_ok, "effect_sliders_all_5");

        // Exercise an actual EQ gain slider and frequency control.
        bool eq_slider_ok = content_.qaSetEqGain(0, 3.0);
        controller.refresh();
        eq_slider_ok = std::fabs(controller.getEqBandBoostCut(0) - 3.0f) < 0.01f
            && eq_slider_ok;

        float min_freq = 0.0f, max_freq = 0.0f;
        controller.getEqBandFrequencyRange(0, &min_freq, &max_freq);
        const float test_freq = (min_freq + max_freq) * 0.5f;
        eq_slider_ok = content_.qaSetEqFrequency(0, test_freq) && eq_slider_ok;
        controller.refresh();
        eq_slider_ok = std::fabs(controller.getEqBandFrequency(0) - test_freq) < 0.2f
            && eq_slider_ok;
        check(eq_slider_ok, "eq_gain_and_frequency_controls");

        check(content_.qaToggleAudioView(), "flip_to_audio_controls");

        // Drive the actual audio-control widgets and combo box.
        bool audio_sliders_ok = content_.qaSelectBands(15);
        content_.qaSetVolumeLeveling(2.0);
        content_.qaSetMasterGain(4.0);
        content_.qaSetBalance(4.0);
        content_.qaSetFilterQ(2.0);
        controller.refresh();
        audio_sliders_ok = controller.getNumEqBands() == 15
            && std::fabs(controller.getVolumeLeveling() - 2.0f) < 0.01f
            && std::fabs(controller.getMasterGain() - 4.0f) < 0.01f
            && std::fabs(controller.getBalance() - 4.0f) < 0.01f
            && std::fabs(controller.getFilterQ() - 2.0f) < 0.01f
            && audio_sliders_ok;
        check(audio_sliders_ok, "audio_sliders_and_band_combo");

        check(content_.qaClickRestoreDefaults(), "restore_defaults_click");
        controller.refresh();
        check(controller.getNumEqBands() == FxController::DEFAULT_NUM_EQ_BANDS
              && controller.getVolumeLeveling() == FxController::DEFAULT_VOLUME_LEVELING
              && controller.getMasterGain() == FxController::DEFAULT_MASTER_GAIN
              && controller.getBalance() == FxController::DEFAULT_BALANCE
              && controller.getFilterQ() == FxController::DEFAULT_FILTER_Q,
              "restore_defaults_values");

        // Restore the exact preset state that existed before QA. If it was
        // an unmodified factory preset, RESET removes the temporary autosave
        // created by the slider tests so QA leaves no trace.
        bool preset_state_restored = controller.setPresetName(original_preset);
        if (preset_state_restored && !original_preset_modified)
        {
            preset_state_restored = controller.resetCurrentPreset();
            check(preset_state_restored && eq_matches_original(),
                  "qa_factory_reset_restores_exact_eq");
        }
        else if (preset_state_restored)
        {
            controller.setNumEqBands(original_bands);
            for (int i = 0; i < FxEffects::NumEffects; ++i)
                controller.setEffectValue(
                    static_cast<FxEffects::EffectType>(i),
                    original_effects[static_cast<size_t>(i)] * 10.0f);

            for (int i = 0; i < original_bands; ++i)
            {
                controller.setEqBandFrequency(
                    i, original_eq_frequency[static_cast<size_t>(i)]);
                controller.setEqBandBoostCut(
                    i, original_eq_gain[static_cast<size_t>(i)]);
            }
        }

        // Audio controls and band count are persisted separately from the
        // factory preset payload, so restore those after preset restoration.
        controller.setNumEqBands(original_bands);
        controller.refresh();
        check(eq_matches_original(), "qa_band_restore_preserves_exact_eq");

        controller.setVolumeLeveling(original_volume);
        controller.setMasterGain(original_master);
        controller.setBalance(original_balance);
        controller.setFilterQ(original_filter_q);
        controller.refresh();

        preset_state_restored = preset_state_restored
            && controller.getCurrentPreset() == original_preset
            && controller.isPresetModified() == original_preset_modified
            && controller.getNumEqBands() == original_bands
            && std::fabs(controller.getVolumeLeveling() - original_volume) < 0.01f
            && std::fabs(controller.getMasterGain() - original_master) < 0.01f
            && std::fabs(controller.getBalance() - original_balance) < 0.01f
            && std::fabs(controller.getFilterQ() - original_filter_q) < 0.01f;

        for (int i = 0; i < FxEffects::NumEffects; ++i)
            preset_state_restored = preset_state_restored
                && std::fabs(controller.getEffectValue(
                       static_cast<FxEffects::EffectType>(i))
                       - original_effects[static_cast<size_t>(i)]) < 0.001f;

        if (controller.getNumEqBands() == original_bands)
        {
            for (int i = 0; i < original_bands; ++i)
            {
                preset_state_restored = preset_state_restored
                    && std::fabs(controller.getEqBandFrequency(i)
                                 - original_eq_frequency[static_cast<size_t>(i)]) < 0.25f
                    && std::fabs(controller.getEqBandBoostCut(i)
                                 - original_eq_gain[static_cast<size_t>(i)]) < 0.01f;
            }
        }

        check(preset_state_restored, "qa_restore_original_audio_state");
        check(content_.qaRestoreAudioView(), "flip_restore_effects");

        const auto original_theme = FxTheme::getThemeMode();
        const auto other_theme = original_theme == FxThemeMode::Dark
            ? FxThemeMode::Light : FxThemeMode::Dark;
        applyTheme(other_theme);
        check(FxTheme::getThemeMode() == other_theme, "theme_toggle");
        applyTheme(original_theme);
        check(FxTheme::getThemeMode() == original_theme, "theme_restore");

        const bool original_top = isAlwaysOnTop();
        prefs_.always_on_top = !original_top;
        setAlwaysOnTop(prefs_.always_on_top);
        saveUiPreferences(prefs_);
        check(isAlwaysOnTop() == !original_top, "always_on_top_toggle");
        prefs_.always_on_top = original_top;
        setAlwaysOnTop(original_top);
        saveUiPreferences(prefs_);
        check(isAlwaysOnTop() == original_top, "always_on_top_restore");

        content_.refreshFromDaemon();
        check(FxController::getInstance().isConnected(), "refresh");

        check(static_cast<bool>(minimize_button_.onClick), "minimize_callback_wired");
        check(static_cast<bool>(menu_button_.onClick), "menu_callback_wired");
        check(static_cast<bool>(donate_button_.onClick), "donate_callback_wired");

        report = out.str();
        return all_ok;
    }

private:
    void configureToolbar()
    {
        menu_image_ = juce::Drawable::createFromImageData(FXIMAGE(MenuButton), FXIMAGESIZE(MenuButton));
        menu_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(MenuButtonHover), FXIMAGESIZE(MenuButtonHover));
        menu_button_.setImages(menu_image_.get(), menu_hover_image_.get());
        menu_button_.setSize(24, 24);
        menu_button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        menu_button_.onClick = [this] { showMenu(); };

        minimize_image_ = juce::Drawable::createFromImageData(FXIMAGE(MinimizeWindowButton), FXIMAGESIZE(MinimizeWindowButton));
        minimize_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(MinimizeWindowButtonHover), FXIMAGESIZE(MinimizeWindowButtonHover));
        minimize_button_.setImages(minimize_image_.get(), minimize_hover_image_.get());
        minimize_button_.setSize(26, 30);
        minimize_button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        minimize_button_.onClick = [this]
        {
            if (auto* peer = getPeer())
            {
                peer->setMinimised(true);
                fxsoundMacMiniaturize(peer->getNativeHandle());
            }
        };

        resize_button_.setSize(26, 26);
        resize_button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        resize_button_.onClick = [this]
        {
            content_.setCompact(!content_.isCompact());
            prefs_.compact = content_.isCompact();
            saveUiPreferences(prefs_);
            updateResizeImage();
            setContent(&content_);
            centreWithSize(getWidth(), getHeight());
        };
        updateResizeImage();

        power_button_.setSize(24, 24);
        power_button_.setImageWidth(24);
        power_button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        power_button_.setPowerState(FxController::getInstance().isPowerOn());
        power_button_.onClick = [this]
        {
            auto& controller = FxController::getInstance();
            const bool requested = !power_button_.getPowerState();
            controller.setPowerState(requested);

            // Re-read daemon truth so a failed command can never leave the UI
            // showing a power state that is not actually active.
            controller.refresh();
            const bool actual = controller.isPowerOn();
            power_button_.setPowerState(actual);
            content_.setPowerEnabled(actual);
        };

        donate_image_ = juce::Drawable::createFromImageData(FXIMAGE(DonateButton), FXIMAGESIZE(DonateButton));
        donate_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(DonateButtonHover), FXIMAGESIZE(DonateButtonHover));
        donate_button_.setImages(donate_image_.get(), donate_hover_image_.get());
        donate_button_.setSize(26, 30);
        donate_button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        donate_button_.onClick = []
        {
            juce::URL("https://www.paypal.com/donate/?hosted_button_id=JVNQGYXCQ2GPG")
                .launchInDefaultBrowser();
        };

        addToolbarButton(&menu_button_, false);
        addToolbarButton(&minimize_button_);
        addToolbarButton(&resize_button_);
        addToolbarButton(&power_button_);
        addToolbarButton(&donate_button_);
    }

    void updateResizeImage()
    {
        const auto normal = content_.isCompact() ? FxImage::MaximizeButton : FxImage::MinimizeButton;
        const auto hover = content_.isCompact() ? FxImage::MaximizeButtonHover : FxImage::MinimizeButtonHover;

        resize_image_ = juce::Drawable::createFromImageData(
            FxTheme::getImage(normal), FxTheme::getImageSize(normal));
        resize_hover_image_ = juce::Drawable::createFromImageData(
            FxTheme::getImage(hover), FxTheme::getImageSize(hover));
        resize_button_.setImages(resize_image_.get(), resize_hover_image_.get());
    }

    void applyTheme(FxThemeMode mode)
    {
        FxTheme::setThemeMode(mode);
        prefs_.theme = mode;
        saveUiPreferences(prefs_);

        menu_image_ = juce::Drawable::createFromImageData(FXIMAGE(MenuButton), FXIMAGESIZE(MenuButton));
        menu_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(MenuButtonHover), FXIMAGESIZE(MenuButtonHover));
        menu_button_.setImages(menu_image_.get(), menu_hover_image_.get());

        minimize_image_ = juce::Drawable::createFromImageData(FXIMAGE(MinimizeWindowButton), FXIMAGESIZE(MinimizeWindowButton));
        minimize_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(MinimizeWindowButtonHover), FXIMAGESIZE(MinimizeWindowButtonHover));
        minimize_button_.setImages(minimize_image_.get(), minimize_hover_image_.get());

        donate_image_ = juce::Drawable::createFromImageData(FXIMAGE(DonateButton), FXIMAGESIZE(DonateButton));
        donate_hover_image_ = juce::Drawable::createFromImageData(FXIMAGE(DonateButtonHover), FXIMAGESIZE(DonateButtonHover));
        donate_button_.setImages(donate_image_.get(), donate_hover_image_.get());

        updateResizeImage();
        sendLookAndFeelChange();
        content_.sendLookAndFeelChange();
        content_.refreshFromDaemon();
        repaint();
    }

    void toggleAlwaysOnTopPreference()
    {
        prefs_.always_on_top = !isAlwaysOnTop();
        setAlwaysOnTop(prefs_.always_on_top);
        saveUiPreferences(prefs_);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        juce::PopupMenu theme_menu;

        theme_menu.addItem(
            "Dark",
            true,
            FxTheme::getThemeMode() == FxThemeMode::Dark,
            [this] { applyTheme(FxThemeMode::Dark); });
        theme_menu.addItem(
            "Light",
            true,
            FxTheme::getThemeMode() == FxThemeMode::Light,
            [this] { applyTheme(FxThemeMode::Light); });

        menu.addItem("Refresh", [this]
        {
            content_.refreshFromDaemon();
            power_button_.setPowerState(FxController::getInstance().isPowerOn());
        });
        menu.addSeparator();
        menu.addItem("Download Bonus Presets", []
        {
            juce::URL("https://www.fxsound.com/presets").launchInDefaultBrowser();
        });
        menu.addSeparator();
        menu.addSubMenu("Theme", theme_menu);
        menu.addItem(
            "Always On Top",
            true,
            isAlwaysOnTop(),
            [this] { toggleAlwaysOnTopPreference(); });
        menu.addSeparator();
        menu.addItem("Donate", []
        {
            juce::URL("https://www.paypal.com/donate/?hosted_button_id=JVNQGYXCQ2GPG")
                .launchInDefaultBrowser();
        });
        menu.addSeparator();
        menu.addItem("Quit", [this] { closeButtonPressed(); });

        menu.showAt(&menu_button_);
    }

    UiPreferences prefs_{loadUiPreferences()};
    FxLinuxContent content_;
    FxPowerButton power_button_;
    juce::DrawableButton menu_button_;
    juce::DrawableButton resize_button_;
    juce::DrawableButton minimize_button_;
    juce::DrawableButton donate_button_;

    std::unique_ptr<juce::Drawable> menu_image_;
    std::unique_ptr<juce::Drawable> menu_hover_image_;
    std::unique_ptr<juce::Drawable> resize_image_;
    std::unique_ptr<juce::Drawable> resize_hover_image_;
    std::unique_ptr<juce::Drawable> minimize_image_;
    std::unique_ptr<juce::Drawable> minimize_hover_image_;
    std::unique_ptr<juce::Drawable> donate_image_;
    std::unique_ptr<juce::Drawable> donate_hover_image_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxLinuxWindow)
};

class FxSoundLinuxApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "FxSound"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& command_line) override
    {
        const auto prefs = loadUiPreferences();
        theme_ = std::make_unique<FxTheme>();
        juce::LookAndFeel::setDefaultLookAndFeel(theme_.get());
        FxTheme::setThemeMode(prefs.theme);
        const bool start_minimized = command_line.containsIgnoreCase("--minimized");
        const bool qa_self_test = command_line.containsIgnoreCase("--qa-self-test");
        const bool qa_minimize_button = command_line.containsIgnoreCase("--qa-minimize-button");
        const bool qa_menu_button = command_line.containsIgnoreCase("--qa-menu-button");
        window_ = std::make_unique<FxLinuxWindow>(start_minimized);

        if (qa_minimize_button)
        {
            juce::Timer::callAfterDelay(250, [this]
            {
                if (window_)
                    window_->qaClickMinimizeButton();
            });
        }

        if (qa_menu_button)
        {
            juce::Timer::callAfterDelay(250, [this]
            {
                if (window_)
                    window_->qaClickMenuButton();
            });
        }

        if (qa_self_test)
        {
            juce::MessageManager::callAsync([this]
            {
                juce::String report;
                const bool ok = window_ && window_->runQaSelfTest(report);
                std::ofstream out("/tmp/fxsound-ui-qa.txt", std::ios::trunc);
                out << report.toStdString();
                out << (ok ? "RESULT PASS\n" : "RESULT FAIL\n");
                out.close();
            });
        }
    }

    void shutdown() override
    {
        window_.reset();

        // FxEqualizer owns JUCE accessibility objects. Destroy it while JUCE
        // and AppKit are still alive instead of during C++ static teardown.
        FxEqualizer::destroyInstance();

        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        theme_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (window_)
            window_->restoreAndBringToFront();
    }

private:
    std::unique_ptr<FxTheme> theme_;
    std::unique_ptr<FxLinuxWindow> window_;
};

START_JUCE_APPLICATION(FxSoundLinuxApplication)
