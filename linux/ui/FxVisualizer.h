/*
 * FxSound Linux visualizer.
 * Same rendering/layout as upstream FxSound; Timer replaces AnimatedAppComponent
 * so the Linux UI can stay on the minimal JUCE GUI modules.
 * AGPL-3.0-or-later, matching upstream FxSound.
 */
#pragma once

#include <JuceHeader.h>

class FxVisualizer : public Component, private Timer
{
public:
    FxVisualizer();
    ~FxVisualizer() override = default;

    void start();
    void pause();
    void reset();
    void update();
    void calcGradient();

private:
    static constexpr int WIDTH = 960;
    static constexpr int HEIGHT = 120;
    static constexpr int NUM_BARS = 10;

    void timerCallback() override;
    void paint(Graphics& g) override;
    void enablementChanged() override;
    void lookAndFeelChanged() override;

    Array<float> band_values_;
    Array<float> band_graph_;
    ColourGradient gradient_;
};
