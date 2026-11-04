# DnB Mix Doctor

A mastering-bus VST3 for **drum & bass** that listens to your mix, tells you what's wrong, and fixes it.
Put it last on the master (or on a drum/bass group). Windows 64-bit, any VST3 host.

## What it detects and corrects

| Problem | How it's detected | Correction |
|---|---|---|
| Clipping / inter-sample peaks | 32-tap sinc true-peak estimate | Look-ahead limiter, ceiling -0.3 dBTP default |
| DC offset | long-term mean | DC blocker |
| Sub-sonic rumble (<22 Hz) | 4th-order LP energy vs sub band | 25 Hz 4th-order high-pass |
| Low end not mono (sub/bass side signal) | side/mid of the Linkwitz-Riley low band | Collapse the low band to mono below 60-200 Hz (default 120) |
| Tonal imbalance | 8 octave-ish bands, per-octave level vs a DnB target curve | Slow, limited (+/-3 dB) auto-EQ |
| Harsh 2.5-7 kHz bursts (snares, hats, reese highs) | fast vs slow band envelope | Dynamic bell cut at ~4.2 kHz, up to 4 dB |
| Out-of-phase / over-wide mix | L/R correlation | **advice only** (never auto-flipped) |
| Over-squashed mix | crest factor | **advice only** |

The analysis always listens to the *input*, so the correctors can't chase their own output.
Reported latency is ~1.8 ms (look-ahead) and is passed to the host for compensation.

## Targets
Styles *Neurofunk / Liquid / Dancefloor* are **engineering estimates**, not measured data.
For ground truth: play a reference track you love, wait a few seconds, press **Capture target**,
and the plugin matches your mix to *that* balance (stored with the project).

## Getting the DLL
A VST3 on Windows *is* a DLL with the extension `.vst3`. (VST2 `.dll`s can no longer be built: Steinberg ended VST2 licensing.)

1. **On a Windows PC:** install Visual Studio 2022 (C++ workload), CMake and Git, then double-click `build_windows.bat`.
   Output: `dist\DnB Mix Doctor.vst3\` (copy to `C:\Program Files\Common Files\VST3\`) and `dist\DnB Mix Doctor.dll`.
2. **No Windows PC / no tools:** push this folder to a GitHub repo; the included workflow builds it on a Windows runner
   (Actions tab -> *Build Windows VST3 (DLL)* -> download the `DnB-Mix-Doctor-Windows` artifact).

## Tests (any OS with a C++17 compiler)
    g++ -O2 -std=c++17 -I Source tests/test_dsp.cpp -o test_dsp && ./test_dsp

## Layout
    Source/MixDoctorDSP.h     all DSP + analysis (no JUCE dependency, unit-tested)
    Source/PluginProcessor.*  parameters, state, host glue
    Source/PluginEditor.*     live diagnostics UI
    tests/test_dsp.cpp        synthetic DnB-style signal tests

## Licensing
JUCE is AGPLv3 or commercial. Distributing this plugin as-is means releasing its source under the AGPL;
selling closed-source builds needs a JUCE licence. Check current JUCE and VST3 terms before you ship.
