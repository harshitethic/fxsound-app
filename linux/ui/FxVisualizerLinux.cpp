/*
 * FxSound Linux spectrum visualizer.
 * Rendering logic mirrors upstream FxSound.
 * AGPL-3.0-or-later, matching upstream FxSound.
 */
#include "FxVisualizer.h"
#include "FxControllerLinux.h"
#include "FxTheme.h"

FxVisualizer::FxVisualizer()
{
    band_values_.resize(FxController::NUM_SPECTRUM_BANDS);
    band_graph_.resize(FxController::NUM_SPECTRUM_BANDS * NUM_BARS);

    calcGradient();
    reset();
    startTimerHz(10);

    setOpaque(false);
    setSize(WIDTH, HEIGHT);
}

void FxVisualizer::start()
{
    calcGradient();
    startTimerHz(30);
}

void FxVisualizer::pause()
{
    calcGradient();
    reset();
    repaint();
    startTimerHz(10);
}

void FxVisualizer::timerCallback()
{
    if (FxController::getInstance().isAudioProcessing())
        update();
    else
        reset();

    repaint();
}

void FxVisualizer::reset()
{
    for (int i = 0; i < FxController::NUM_SPECTRUM_BANDS * NUM_BARS; ++i)
        band_graph_.set(i, 0.0f);
}

void FxVisualizer::update()
{
    if (!isEnabled())
        return;

    FxController::getInstance().getSpectrumBandValues(band_values_);

    for (int i = 0; i < FxController::NUM_SPECTRUM_BANDS; ++i)
    {
        if (band_values_[i] < 0.0f || band_values_[i] > 1.0f)
            band_values_.set(i, 0.0f);

        for (int j = 0; j < NUM_BARS / 2; ++j)
        {
            band_graph_.set(i * NUM_BARS + j,
                            band_graph_[i * NUM_BARS + j + 1]);
            band_graph_.set(i * NUM_BARS + (NUM_BARS - 1) - j,
                            band_graph_[i * NUM_BARS + j + 1]);
        }

        band_graph_.set(i * NUM_BARS + NUM_BARS / 2, band_values_[i]);
    }
}

void FxVisualizer::paint(Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setFillType(FillType(Colour(FXCOLOR(ControlBackground)).withAlpha(1.0f)));
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);

    g.setGradientFill(gradient_);

    Path barsPath;
    float x = 27.0f;
    constexpr float dx = 9.1f;

    for (int i = 0; i < FxController::NUM_SPECTRUM_BANDS * NUM_BARS; ++i)
    {
        const float band_value = band_graph_[i] == 0.0f ? 0.01f : band_graph_[i];
        const float height = band_value * 100.0f;

        barsPath.addRectangle(x,
                              bounds.getHeight() / 2.0f - height / 2.0f,
                              4.0f,
                              height);
        x += dx;
    }

    g.fillPath(barsPath);
}

void FxVisualizer::enablementChanged()
{
    if (isEnabled())
        start();
    else
        pause();
}

void FxVisualizer::lookAndFeelChanged()
{
    calcGradient();
    repaint();
}

void FxVisualizer::calcGradient()
{
    float alpha = FxController::getInstance().isAudioProcessing() ? 1.0f : 0.75f;

    gradient_ = ColourGradient(
        isEnabled()
            ? Colour(FXCOLOR(GraphHigh)).withAlpha(alpha)
            : Colour(FXCOLOR(GraphHigh)).withSaturation(0.0f).withAlpha(alpha),
        2.0f,
        0.0f,
        isEnabled()
            ? Colour(FXCOLOR(GraphHigh)).withAlpha(alpha)
            : Colour(FXCOLOR(GraphHigh)).withSaturation(0.0f).withAlpha(alpha),
        2.0f,
        100.0f,
        false);

    gradient_.addColour(
        0.5f,
        isEnabled()
            ? Colour(FXCOLOR(GraphLow)).withAlpha(alpha)
            : Colour(FXCOLOR(GraphLow)).withSaturation(0.0f).withAlpha(alpha));
}
