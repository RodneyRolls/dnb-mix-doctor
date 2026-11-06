# DnB Mix Doctor V2 — Mod Con DSP

A one-stop mix repair processor for drum & bass (160–178 BPM). Put it on the master or a drum/bass bus:
it fixes phasey sub, kick/bass clashes, weak transients, mud, reese resonances, harsh highs, dull top end,
uncontrolled width and low loudness, and finishes with a clipper + true-peak limiter loudness maximizer.

VST3 (Windows + macOS), AU (macOS) and Standalone. JUCE 9.0.5, C++17, CMake.

Plugin codes: manufacturer **Mod Con DSP** / `MCDS`, plugin `DnM2`.

## What's new in V2
- **Modern UI:** dark graphite surfaces, rounded cards, a cyan → violet accent, values shown inside the knobs,
  arc level gauges and segmented display tabs. Same 1300×700 layout, resizable with a locked aspect ratio.
- **Music-reactive waveform:** each waveform column is tinted by its own spectral balance and brightened by its
  level, around a "mood" hue that follows the track. Sub-heavy rollers glow violet, reese-heavy neuro and punchy
  drums push toward magenta/pink, and bright liquid sits in cyan/teal. The spectrum, goniometer, level gauges and
  the display's ambient light follow the same colour.
- **Louder maximizer:** LOUDNESS now goes up to −3 LUFS (V1: −4) and defaults to −7 LUFS (V1: −9).
  - A 4× oversampled soft clipper shaves peaks before the limiter. It moves closer to the ceiling the louder
    the target is.
  - The limiter's release is program-dependent: 25 ms on peaks, 250 ms when limiting is sustained.
  - Auto-gain can add up to +30 dB, and 4× oversampling is on by default.
  - Factory presets are 1.5–2 LU louder.
- **Separate plugin:** V2 has its own plugin code (`DnM2`) and bundle ID (`com.modcondsp.dnbmixdoctorv2`), so
  it installs next to V1.

## Build

You need CMake 3.22+ and Git. The first configure downloads JUCE 9.0.5 automatically
(or put a JUCE checkout in `./JUCE`, or pass `-DJUCE_DIR=<path>`).

**Windows** (Visual Studio 2022 or 2026 with "Desktop development with C++"):

    build_windows.bat

or by hand:

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target DnBMixDoctorV2_VST3 DnBMixDoctorV2_Standalone

Copy `build\DnBMixDoctor_artefacts\Release\VST3\DnB Mix Doctor.vst3` to `C:\Program Files\Common Files\VST3\`.

**macOS** (Xcode 14+; builds a universal arm64 + x86_64 binary):

    ./build_macos.sh

or by hand:

    cmake -B build -G Xcode
    cmake --build build --config Release --target DnBMixDoctorV2_VST3 DnBMixDoctorV2_AU DnBMixDoctorV2_Standalone

Copy the `.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and the `.component` to `~/Library/Audio/Plug-Ins/Components/`.
For distribution, sign and notarise the bundles (`codesign` + `notarytool`).

**CI:** `.github/workflows/build.yml` builds Windows and macOS on every push and uploads the plugins as artifacts.

### Tests and UI snapshots

    cmake --build build --config Release --target DnBMixDoctorTests
    build/DnBMixDoctorTests_artefacts/Release/DnBMixDoctorTests

Checks, at 44.1/48/96/192 kHz: dry/wet latency alignment, wet impulse alignment in both oversampling modes,
true-peak ceiling on a hot mix, loudness landing on target (including -5 and -4 LUFS with a -0.3 dBTP ceiling), sub-mono side removal, mono layout, and CPU use.

    cmake --build build --config Release --target DnBMixDoctorSnapshot
    build/DnBMixDoctorSnapshot_artefacts/Release/DnBMixDoctorSnapshot <out-folder> [scale]

Renders the editor (WAVE / SPECTRUM / STEREO) to PNG with synthetic DnB audio flowing, plus a WAVE shot after a bright mix to show the colour change.

## Signal flow

    INPUT → [LR4 @120 Hz: SUB TIGHT + KICK/BASS on the low band] → [LR4 @SUB MONO: mono lows, WIDTH above]
          → PUNCH → SNAP → MUD / REESE TAME / HARSH dynamic EQ → AIR → GLUE
          → (4x oversampled when enabled: SATURATE → loudness auto-gain → soft clipper → limiter → catch limiter)
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
| LOUDNESS | Target loudness −14…−3 LUFS (default −7). Gain into the clipper + limiter = target − integrated input loudness, plus a slow closed-loop trim from the measured output loudness. The louder the target, the closer the soft clipper sits to the ceiling |
| CEILING | True-peak ceiling −3…0 dBTP |
| FIX AMOUNT | Macro that scales every corrective module (everything from SUB MONO to SATURATE) 0–100 % |
| MIX | Dry/wet with the dry path delayed to match the latency |
| OUTPUT | Output gain, ±24 dB |

### DSP notes
- **Crossovers:** 4th-order Linkwitz-Riley (TPT form), recombined by summing — magnitude-flat, phase-coherent.
- **Dynamic EQ:** Cytomic/Simper trapezoidal SVF bells re-tuned every 32 samples (modulation-safe, no zipper).
- **Clipper:** soft clipper with a tanh knee (linear below 60 % of its threshold) in front of the limiter. It runs
  at 4x when oversampling is on. Its threshold sits 3 dB over the ceiling at −12 LUFS and 0.3 dB over it from
  −5 LUFS up.
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
`%APPDATA%\Mod Con DSP\DnB Mix Doctor V2\Presets` (Windows) or
`~/Library/Application Support/Mod Con DSP/DnB Mix Doctor V2/Presets` (macOS). The star marks favourites; A/B swaps
two complete settings without touching the power switch or oversampling mode.

## Source layout

    Source/Params.h                 parameter IDs, ranges, layout
    Source/PluginProcessor.*        host glue, metering, visual FIFOs, A/B, state
    Source/PluginEditor.*           1300x700 editor (scaled, aspect-locked), top bar, presets, auto-fix sweep
    Source/PresetManager.*          factory/user presets, favourites
    Source/dsp/MixEngine.*          the full signal chain
    Source/dsp/TruePeakLimiter.h    look-ahead true-peak limiter
    Source/dsp/LoudnessMeter.h      BS.1770 / R128 meter
    Source/dsp/DspUtils.h           SVF, envelopes, delay, DC blocker, true-peak interpolator
    Source/analysis/VisualFifo.h    lock-free FIFOs + waveform column builder
    Source/analysis/AutoFix.*       8-bar analyser and suggestion mapping
    Source/ui/Theme.*               colours, fonts, card/glow helpers, LookAndFeel (knobs, combo, menus)
    Source/ui/Widgets.h             knob (fine drag), icon buttons with status LEDs, numeric readouts
    Source/ui/LevelGauge.h          arc level gauge relative to the loudness target
    Source/ui/Visualizer.*          WAVE / SPECTRUM / STEREO display with music-reactive colour
    tests/EngineTests.cpp           offline DSP checks
    tests/UISnapshot.cpp            editor-to-PNG renderer

## Licensing
JUCE is AGPLv3 or commercial. Distributing closed-source builds needs a JUCE licence; check current JUCE
and Steinberg VST3 terms before you ship.
