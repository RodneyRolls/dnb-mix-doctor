#include "Visualizer.h"
#include "../PluginProcessor.h"

namespace hifi
{
    using namespace juce;

    namespace
    {
        constexpr float displayHeadroom = 0.86f;   // 0 dBFS sits at 86 % of the half height
        const char* tabNames[] = { "WAVE", "SPECTRUM", "STEREO" };

        Colour bandColour (const mcdsp::VisColumn& c)
        {
            // weight highs up: they carry far less energy than the sub but matter as much visually
            const float wl = c.eLow, wm = c.eMid * 3.0f, wh = c.eHigh * 12.0f;
            const float sum = wl + wm + wh;
            if (sum <= 1.0e-12f) return colours::midAmber;
            auto mixc = [&] (auto get) { return (get (colours::subRed) * wl + get (colours::midAmber) * wm + get (colours::highCyan) * wh) / sum; };
            return Colour::fromFloatRGBA (mixc ([] (Colour k) { return k.getFloatRed(); }),
                                          mixc ([] (Colour k) { return k.getFloatGreen(); }),
                                          mixc ([] (Colour k) { return k.getFloatBlue(); }), 1.0f);
        }

        float freqToX (float f, Rectangle<float> r)
        {
            return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        }
    }

    Visualizer::Visualizer (DnBMixDoctorProcessor& p) : proc (p)
    {
        specIn.fill (-100.0f);
        specOut.fill (-100.0f);
        setOpaque (false);
    }

    void Visualizer::resized()
    {
        auto r = getLocalBounds().toFloat();
        glass = r.reduced (7.0f);
        plot = glass.reduced (12.0f, 10.0f).withTrimmedTop (16.0f);
        auto tabRow = glass.reduced (10.0f, 6.0f).removeFromTop (16.0f);
        for (int i = 2; i >= 0; --i)
            tabs[(size_t) i] = tabRow.removeFromRight (i == 1 ? 82.0f : 62.0f).reduced (2.0f, 0.0f);
        background = {};
        phosphor = {};
    }

    void Visualizer::mouseDown (const MouseEvent& e)
    {
        for (int i = 0; i < 3; ++i)
            if (tabs[(size_t) i].expanded (2.0f).contains (e.position))
            {
                view = (View) i;
                repaint();
            }
    }

    //==============================================================================
    void Visualizer::frame (double t)
    {
        const double dt = lastFrameTime > 0 ? jlimit (0.0, 0.1, t - lastFrameTime) : 1.0 / 60.0;
        lastFrameTime = t;

        pullColumns();
        pullSpectrum();
        pullStereo();
        if (view == View::spectrum) computeSpectrum();

        if (t - lastColumnTime > 0.15) { rmsL *= 0.8f; rmsR *= 0.8f; }   // host stopped calling processBlock

        if (onFrame) onFrame (dt);
        repaint();
    }

    void Visualizer::pullColumns()
    {
        const int n = proc.columnFifo.pop (popBuf.data(), numCols);
        if (n == 0) return;

        double pl = 0, pr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& c = popBuf[(size_t) i];
            cols[(size_t) colWrite] = c;
            colWrite = (colWrite + 1) % numCols;
            pl += c.rmsL * c.rmsL; pr += c.rmsR * c.rmsR;
            if (std::max (std::abs (c.outMax), std::abs (c.outMin)) >= 0.9999f)
                clipFlashUntil = lastFrameTime + 0.6;
        }
        colCount += n;
        rmsL = (float) std::sqrt (pl / n);
        rmsR = (float) std::sqrt (pr / n);
        lastColumnTime = lastFrameTime;
    }

    void Visualizer::pullSpectrum()
    {
        const int ready = jmin (proc.spectrumInFifo.getNumReady(), proc.spectrumOutFifo.getNumReady(), fftSize);
        if (ready <= 0) return;
        const int a = proc.spectrumInFifo.pop (popTmpIn.data(), ready);
        const int b = proc.spectrumOutFifo.pop (popTmpOut.data(), ready);
        const int n = jmin (a, b);
        for (int i = 0; i < n; ++i)
        {
            ringIn[(size_t) ringPos] = popTmpIn[(size_t) i];
            ringOut[(size_t) ringPos] = popTmpOut[(size_t) i];
            ringPos = (ringPos + 1) % fftSize;
        }
    }

    void Visualizer::pullStereo()
    {
        stereoCount = proc.stereoFifo.pop (stereoTmp.data(), (int) stereoTmp.size());
        if (stereoCount == 0) return;
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < stereoCount; ++i)
        {
            const auto& f = stereoTmp[(size_t) i];
            lr += (double) f.l * f.r; ll += (double) f.l * f.l; rr += (double) f.r * f.r;
        }
        if (ll > 1e-9 && rr > 1e-9)
            correlation += ((float) (lr / std::sqrt (ll * rr)) - correlation) * 0.15f;
    }

    void Visualizer::computeSpectrum()
    {
        const double sr = proc.currentSampleRate.load();
        auto analyse = [&] (const std::array<float, fftSize>& ring, std::array<float, numPoints>& out)
        {
            for (int i = 0; i < fftSize; ++i) fftBuf[(size_t) i] = ring[(size_t) ((ringPos + i) % fftSize)];
            std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
            window.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);

            const float norm = 4.0f / (float) fftSize;   // full-scale sine -> 0 dB with a Hann window
            for (int p = 0; p < numPoints; ++p)
            {
                const float f0 = 20.0f * std::pow (1000.0f, (float) p / numPoints);
                const float f1 = 20.0f * std::pow (1000.0f, (float) (p + 1) / numPoints);
                const float bin0 = (float) (f0 / sr * fftSize), bin1 = (float) (f1 / sr * fftSize);
                float mag = 0;
                if (bin1 - bin0 < 1.5f)
                {
                    // low end: fewer than ~1 bin per point -> interpolate for a smooth curve
                    const float centre = jlimit (1.0f, (float) (fftSize / 2 - 2), 0.5f * (bin0 + bin1));
                    const int b = (int) centre;
                    const float frac = centre - (float) b;
                    mag = fftBuf[(size_t) b] * (1.0f - frac) + fftBuf[(size_t) b + 1] * frac;
                }
                else
                {
                    const int b0 = jlimit (1, fftSize / 2 - 1, (int) bin0);
                    const int b1 = jlimit (b0, fftSize / 2 - 1, (int) bin1);
                    for (int b = b0; b <= b1; ++b) mag = jmax (mag, fftBuf[(size_t) b]);
                }
                // +3 dB/oct tilt around 1 kHz so a well balanced mix reads roughly flat
                const float db = Decibels::gainToDecibels (mag * norm, -120.0f) + 3.0f * std::log2 (f0 / 1000.0f);
                auto& s = out[(size_t) p];
                s += (db - s) * (db > s ? 0.55f : 0.10f);
            }
        };
        analyse (ringIn, specIn);
        analyse (ringOut, specOut);
    }

    //==============================================================================
    void Visualizer::renderBackground (float scale)
    {
        bgScale = scale;
        background = Image (Image::ARGB, jmax (1, roundToInt (getWidth() * scale)), jmax (1, roundToInt (getHeight() * scale)), true);
        Graphics g (background);
        g.addTransform (AffineTransform::scale (scale));
        const auto outer = getLocalBounds().toFloat();

        // chrome bezel
        ColourGradient bezel (Colour (0xfff4f2ec), 0, outer.getY(), Colour (0xff57534d), 0, outer.getBottom(), false);
        bezel.addColour (0.5, Colour (0xffb9b5ad));
        g.setGradientFill (bezel);
        g.fillRoundedRectangle (outer, 9.0f);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (glass.expanded (1.5f), 6.0f);

        // smoked glass with warm backlight
        ColourGradient bg (Colour (0xff1d140c), glass.getCentreX(), glass.getBottom() + 20.0f,
                           colours::glass, glass.getCentreX(), glass.getY(), true);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (glass, 5.0f);
        ColourGradient warm (colours::amber.withAlpha (0.10f), glass.getCentreX(), glass.getCentreY() + glass.getHeight() * 0.3f,
                             Colours::transparentBlack, glass.getX(), glass.getY(), true);
        g.setGradientFill (warm);
        g.fillRoundedRectangle (glass, 5.0f);

        // scanlines
        for (float y = glass.getY() + 1.0f; y < glass.getBottom(); y += 2.0f)
        {
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRect (glass.getX(), y, glass.getWidth(), 0.6f);
        }
        // inner shadow
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (glass, 5.0f, 2.0f);
    }

    void Visualizer::paint (Graphics& g)
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (background.isNull() || bgScale != scale)
            renderBackground (scale);
        g.drawImage (background, getLocalBounds().toFloat());

        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (glass.reduced (2.0f).toNearestInt());
            switch (view)
            {
                case View::wave:     drawWave (g); break;
                case View::spectrum: drawSpectrum (g); break;
                case View::stereo:   drawStereo (g); break;
            }
        }
        drawTabs (g);

        // glass reflection
        ColourGradient glare (Colours::white.withAlpha (0.06f), glass.getX(), glass.getY(),
                              Colours::transparentWhite, glass.getX() + glass.getWidth() * 0.35f, glass.getBottom(), false);
        g.setGradientFill (glare);
        g.fillRoundedRectangle (glass, 5.0f);
    }

    void Visualizer::drawTabs (Graphics& g)
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto& r = tabs[(size_t) i];
            const bool active = (int) view == i;
            g.setColour (active ? colours::amber.withAlpha (0.18f) : Colours::white.withAlpha (0.03f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (active ? colours::amber.withAlpha (0.8f) : colours::amber.withAlpha (0.25f));
            g.drawRoundedRectangle (r, 3.0f, 0.8f);
            g.setFont (labelFont (9.5f));
            g.setColour (active ? colours::amberHot : colours::amber.withAlpha (0.45f));
            g.drawText (tabNames[i], r, Justification::centred);
        }
    }

    //==============================================================================
    void Visualizer::drawWave (Graphics& g)
    {
        const float midY = plot.getCentreY();
        const float halfH = plot.getHeight() * 0.5f;
        const float k = halfH * displayHeadroom;
        const float colW = plot.getWidth() / (float) numCols;
        auto yFor = [&] (float v) { return midY - jlimit (-1.0f / displayHeadroom, 1.0f / displayHeadroom, v) * k; };

        // grid: -6 / -12 dB lines, centre line, one vertical line per second scrolling with the audio
        g.setColour (colours::amber.withAlpha (0.07f));
        for (float lvl : { 0.5f, 0.25f })
        {
            g.drawHorizontalLine (roundToInt (yFor (lvl)), plot.getX(), plot.getRight());
            g.drawHorizontalLine (roundToInt (yFor (-lvl)), plot.getX(), plot.getRight());
        }
        g.setColour (colours::amber.withAlpha (0.14f));
        g.drawHorizontalLine (roundToInt (midY), plot.getX(), plot.getRight());
        const int perSecond = mcdsp::ColumnBuilder::columnsPerSecond;
        for (int c = 0; c < numCols; ++c)
            if (((colCount - numCols + c) % perSecond) == 0)
            {
                g.setColour (colours::amber.withAlpha (0.06f));
                g.drawVerticalLine (roundToInt (plot.getX() + c * colW), plot.getY(), plot.getBottom());
            }
        g.setFont (labelFont (8.5f));
        g.setColour (colours::amber.withAlpha (0.35f));
        g.drawText ("-6", Rectangle<float> (plot.getRight() - 24, yFor (0.5f) - 10, 22, 10), Justification::centredRight);
        g.drawText ("-12", Rectangle<float> (plot.getRight() - 24, yFor (0.25f) - 10, 22, 10), Justification::centredRight);

        // processed signal: solid, band-coloured per column
        for (int c = 0; c < numCols; ++c)
        {
            const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
            const float y1 = yFor (col.outMax), y2 = yFor (col.outMin);
            const auto colour = bandColour (col);
            g.setColour (colour.withAlpha (0.92f));
            g.fillRect (plot.getX() + c * colW, y1, colW + 0.5f, jmax (1.0f, y2 - y1));
        }
        // soft bloom over the waveform
        ColourGradient bloom (Colours::white.withAlpha (0.10f), 0, midY - k, Colours::transparentWhite, 0, midY, false);
        g.setGradientFill (bloom);
        g.fillRect (plot.withTop (midY - k).withBottom (midY));

        // unprocessed input: thin ghost outline
        Path ghost;
        for (int c = 0; c < numCols; ++c)
        {
            const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
            const float x = plot.getX() + c * colW;
            if (c == 0) ghost.startNewSubPath (x, yFor (col.inMax)); else ghost.lineTo (x, yFor (col.inMax));
        }
        for (int c = numCols - 1; c >= 0; --c)
        {
            const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
            ghost.lineTo (plot.getX() + c * colW, yFor (col.inMin));
        }
        g.setColour (Colours::white.withAlpha (0.38f));
        g.strokePath (ghost, PathStrokeType (0.9f));

        // peak-hold markers per quarter second
        const int win = perSecond / 4;
        for (int start = 0; start < numCols; start += win)
        {
            float pk = 0;
            for (int c = start; c < start + win && c < numCols; ++c)
            {
                const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
                pk = jmax (pk, std::abs (col.outMax), std::abs (col.outMin));
            }
            if (pk < 0.01f) continue;
            const float x0 = plot.getX() + start * colW, x1 = x0 + win * colW - 2.0f;
            g.setColour (colours::amberHot.withAlpha (0.85f));
            g.fillRect (x0 + 1.0f, yFor (pk) - 1.0f, x1 - x0, 1.5f);
            g.fillRect (x0 + 1.0f, yFor (-pk), x1 - x0, 1.5f);
        }

        // 0 dBFS clip lines, flashing red after an over
        const bool flashing = lastFrameTime < clipFlashUntil && std::fmod (lastFrameTime, 0.16) < 0.08;
        const bool clipped = lastFrameTime < clipFlashUntil;
        g.setColour (clipped ? (flashing ? colours::clipRed : colours::clipRed.withAlpha (0.5f)) : colours::clipRed.withAlpha (0.35f));
        const float dash[] = { 5.0f, 4.0f };
        for (float s : { 1.0f, -1.0f })
            g.drawDashedLine (Line<float> (plot.getX(), yFor (s), plot.getRight(), yFor (s)), dash, 2, clipped ? 1.6f : 1.0f);
        g.setFont (labelFont (8.5f));
        g.drawText ("0 dBFS", Rectangle<float> (plot.getX() + 2, yFor (-1.0f) + 2, 48, 10), Justification::centredLeft);

        // gain-reduction trace along the top edge
        const float grH = plot.getHeight() * 0.22f;
        Path gr;
        gr.startNewSubPath (plot.getX(), glass.getY() + 2.0f);
        for (int c = 0; c < numCols; ++c)
        {
            const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
            gr.lineTo (plot.getX() + c * colW, glass.getY() + 2.0f + jlimit (0.0f, 1.0f, -col.grDb / 12.0f) * grH);
        }
        gr.lineTo (plot.getRight(), glass.getY() + 2.0f);
        gr.closeSubPath();
        g.setColour (Colour (0xffff6a4a).withAlpha (0.22f));
        g.fillPath (gr);
        g.setColour (Colour (0xffff8a6a).withAlpha (0.55f));
        g.strokePath (gr, PathStrokeType (0.8f));
        g.setColour (colours::amber.withAlpha (0.45f));
        g.drawText ("GR", Rectangle<float> (plot.getX() + 2, glass.getY() + 4, 30, 10), Justification::centredLeft);
    }

    //==============================================================================
    void Visualizer::drawSpectrum (Graphics& g)
    {
        const float dbTop = 6.0f, dbBottom = -78.0f;
        auto yFor = [&] (float db) { return jmap (jlimit (dbBottom, dbTop, db), dbBottom, dbTop, plot.getBottom(), plot.getY()); };

        g.setFont (labelFont (8.5f));
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, plot);
            g.setColour (colours::amber.withAlpha (0.08f));
            g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (colours::amber.withAlpha (0.4f));
            const String label = f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f);
            g.drawText (label, Rectangle<float> (x + 2, plot.getBottom() - 11, 30, 10), Justification::centredLeft);
        }
        for (float db = 0.0f; db >= -72.0f; db -= 12.0f)
        {
            g.setColour (colours::amber.withAlpha (db == 0.0f ? 0.16f : 0.07f));
            g.drawHorizontalLine (roundToInt (yFor (db)), plot.getX(), plot.getRight());
            g.setColour (colours::amber.withAlpha (0.35f));
            g.drawText (String ((int) db), Rectangle<float> (plot.getRight() - 26, yFor (db) - 10, 24, 10), Justification::centredRight);
        }

        auto curve = [&] (const std::array<float, numPoints>& s, bool closed)
        {
            Path p;
            for (int i = 0; i < numPoints; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (numPoints - 1);
                if (i == 0) p.startNewSubPath (x, yFor (s[(size_t) i])); else p.lineTo (x, yFor (s[(size_t) i]));
            }
            if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.lineTo (plot.getX(), plot.getBottom()); p.closeSubPath(); }
            return p;
        };

        auto outFill = curve (specOut, true);
        ColourGradient fillGrad (colours::amber.withAlpha (0.55f), 0, plot.getY(), colours::subRed.withAlpha (0.12f), 0, plot.getBottom(), false);
        g.setGradientFill (fillGrad);
        g.fillPath (outFill);
        g.setColour (colours::amberHot);
        g.strokePath (curve (specOut, false), PathStrokeType (1.4f));
        g.setColour (colours::highCyan.withAlpha (0.55f));
        g.strokePath (curve (specIn, false), PathStrokeType (1.0f));

        g.setFont (labelFont (9.0f));
        g.setColour (colours::highCyan.withAlpha (0.8f));
        g.drawText ("INPUT", Rectangle<float> (plot.getX() + 4, plot.getY() + 2, 60, 10), Justification::centredLeft);
        g.setColour (colours::amberHot);
        g.drawText ("OUTPUT", Rectangle<float> (plot.getX() + 64, plot.getY() + 2, 70, 10), Justification::centredLeft);
    }

    //==============================================================================
    void Visualizer::drawStereo (Graphics& g)
    {
        const float side = plot.getHeight();
        auto gonio = Rectangle<float> (side, side).withCentre ({ plot.getX() + plot.getWidth() * 0.32f, plot.getCentreY() });
        const auto c = gonio.getCentre();
        const float R = side * 0.5f;

        // axes + labels
        g.setColour (colours::amber.withAlpha (0.10f));
        g.drawEllipse (gonio.reduced (2.0f), 1.0f);
        g.drawLine (c.x, gonio.getY(), c.x, gonio.getBottom(), 1.0f);
        g.drawLine (gonio.getX(), c.y, gonio.getRight(), c.y, 1.0f);
        g.drawLine (c.x - R * 0.7f, c.y - R * 0.7f, c.x + R * 0.7f, c.y + R * 0.7f, 0.8f);
        g.drawLine (c.x + R * 0.7f, c.y - R * 0.7f, c.x - R * 0.7f, c.y + R * 0.7f, 0.8f);
        g.setFont (labelFont (9.0f));
        g.setColour (colours::amber.withAlpha (0.45f));
        g.drawText ("M", Rectangle<float> (c.x - 8, gonio.getY() + 2, 16, 10), Justification::centred);
        g.drawText ("L", Rectangle<float> (c.x - R * 0.72f - 14, c.y - R * 0.72f - 4, 12, 10), Justification::centred);
        g.drawText ("R", Rectangle<float> (c.x + R * 0.72f + 2, c.y - R * 0.72f - 4, 12, 10), Justification::centred);
        g.drawText ("S", Rectangle<float> (gonio.getRight() - 12, c.y - 12, 12, 10), Justification::centred);

        // phosphor persistence
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int px = jmax (1, roundToInt (side * scale));
        if (phosphor.isNull() || phosphor.getWidth() != px)
            phosphor = Image (Image::ARGB, px, px, true);
        phosphor.multiplyAllAlphas (0.80f);
        {
            Graphics pg (phosphor);
            const float pr = (float) px * 0.5f;
            const float dot = jmax (1.5f, scale * 2.0f);
            pg.setColour (colours::amberHot.withAlpha (0.8f));
            for (int i = 0; i < stereoCount; ++i)
            {
                const auto& f = stereoTmp[(size_t) i];
                const float sx = (f.l - f.r) * 0.7071f, my = (f.l + f.r) * 0.7071f;
                pg.fillRect (pr + sx * pr * 0.72f, pr - my * pr * 0.72f, dot, dot);
            }
        }
        g.drawImage (phosphor, gonio);

        // correlation meter
        auto meter = Rectangle<float> (plot.getWidth() * 0.34f, 16.0f)
                         .withCentre ({ plot.getX() + plot.getWidth() * 0.76f, plot.getCentreY() + 8.0f });
        drawInsetWindow (g, meter.expanded (3.0f), 3.0f);
        g.setColour (colours::clipRed.withAlpha (0.18f));
        g.fillRect (meter.withWidth (meter.getWidth() * 0.5f));
        g.setColour (colours::vfdGreen.withAlpha (0.10f));
        g.fillRect (meter.withTrimmedLeft (meter.getWidth() * 0.75f));
        g.setFont (labelFont (9.0f));
        for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        {
            const float x = meter.getX() + (v + 1.0f) * 0.5f * meter.getWidth();
            g.setColour (colours::amber.withAlpha (0.35f));
            g.drawVerticalLine (roundToInt (x), meter.getBottom() + 3, meter.getBottom() + 7);
            g.drawText (v > 0 ? "+" + String (v, 1) : String (v, 1), Rectangle<float> (x - 15, meter.getBottom() + 8, 30, 10), Justification::centred);
        }
        const float cx = meter.getX() + (jlimit (-1.0f, 1.0f, correlation) + 1.0f) * 0.5f * meter.getWidth();
        g.setColour (colours::amber.withAlpha (0.35f));
        g.fillRect (cx - 4.0f, meter.getY(), 8.0f, meter.getHeight());
        g.setColour (correlation < 0.0f ? colours::clipRed : colours::amberHot);
        g.fillRect (cx - 1.5f, meter.getY() - 2.0f, 3.0f, meter.getHeight() + 4.0f);

        g.setColour (colours::amber.withAlpha (0.6f));
        g.drawText ("CORRELATION", meter.withY (meter.getY() - 40).withHeight (12), Justification::centred);
        drawVfdText (g, String (correlation, 2), meter.withY (meter.getY() - 26).withHeight (16).withSizeKeepingCentre (90, 16),
                     correlation < 0.0f ? colours::clipRed : colours::vfd);
    }
}
