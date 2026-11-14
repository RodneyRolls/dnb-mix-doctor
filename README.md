# Fix Dr — Mod Con DSP

A one-stop mix repair processor for drum & bass (160–178 BPM). Put it on the master or a drum/bass bus:
it fixes phasey sub, kick/bass clashes, weak transients, mud, reese resonances, harsh highs, dull top end,
uncontrolled width and low loudness, and finishes with a clipper + true-peak limiter loudness maximizer.
Fix Dr is the successor to DnB Mix Doctor V1/V2, The Mix Fixer, Dr Rolls Mix Fixer, Rolls Mix Fixer, Mod Con Fixer, JLP DSP, JLP Mix Fixer, JLP Mix Doctor and Dr Mix.

VST3 (Windows + macOS), AU (macOS) and Standalone. JUCE 9.0.5, C++17, CMake.

Plugin codes: manufacturer **Mod Con DSP** / `MCDS`, plugin `FxDr`, bundle `com.modcondsp.fixdr`
(its own identity, so it installs next to the earlier versions). The standalone `Fix Dr.exe` uses a
VU-meter app icon (`Resources/icon.png`, drawn by `DnBMixDoctorSnapshot --icon <png>`).

## Highlights
- **Modern UI** with dark graphite cards, a cyan → violet accent and values shown inside the knobs. The wordmark
  and the two analogue VU meters use the original V1 design. The VU faces are lit white and glow red as the
  level climbs past 0 VU (0 VU = the LOUDNESS target).
- **ADVANCED panel (16-band EQ):** the ADVANCED button in the header swaps the knob section for a 16-band graphic
  EQ (25 Hz to 18 kHz, ±12 dB per band) with a live response curve. EQ MIX scales the whole curve (0 % = flat,
  100 % = as drawn), EQ ON switches it in and out, FLAT resets every band. The EQ sits after GLUE and before the
  clipper/limiter, so auto-gain keeps the loudness on target. Loading a preset starts from a flat EQ.
- **Accurate, loudness-coloured waveform:** 512 slices per second, drawn per screen pixel like a DAW: the true
  min/max peak of the audio under each pixel (lighter) with its RMS level inside (solid). Colour follows the level,
  round the colour wheel from yellow (quiet) through green, cyan, blue and magenta to red (loudest). The spectrum
  and goniometer still follow the track's overall colour "mood".
- **Separate CLIPPER and LOUDNESS knobs:**
  - CLIPPER sets how hard a 4× oversampled soft clipper shaves peaks before the limiter (0 % = off).
  - LOUDNESS sets how loud the drops end up (−14…−3 LUFS).
  - AUTO GAIN sets how much of the automatic gain is applied: 100 % reaches the LOUDNESS target, 0 % applies none
    (the clipper and limiter work on your mix as it comes in).
- **MULTIBAND maximizer mode** (button under LOUDNESS): instead of turning the whole mix down on every peak, a
  4-band look-ahead stage works out which part of the spectrum caused the peak and turns down mostly that band, with
  its own transient-aware release per band. Kicks no longer duck the hats, and bass is released slowly so it is not
  chopped. Off = CLASSIC broadband limiter (the original sound).
- **Stable loudness (no volume drift):** auto-gain learns the loudness of the loud sections and then holds it.
  The level does not sink as more of the track plays, does not jump in breakdowns, and does not change when you
  press play. Use *Menu → Reset meters + re-learn loudness* (or click the INTEGRATED readout) when you switch to
  a different song.

## Build

You need CMake 3.22+ and Git. The first configure downloads JUCE 9.0.5 automatically
(or put a JUCE checkout in `./JUCE`, or pass `-DJUCE_DIR=<path>`).

**Windows** (Visual Studio 2022 or 2026 with "Desktop development with C++"):

    build_windows.bat

or by hand:

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target FixDr_VST3 FixDr_Standalone

Copy `build\FixDr_artefacts\Release\VST3\Fix Dr.vst3` to `C:\Program Files\Common Files\VST3\`.

**Windows installer** (needs [Inno Setup 6](https://jrsoftware.org/isinfo.php): `winget install JRSoftware.InnoSetup`):

    build_installer.bat

This builds the plugin, then writes two installers to `dist`:
- `Fix Dr Setup 1.0.0.exe` (from `installer\FixDr.iss`): the VST3 plugin, plus optionally the standalone app with
  Start-menu and desktop shortcuts.
- `Fix Dr VST3 Setup 1.0.0.exe` (from `installer\FixDrVST3.iss`): the VST3 plugin only, nothing else.

Both put the VST3 in `C:\Program Files\Common Files\VST3\`. Each appears in *Settings > Apps*, where it can be
uninstalled cleanly.
The installer is unsigned, so Windows SmartScreen may warn on first run ("More info > Run anyway").

**macOS** (Xcode 14+; builds a universal arm64 + x86_64 binary):

    ./build_macos.sh

or by hand:

    cmake -B build -G Xcode
    cmake --build build --config Release --target FixDr_VST3 FixDr_AU FixDr_Standalone

Copy the `.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and the `.component` to `~/Library/Audio/Plug-Ins/Components/`.
For distribution, sign and notarise the bundles (`codesign` + `notarytool`).

**CI:** `.github/workflows/build.yml` builds Windows and macOS on every push and uploads the plugins as artifacts.

### Tests and UI snapshots

    cmake --build build --config Release --target DnBMixDoctorTests
    build/DnBMixDoctorTests_artefacts/Release/DnBMixDoctorTests

Checks, at 44.1/48/96/192 kHz: dry/wet latency alignment, level stability across intro → drop → breakdown → drop, AUTO GAIN at 0/50/100 %, wet impulse alignment in both oversampling modes,
true-peak ceiling on a hot mix, loudness landing on target (including -5 and -4 LUFS with a -0.3 dBTP ceiling), sub-mono side removal, MULTIBAND mode (ceiling, hats not ducked by kicks, loudness, switching mid-stream), mono layout, and CPU use.

    cmake --build build --config Release --target DnBMixDoctorSnapshot
    build/DnBMixDoctorSnapshot_artefacts/Release/DnBMixDoctorSnapshot <out-folder> [scale]

Renders the editor (WAVE / SPECTRUM / STEREO) to PNG with synthetic DnB audio flowing, plus a WAVE shot after a bright mix, the ADVANCED panel open, and the editor after a close + reopen.

## Signal flow

    INPUT → [LR4 @120 Hz: SUB TIGHT + KICK/BASS on the low band] → [LR4 @SUB MONO: mono lows, WIDTH above]
          → PUNCH → SNAP → MUD / REESE TAME / HARSH dynamic EQ → AIR → GLUE → ADVANCED 16-band EQ
          → (4x oversampled when enabled: SATURATE → loudness auto-gain → soft clipper → [MULTIBAND stage] → limiter → catch limiter)
          → latency-aligned MIX → OUTPUT → click-free bypass

| Control | What it does |
|---|---|
| INPUT | Input gain, ±24 dB |
| SUB MONO | Linkwitz-Riley split; everything below 40–250 Hz is folded to mono |
| SUB TIGHT | Shortens sub sustain (fast-vs-slow release envelope) plus gentle low-band compression |
| KICK/BASS | Detects kicks in the low band; after the kick's first ~8 ms, ducks the low band (up to −9 dB) and dips the 220 Hz bass body so the kick isn't smeared by bass sustain |
| PUNCH | Broadband transient shaper (attack boost up to +10 dB) |
| SNAP | Transient-triggered bell boost centred on 900 Hz, covering ~150 Hz–2 kHz |
| MUD | Dynamic cut at 320 Hz when 200–500 Hz builds up against the broadband level |
| REESE TAME | Five dynamic bells (350 Hz–2.8 kHz) that cut any band poking above its spectral neighbours |
| HARSH | Dynamic cuts at 3.2 and 6 kHz with fast attack for bursts |
| AIR | 10 kHz high shelf (up to +6 dB) plus a gentle harmonic exciter |
| GLUE / ATTACK / RELEASE | Stereo-linked soft-knee bus compressor; AUTO release adapts 60–900 ms to how hard it's working |
| WIDTH | Side gain above the SUB MONO frequency, 0–200 % (lows stay mono) |
| SATURATE | Asymmetric tanh tape/valve stage (2nd + 3rd harmonics), unity small-signal gain |
| CLIPPER | Soft clipper (tanh knee, 4x oversampled) in front of the limiter. 0 % = off; higher settings move its threshold from 6 dB over the ceiling down to 0.1 dB over it, so it takes more of the peaks off and the limiter works less. Louder and punchier, but grittier at high settings |
| LOUDNESS | How loud the loud sections (drops) come out, −14…−3 LUFS short-term (default −7). Auto-gain learns the input loudness of the loud sections and holds it, plus a bounded make-up for what the clipper and limiter take off. It does not follow the running average, so the level stays put across the song |
| AUTO GAIN | How much of the learned auto-gain is applied, 0–100 %. At 100 % the drops land on LOUDNESS; lower settings scale the gain down in dB (50 % = half the dB). The make-up for limiter loss only adapts at 100 % |
| MULTIBAND | Maximizer mode. Off = CLASSIC: one broadband look-ahead limiter. On = MULTIBAND: a phase-coherent 4-band split (120 Hz / 1 kHz / 6 kHz); every overshoot is shared between the bands in proportion to how much each pushes the waveform towards the peak, each band capped at −12 dB, each with its own look-ahead gain and smart release (fast on transients, slow on sustained sound, slower for lows). The broadband true-peak limiter still runs after it, so the ceiling is guaranteed. Switching fades over 50 ms; latency is the same in both modes |
| CEILING | True-peak ceiling −3…0 dBTP |
| FIX AMOUNT | Macro that scales every corrective module (everything from SUB MONO to SATURATE) 0–100 % |
| EQ (ADVANCED) | 16 bells at 25, 40, 63, 100, 160, 250, 400, 630 Hz, 1, 1.6, 2.5, 4, 6.3, 10, 14, 18 kHz (Q 1.8, ±12 dB). EQ MIX multiplies every band gain (no dry/wet phase smearing); EQ ON fades the curve out |
| MIX | Dry/wet with the dry path delayed to match the latency |
| OUTPUT | Output gain, ±24 dB |

### DSP notes
- **Crossovers:** 4th-order Linkwitz-Riley (TPT form), recombined by summing — magnitude-flat, phase-coherent.
- **Dynamic EQ:** Cytomic/Simper trapezoidal SVF bells re-tuned every 32 samples (modulation-safe, no zipper).
- **Clipper:** soft clipper with a tanh knee (linear below 60 % of its threshold) in front of the limiter. It runs at
  4x when oversampling is on. The CLIPPER knob places its threshold between 6 dB and 0.1 dB over the ceiling.
- **Limiter:** look-ahead (1.5 ms) sliding-minimum gain computer + moving-average attack, so it cannot overshoot
  and doesn't distort. The release is program-dependent: 25 ms back to the sustained gain level, then 250 ms to
  unity. True-peak detection uses a 4x polyphase windowed-sinc interpolator (BS.1770-style).
  In 4x mode the limiter runs on the oversampled signal, then a short true-peak catch limiter after decimation
  removes the small overs the downsampling filter puts back.
- **Latency** is reported to the host and is **identical in both oversampling modes** (the 1x path is padded),
  so toggling oversampling never changes plugin delay compensation. About 3.7 ms at 48 kHz.
- **Bypass** (power button / host bypass) cross-fades over 30 ms to the latency-aligned dry signal.
- **Smoothing:** gains use 50 ms ramps, corrective amounts a 30 ms one-pole at control rate, all automatable
  via `AudioProcessorValueTreeState`.
- **Loudness metering:** ITU-R BS.1770-4 K-weighting, momentary / short-term / gated integrated (histogram, no
  allocation). Integrated loudness and true-peak hold reset when transport starts or when you click the readout.
- **Real-time safety:** no allocation or locks on the audio thread. Visual data (waveform columns, FFT samples,
  stereo frames) goes to the UI through single-producer/single-consumer `AbstractFifo` queues with fixed storage.

### Auto-fix
The lightning button listens for 8 bars at the host tempo (174 BPM if unknown; silence doesn't count), measuring
octave-band balance against a built-in DnB reference curve, L/R correlation below 120 Hz, side/mid ratio,
kick/bass overlap (low-band energy 40–150 ms after each kick vs during it), transient density, crest factor and
integrated loudness. It then sweeps the knobs to suggested values; you tweak from there. Press it again while
it's listening to cancel. The reference curve is an engineering estimate, not measured from commercial masters.

### Presets
Factory: Default, Liquid Polish, Neuro Smash, Jump Up Bounce, Rollers Sub Fix, Jungle Breaks, Club Loud,
Gentle Master Assist. User presets save as `.mcdpreset` files in
`%APPDATA%\Mod Con DSP\Fix Dr\Presets` (Windows) or
`~/Library/Application Support/Mod Con DSP/Fix Dr/Presets` (macOS). The star marks favourites; A/B swaps
two complete settings without touching the power switch or oversampling mode.

## Source layout

    Source/Params.h                 parameter IDs, ranges, layout
    Source/PluginProcessor.*        host glue, metering, visual FIFOs, A/B, state
    Source/PluginEditor.*           1300x700 editor (scaled, aspect-locked), 22 knobs, top bar, presets, auto-fix sweep
    Source/PresetManager.*          factory/user presets, favourites
    Source/dsp/MixEngine.*          the full signal chain
    Source/dsp/TruePeakLimiter.h    look-ahead true-peak limiter
    Source/dsp/MultibandMaximizer.h 4-band smart-release maximizer stage (MULTIBAND mode)
    Source/dsp/LoudnessMeter.h      BS.1770 / R128 meter
    Source/dsp/DspUtils.h           SVF, envelopes, delay, DC blocker, true-peak interpolator
    Source/analysis/VisualFifo.h    lock-free FIFOs + waveform column builder
    Source/analysis/AutoFix.*       8-bar analyser and suggestion mapping
    Source/ui/Theme.*               colours, fonts, card/glow helpers, LookAndFeel (knobs, combo, menus)
    Source/ui/Widgets.h             knob (fine drag), icon buttons with status LEDs, numeric readouts
    Source/ui/VUMeter.h             analogue VU meter (white face, glows red when hot), damped needle
    Source/ui/Visualizer.*          WAVE / SPECTRUM / STEREO display with music-reactive colour
    tests/EngineTests.cpp           offline DSP checks
    tests/UISnapshot.cpp            editor-to-PNG renderer

## Licensing
JUCE is AGPLv3 or commercial. Distributing closed-source builds needs a JUCE licence; check current JUCE
and Steinberg VST3 terms before you ship.
