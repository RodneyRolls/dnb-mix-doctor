#!/usr/bin/env bash
# Builds "MC6400 DSP" (Mod Con DSP) for macOS: universal VST3 + AU.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target Mc6400Dsp_VST3 Mc6400Dsp_AU

ART="build/Mc6400Dsp_artefacts/Release"
mkdir -p dist
rm -rf "dist/MC6400 DSP.vst3" "dist/MC6400 DSP.component"
cp -R "$ART/VST3/MC6400 DSP.vst3" dist/
cp -R "$ART/AU/MC6400 DSP.component" dist/

echo
echo "Done:"
echo "  dist/MC6400 DSP.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/MC6400 DSP.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "Sign and notarise before distributing to other Macs."
