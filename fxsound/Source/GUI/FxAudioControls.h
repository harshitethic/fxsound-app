/*
FxSound
Copyright (C) 2025  FxSound LLC

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include "FxAudioSlider.h"
#include "FxBalanceSlider.h"
#include "FxTheme.h"

//==============================================================================
/*
*/
class FxEffects : public Component
{
public:
	enum EffectType {Fidelity=0, Ambience=1, Surround=2, DynamicBoost=3, Bass=4, NumEffects=5};

	FxEffects();
	~FxEffects() = default;

	void update();
	void showValues(bool show);

    bool qaSetEffect(EffectType effect, double value)
    {
        const int i = static_cast<int>(effect);
        if (i < 0 || i >= static_cast<int>(effects_.size()) || effects_[i] == nullptr)
            return false;
        effects_[i]->setValue(value, NotificationType::sendNotificationSync);
        return true;
    }

private:
	class FxEffectSlider : public Slider
	{
	public:
		FxEffectSlider(EffectType effect);
		~FxEffectSlider() = default;

		void setEffectValue(float value);
		void showValue(bool show);

        void enablementChanged() override;

	private:
		static constexpr int LABEL_HEIGHT = 12;

		void resized() override;
		void valueChanged() override;
		bool keyPressed(const KeyPress& key) override;

		Label value_label_;

		EffectType effect_;
	};

	static constexpr int LABEL_HEIGHT = 14;
	static constexpr int SLIDER_WIDTH = 160;
	static constexpr int SLIDER_HEIGHT = 18;
	static constexpr int X_MARGIN = 8;
	static constexpr int Y_MARGIN = 21;

	void resized() override;
	void paint(Graphics& g) override;

	std::vector<std::unique_ptr<Label>> labels_;
	std::vector<std::unique_ptr<FxEffectSlider>> effects_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEffects)
};

class FxEqualizerControl : public Component
{
public:
	FxEqualizerControl();
	~FxEqualizerControl();

	void update();

    void setLookAndFeel(FxTheme& theme);

    // Internal QA hook used by the macOS port's --qa-self-test mode.
    bool qaClickRestoreDefaults()
    {
        if (!restore_defaults_button_.onClick)
            return false;
        restore_defaults_button_.onClick();
        return true;
    }

    bool qaSelectBands(int bands)
    {
        if (std::find(equalizer_bands_.begin(), equalizer_bands_.end(), bands) == equalizer_bands_.end())
            return false;
        equalizer_.setSelectedId(bands, NotificationType::sendNotificationSync);
        return true;
    }

    void qaSetMasterGain(double value)
    {
        master_gain_slider_.setValue(value, NotificationType::sendNotificationSync);
    }

    void qaSetVolumeLeveling(double value)
    {
        volume_leveling_slider_.setValue(value, NotificationType::sendNotificationSync);
    }

    void qaSetFilterQ(double value)
    {
        filter_q_slider_.setValue(value, NotificationType::sendNotificationSync);
    }

    void qaSetBalance(double value)
    {
        balance_slider_.setValue(value, NotificationType::sendNotificationSync);
    }

private:
	void resized() override;
	void paint(Graphics& g) override;

	static constexpr int X_MARGIN = 8;
	static constexpr int Y_MARGIN = 28;
	static constexpr int ROW_GAP = 8;
	static constexpr int LABEL_WIDTH = 52;
	static constexpr int CONTROL_GAP = 4;
	static constexpr int CONTROL_WIDTH = 100;
	static constexpr int COMBOBOX_HEIGHT = 20;
	static constexpr int SLIDER_WIDTH = 160;
	static constexpr int SLIDER_HEIGHT = 18;
	static constexpr int LABEL_HEIGHT = 14;
    static constexpr int BUTTON_WIDTH = 18;
	static constexpr int BUTTON_HEIGHT = 18;
	
	std::vector<int> equalizer_bands_ = { 5, 10, 15, 20, 31 };

	void setText();
	void updateEqualizerBandsText();
	void selectEqualizerBands();
	void restoreDefaults();

	void visibilityChanged() override;

	Label master_gain_title_;
	Label volume_leveling_title_;
	Label filter_q_title_;
	Label balance_title_;
	Label left_label_;
	Label right_label_;

	ComboBox equalizer_;
	FxAudioSlider master_gain_slider_;
	FxAudioSlider volume_leveling_slider_;
	FxAudioSlider filter_q_slider_;
	FxBalanceSlider balance_slider_;
	DrawableButton restore_defaults_button_;

	std::unique_ptr<Drawable> restore_defaults_image_;
	std::unique_ptr<Drawable> restore_defaults_hover_image_;
	
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxEqualizerControl)
};

class FxAudioControls : public Component
{
public:
	FxAudioControls();
	~FxAudioControls() = default;

	void update();
	void showValues(bool show);

	void setLookAndFeel();

    // Internal QA hooks used only by the macOS --qa-self-test mode.
    bool qaToggleView()
    {
        if (!flip_button_.onClick)
            return false;
        const bool before = effects_shown_;
        flip_button_.onClick();
        return effects_shown_ != before;
    }

    bool qaEffectsShown() const noexcept { return effects_shown_; }
    bool qaClickRestoreDefaults() { return equalizer_control_.qaClickRestoreDefaults(); }
    bool qaSetEffect(FxEffects::EffectType effect, double value) { return effects_.qaSetEffect(effect, value); }
    bool qaSelectBands(int bands) { return equalizer_control_.qaSelectBands(bands); }
    void qaSetMasterGain(double value) { equalizer_control_.qaSetMasterGain(value); }
    void qaSetVolumeLeveling(double value) { equalizer_control_.qaSetVolumeLeveling(value); }
    void qaSetFilterQ(double value) { equalizer_control_.qaSetFilterQ(value); }
    void qaSetBalance(double value) { equalizer_control_.qaSetBalance(value); }

private:
	static constexpr int WIDTH = 168;
	static constexpr int HEIGHT = 257;
	static constexpr int BUTTON_WIDTH = 18;
	static constexpr int BUTTON_HEIGHT = 18;

	void resized() override;
	void paint(Graphics& g) override;

	FxEffects effects_;
	FxEqualizerControl equalizer_control_;
	DrawableButton flip_button_;

	std::unique_ptr<Drawable> flip_image_;
	std::unique_ptr<Drawable> flip_hover_image_;

	bool effects_shown_;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxAudioControls)
};
