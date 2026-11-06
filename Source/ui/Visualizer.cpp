#include "Visualizer.h"
#include "../PluginProcessor.h"

namespace mcui
{
    using namespace juce;

    namespace
    {
        constexpr float displayHeadroom = 0.86f;   // 0 dBFS sits at 86 % of the half height
        const char* tabNames[] = { "Wave", "Spectrum", "Stereo" };

        float freqToX (float f, Rectangle<float> r)
        {
            return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        }

        /** One-pole coefficient for a time constant expressed in waveform columns. */
        float colCoef (double seconds)
        {
            return 1.0f - std::exp (-1.0f / (float) (seconds * mcdsp::ColumnBuilder::columnsPerSecond));
        }
    }

    Visualizer::Visualizer (DnBMixDoctorProcessor& p) : proc (p)
    {
        specIn.fill (-100.0f);
        specOut.fill (-100.0f);
        colTone.fill (0.4f);
        setOpaque (false);
    }

    void Visualizer::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        screen = r.reduced (8.0f);
        plot = screen.reduced (12.0f, 10.0f).withTrimmedTop (22.0f);
        auto tabRow = screen.reduced (8.0f, 7.0f).removeFromTop (20.0f).removeFromRight (222.0f);
        for (int i = 0; i < 3; ++i)
            tabs[(size_t) i] = tabRow.removeFromLeft (74.0f).reduced (1.0f, 0.0f);
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
    float Visualizer::columnBrightness (const mcdsp::VisColumn& c) noexcept
    {
        // weight mids and highs up: they carry far less energy than the sub but matter as much to the ear
        const float wl = c.eLow, wm = c.eMid * 3.0f, wh = c.eHigh * 12.0f;
        const float sum = wl + wm + wh;
        if (sum <= 1.0e-10f) return -1.0f;
        return (0.5f * wm + wh) / sum;
    }

    Colour Visualizer::loudnessColour (float peak) noexcept
    {
        // -24 dBFS and below = yellow, then round the colour wheel (green, cyan, blue, violet, magenta)
        // to red at 0 dBFS
        const float db = Decibels::gainToDecibels (peak, -100.0f);
        const float t = jlimit (0.0f, 1.0f, (db + 24.0f) / 24.0f);
        const float hue = std::fmod ((60.0f + 300.0f * t) / 360.0f, 1.0f);
        return Colour::fromHSV (hue, 0.92f, 1.0f, 1.0f);
    }

    Colour Visualizer::moodColour (float hueOffset, float brightness, float saturation) const noexcept
    {
        float h = std::fmod (moodHue + hueOffset, 1.0f);
        if (h < 0.0f) h += 1.0f;
        return Colour::fromHSV (h, jlimit (0.0f, 1.0f, saturation), jlimit (0.0f, 1.0f, brightness), 1.0f);
    }

    void Visualizer::frame (double t)
    {
        const double dt = lastFrameTime > 0 ? jlimit (0.0, 0.1, t - lastFrameTime) : 1.0 / 60.0;
        lastFrameTime = t;

        pullColumns();
        pullSpectrum();
        pullStereo();
        if (view == View::spectrum) computeSpectrum();

        if (t - lastColumnTime > 0.15) { rmsL *= 0.8f; rmsR *= 0.8f; energy *= 0.9f; }   // host stopped

        // mood hue: sub-heavy -> violet (0.78), bright -> cyan/teal (0.48), punchy -> pushed toward magenta/pink
        const float nb = jlimit (0.0f, 1.0f, (moodTone - 0.12f) / 0.55f);
        const float targetHue = 0.78f - 0.30f * nb + 0.13f * moodPunch;
        moodHue += (targetHue - moodHue) * (1.0f - std::exp (-(float) dt / 0.6f));

        if (onFrame) onFrame (dt);
        repaint();
    }

    void Visualizer::pullColumns()
    {
        const int n = proc.columnFifo.pop (popBuf.data(), numCols);
        if (n == 0) return;

        const float toneCoef = colCoef (1.5), punchCoef = colCoef (1.2), peakCoef = colCoef (0.6), energyCoef = colCoef (0.08);
        double pl = 0, pr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& c = popBuf[(size_t) i];
            const float tone = columnBrightness (c);
            const float peak = std::max (std::abs (c.outMax), std::abs (c.outMin));

            colTone[(size_t) colWrite] = tone < 0.0f ? moodTone : tone;
            cols[(size_t) colWrite] = c;
            colWrite = (colWrite + 1) % numCols;

            if (tone >= 0.0f && peak > 0.003f)
            {
                moodTone += (tone - moodTone) * toneCoef;
                slowPeak += (peak - slowPeak) * peakCoef;
                const float transient = jlimit (0.0f, 1.0f, (peak / jmax (1.0e-4f, slowPeak) - 1.0f) / 1.2f);
                moodPunch += (transient - moodPunch) * punchCoef;
            }
            energy += (jmin (1.0f, peak) - energy) * energyCoef;

            pl += c.rmsL * c.rmsL; pr += c.rmsR * c.rmsR;
            if (peak >= 0.9999f)
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
    void Visualizer::paint (Graphics& g)
    {
        const auto outer = getLocalBounds().toFloat().reduced (1.0f);
        drawCard (g, outer, 14.0f);
        g.setColour (colours::screen);
        g.fillRoundedRectangle (screen, 10.0f);

        {
            Graphics::ScopedSaveState ss (g);
            Path clip;
            clip.addRoundedRectangle (screen, 10.0f);
            g.reduceClipRegion (clip);

            // ambient light from the music: a soft glow in the current mood colour that breathes with the level
            drawGlow (g, { screen.getCentreX(), screen.getBottom() + 20.0f }, screen.getWidth() * 0.55f,
                      moodColour (0.0f, 0.9f).withAlpha (0.10f + 0.16f * energy));
            drawGlow (g, { screen.getX() + screen.getWidth() * 0.15f, screen.getY() }, screen.getWidth() * 0.35f,
                      moodColour (-0.12f, 0.9f).withAlpha (0.05f + 0.06f * energy));

            switch (view)
            {
                case View::wave:     drawWave (g); break;
                case View::spectrum: drawSpectrum (g); break;
                case View::stereo:   drawStereo (g); break;
            }
        }
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (screen.reduced (0.5f), 10.0f, 1.0f);
        drawTabs (g);
    }

    void Visualizer::drawTabs (Graphics& g)
    {
        auto bar = tabs[0].getUnion (tabs[2]).expanded (2.0f);
        g.setColour (colours::screen);
        g.fillRoundedRectangle (bar.expanded (3.0f), bar.getHeight() * 0.5f + 3.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
        for (int i = 0; i < 3; ++i)
        {
            const auto& r = tabs[(size_t) i];
            const bool active = (int) view == i;
            if (active)
            {
                g.setColour (moodColour (0.0f, 0.85f, 0.65f).withAlpha (0.35f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (moodColour (0.0f, 1.0f, 0.5f).withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
            }
            g.setFont (font (11.0f, active));
            g.setColour (active ? Colours::white : colours::textDim);
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

        // grid
        g.setColour (Colours::white.withAlpha (0.035f));
        for (float lvl : { 0.5f, 0.25f })
        {
            g.drawHorizontalLine (roundToInt (yFor (lvl)), plot.getX(), plot.getRight());
            g.drawHorizontalLine (roundToInt (yFor (-lvl)), plot.getX(), plot.getRight());
        }
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (roundToInt (midY), plot.getX(), plot.getRight());
        const int perSecond = mcdsp::ColumnBuilder::columnsPerSecond;
        g.setColour (Colours::white.withAlpha (0.03f));
        for (int c = 0; c < numCols; ++c)
            if (((colCount - numCols + c) % perSecond) == 0)
                g.drawVerticalLine (roundToInt (plot.getX() + c * colW), plot.getY(), plot.getBottom());
        g.setFont (font (9.0f));
        g.setColour (colours::textFaint);
        g.drawText ("-6", Rectangle<float> (plot.getRight() - 24, yFor (0.5f) - 11, 22, 10), Justification::centredRight);
        g.drawText ("-12", Rectangle<float> (plot.getRight() - 24, yFor (0.25f) - 11, 22, 10), Justification::centredRight);

        // processed signal: each column tinted around the mood hue by its own spectral balance, lit by its level
        Path topEdge, bottomEdge;
        for (int c = 0; c < numCols; ++c)
        {
            const int idx = (colWrite + c) % numCols;
            const auto& col = cols[(size_t) idx];
            const float y1 = yFor (col.outMax), y2 = yFor (col.outMin);
            const float x = plot.getX() + c * colW;
            // colour by the slice's RMS (+6 dB so a full-scale sine reads ~0 dB): shows loud vs quiet parts even on
            // a heavily limited master, where nearly every peak sits at the ceiling
            const float level = 2.0f * jmax (col.rmsL, col.rmsR);
            g.setColour (loudnessColour (level).withAlpha (0.92f));
            g.fillRect (x, y1, colW + 0.5f, jmax (1.0f, y2 - y1));
            if (c == 0) { topEdge.startNewSubPath (x, y1); bottomEdge.startNewSubPath (x, y2); }
            else        { topEdge.lineTo (x, y1);          bottomEdge.lineTo (x, y2); }
        }
        // bright rim along the envelope + soft bloom through the centre
        const auto rim = Colours::white.withAlpha (0.45f);
        g.setColour (rim);
        g.strokePath (topEdge, PathStrokeType (1.0f));
        g.strokePath (bottomEdge, PathStrokeType (1.0f));
        ColourGradient bloom (Colours::white.withAlpha (0.0f), 0, midY - k, Colours::white.withAlpha (0.0f), 0, midY + k, false);
        bloom.addColour (0.5, Colours::white.withAlpha (0.10f));
        g.setGradientFill (bloom);
        g.fillRect (plot.withTop (midY - k).withBottom (midY + k));

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
        g.setColour (Colours::white.withAlpha (0.28f));
        g.strokePath (ghost, PathStrokeType (0.8f));

        // peak-hold markers per quarter second
        const int win = perSecond / 4;
        g.setColour (Colours::white.withAlpha (0.38f));
        for (int start = 0; start < numCols; start += win)
        {
            float pk = 0;
            for (int c = start; c < start + win && c < numCols; ++c)
            {
                const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
                pk = jmax (pk, std::abs (col.outMax), std::abs (col.outMin));
            }
            if (pk < 0.01f) continue;
            const float x0 = plot.getX() + start * colW, x1 = x0 + win * colW - 3.0f;
            g.fillRect (x0 + 1.5f, yFor (pk) - 1.0f, x1 - x0, 1.0f);
            g.fillRect (x0 + 1.5f, yFor (-pk), x1 - x0, 1.0f);
        }

        // 0 dBFS lines, flashing red after an over
        const bool clipped = lastFrameTime < clipFlashUntil;
        const bool flashing = clipped && std::fmod (lastFrameTime, 0.16) < 0.08;
        g.setColour (clipped ? (flashing ? colours::bad : colours::bad.withAlpha (0.5f)) : colours::bad.withAlpha (0.30f));
        const float dash[] = { 4.0f, 4.0f };
        for (float s : { 1.0f, -1.0f })
            g.drawDashedLine (Line<float> (plot.getX(), yFor (s), plot.getRight(), yFor (s)), dash, 2, clipped ? 1.5f : 1.0f);
        g.setFont (font (9.0f, true));
        g.drawText ("0 dBFS", Rectangle<float> (plot.getX() + 2, yFor (-1.0f) + 2, 48, 10), Justification::centredLeft);

        // colour key: quiet (yellow) -> loud (red) through the spectrum
        {
            auto key = Rectangle<float> (150.0f, 6.0f).withPosition (screen.getRight() - 196.0f, screen.getBottom() - 14.0f);
            for (int i = 0; i < 150; ++i)
            {
                g.setColour (loudnessColour (Decibels::decibelsToGain (-24.0f + 24.0f * (float) i / 149.0f)));
                g.fillRect (key.getX() + (float) i, key.getY(), 1.0f, key.getHeight());
            }
            g.setFont (font (8.5f, true));
            g.setColour (colours::textDim);
            g.drawText ("QUIET", Rectangle<float> (key.getX() - 38.0f, key.getY() - 3.0f, 34.0f, 12.0f), Justification::centredRight);
            g.drawText ("LOUD", Rectangle<float> (key.getRight() + 4.0f, key.getY() - 3.0f, 34.0f, 12.0f), Justification::centredLeft);
        }

        // gain-reduction trace hanging from the top edge
        const float grTop = screen.getY() + 2.0f, grH = plot.getHeight() * 0.22f;
        Path gr;
        gr.startNewSubPath (plot.getX(), grTop);
        for (int c = 0; c < numCols; ++c)
        {
            const auto& col = cols[(size_t) ((colWrite + c) % numCols)];
            gr.lineTo (plot.getX() + c * colW, grTop + jlimit (0.0f, 1.0f, -col.grDb / 12.0f) * grH);
        }
        gr.lineTo (plot.getRight(), grTop);
        gr.closeSubPath();
        g.setColour (colours::pink.withAlpha (0.16f));
        g.fillPath (gr);
        g.setColour (colours::pink.withAlpha (0.55f));
        g.strokePath (gr, PathStrokeType (0.8f));
        g.setFont (font (9.5f, true));
        g.setColour (colours::pink.withAlpha (0.8f));
        g.drawText ("GAIN REDUCTION", Rectangle<float> (screen.getX() + 12, screen.getY() + 8, 120, 12), Justification::centredLeft);
    }

    //==============================================================================
    void Visualizer::drawSpectrum (Graphics& g)
    {
        const float dbTop = 6.0f, dbBottom = -78.0f;
        auto yFor = [&] (float db) { return jmap (jlimit (dbBottom, dbTop, db), dbBottom, dbTop, plot.getBottom(), plot.getY()); };

        g.setFont (font (9.0f));
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, plot);
            g.setColour (Colours::white.withAlpha (0.04f));
            g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (colours::textFaint);
            const String label = f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f);
            if (x < plot.getRight() - 30.0f)
                g.drawText (label, Rectangle<float> (x + 3, plot.getBottom() - 11, 30, 10), Justification::centredLeft);
            else
                g.drawText (label, Rectangle<float> (x - 33, plot.getBottom() - 11, 30, 10), Justification::centredRight);
        }
        for (float db = 0.0f; db >= -72.0f; db -= 12.0f)
        {
            g.setColour (Colours::white.withAlpha (db == 0.0f ? 0.08f : 0.035f));
            g.drawHorizontalLine (roundToInt (yFor (db)), plot.getX(), plot.getRight());
            g.setColour (colours::textFaint);
            g.drawText (String ((int) db), Rectangle<float> (plot.getRight() - 26, yFor (db) - 11, 24, 10), Justification::centredRight);
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

        ColourGradient fill (moodColour (0.08f, 0.95f).withAlpha (0.45f), plot.getX(), 0,
                             moodColour (-0.10f, 0.95f).withAlpha (0.45f), plot.getRight(), 0, false);
        g.setGradientFill (fill);
        g.fillPath (curve (specOut, true));
        ColourGradient fade (Colours::transparentBlack, 0, plot.getY(), colours::screen.withAlpha (0.85f), 0, plot.getBottom(), false);
        g.setGradientFill (fade);
        g.fillRect (plot);
        g.setColour (moodColour (0.0f, 1.0f, 0.35f));
        g.strokePath (curve (specOut, false), PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (0.40f));
        g.strokePath (curve (specIn, false), PathStrokeType (1.0f));

        auto legend = Rectangle<float> (screen.getX() + 12, screen.getY() + 8, 200, 12);
        g.setFont (font (9.5f, true));
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawText ("INPUT", legend.removeFromLeft (50), Justification::centredLeft);
        g.setColour (moodColour (0.0f, 1.0f, 0.35f));
        g.drawText ("OUTPUT", legend.removeFromLeft (60), Justification::centredLeft);
    }

    //==============================================================================
    void Visualizer::drawStereo (Graphics& g)
    {
        const float side = plot.getHeight();
        auto gonio = Rectangle<float> (side, side).withCentre ({ plot.getX() + plot.getWidth() * 0.32f, plot.getCentreY() });
        const auto c = gonio.getCentre();
        const float R = side * 0.5f;

        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawEllipse (gonio.reduced (2.0f), 1.0f);
        g.drawEllipse (gonio.reduced (R * 0.5f), 1.0f);
        g.drawLine (c.x, gonio.getY(), c.x, gonio.getBottom(), 1.0f);
        g.drawLine (gonio.getX(), c.y, gonio.getRight(), c.y, 1.0f);
        g.drawLine (c.x - R * 0.7f, c.y - R * 0.7f, c.x + R * 0.7f, c.y + R * 0.7f, 0.8f);
        g.drawLine (c.x + R * 0.7f, c.y - R * 0.7f, c.x - R * 0.7f, c.y + R * 0.7f, 0.8f);
        g.setFont (font (9.5f, true));
        g.setColour (colours::textFaint);
        g.drawText ("M", Rectangle<float> (c.x - 8, gonio.getY() + 3, 16, 10), Justification::centred);
        g.drawText ("L", Rectangle<float> (c.x - R * 0.72f - 14, c.y - R * 0.72f - 4, 12, 10), Justification::centred);
        g.drawText ("R", Rectangle<float> (c.x + R * 0.72f + 2, c.y - R * 0.72f - 4, 12, 10), Justification::centred);
        g.drawText ("S", Rectangle<float> (gonio.getRight() - 12, c.y - 12, 12, 10), Justification::centred);

        // phosphor persistence, coloured by the music
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int px = jmax (1, roundToInt (side * scale));
        if (phosphor.isNull() || phosphor.getWidth() != px)
            phosphor = Image (Image::ARGB, px, px, true);
        phosphor.multiplyAllAlphas (0.80f);
        {
            Graphics pg (phosphor);
            const float pr = (float) px * 0.5f;
            const float dot = jmax (1.5f, scale * 2.0f);
            pg.setColour (moodColour (0.0f, 1.0f, 0.45f).withAlpha (0.85f));
            for (int i = 0; i < stereoCount; ++i)
            {
                const auto& f = stereoTmp[(size_t) i];
                const float sx = (f.l - f.r) * 0.7071f, my = (f.l + f.r) * 0.7071f;
                pg.fillRect (pr + sx * pr * 0.72f, pr - my * pr * 0.72f, dot, dot);
            }
        }
        g.drawImage (phosphor, gonio);

        // correlation meter
        auto meter = Rectangle<float> (plot.getWidth() * 0.34f, 10.0f)
                         .withCentre ({ plot.getX() + plot.getWidth() * 0.76f, plot.getCentreY() + 10.0f });
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (meter, 5.0f);
        const float mid = meter.getCentreX();
        const float cx = meter.getX() + (jlimit (-1.0f, 1.0f, correlation) + 1.0f) * 0.5f * meter.getWidth();
        const auto barCol = correlation < 0.0f ? colours::bad : (correlation < 0.3f ? colours::warn : colours::good);
        g.setColour (barCol.withAlpha (0.85f));
        g.fillRoundedRectangle (Rectangle<float> (jmin (mid, cx), meter.getY(), std::abs (cx - mid), meter.getHeight()), 5.0f);
        drawGlow (g, { cx, meter.getCentreY() }, 14.0f, barCol.withAlpha (0.5f));
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ cx, meter.getCentreY() }));

        g.setFont (font (9.0f));
        g.setColour (colours::textFaint);
        for (float v : { -1.0f, 0.0f, 1.0f })
        {
            const float x = meter.getX() + (v + 1.0f) * 0.5f * meter.getWidth();
            g.drawText (v > 0 ? "+1" : String ((int) v), Rectangle<float> (x - 15, meter.getBottom() + 6, 30, 10), Justification::centred);
        }
        drawCaption (g, "Correlation", meter.withY (meter.getY() - 46).withHeight (12), Justification::centred, colours::textDim, 9.5f);
        g.setColour (barCol);
        g.setFont (font (24.0f, true));
        g.drawText ((correlation > 0 ? "+" : "") + String (correlation, 2), meter.withY (meter.getY() - 34).withHeight (26),
                    Justification::centred);
    }
}
