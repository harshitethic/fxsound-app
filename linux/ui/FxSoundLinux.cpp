/*
 * FxSound Linux desktop UI.
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
#include <filesystem>
#include <fstream>
#include <string>

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
        return "fxsound-linux-ui.conf";
    return std::filesystem::path(home) / ".config" / "fxsound-linux" / "ui.conf";
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
    FxLinuxWindow()
        : FxWindow(),
          power_button_("powerButton"),
          menu_button_("menuButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          resize_button_("resizeButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          minimize_button_("minimizeButton", juce::DrawableButton::ButtonStyle::ImageFitted),
          donate_button_("donateButton", juce::DrawableButton::ButtonStyle::ImageFitted)
    {
        setName("FxSound Linux");
        setOpaque(false);
        enableShadow(false);

        content_.setCompact(prefs_.compact);
        setAlwaysOnTop(prefs_.always_on_top);
        configureToolbar();

        setContent(&content_);
        centreWithSize(getWidth(), getHeight());
        addToDesktop(juce::ComponentPeer::windowAppearsOnTaskbar);
        setVisible(true);
        toFront(true);
    }

    ~FxLinuxWindow() override
    {
        removeFromDesktop();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
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
                peer->setMinimised(true);
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
            const bool on = !power_button_.getPowerState();
            controller.setPowerState(on);
            power_button_.setPowerState(on);
            content_.setPowerEnabled(on);
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
            [this]
            {
                prefs_.always_on_top = !isAlwaysOnTop();
                setAlwaysOnTop(prefs_.always_on_top);
                saveUiPreferences(prefs_);
            });
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
    const juce::String getApplicationName() override { return "FxSound Linux"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        const auto prefs = loadUiPreferences();
        theme_ = std::make_unique<FxTheme>();
        juce::LookAndFeel::setDefaultLookAndFeel(theme_.get());
        FxTheme::setThemeMode(prefs.theme);
        window_ = std::make_unique<FxLinuxWindow>();
    }

    void shutdown() override
    {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        theme_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<FxTheme> theme_;
    std::unique_ptr<FxLinuxWindow> window_;
};

START_JUCE_APPLICATION(FxSoundLinuxApplication)
