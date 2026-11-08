#!/usr/bin/env bash
# Builds "JLP DSP" (Mod Con) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target JlpDsp_VST3 JlpDsp_AU JlpDsp_Standalone

ART="build/JlpDsp_artefacts/Release"
mkdir -p dist
rm -rf "dist/JLP DSP.vst3" "dist/JLP DSP.component" "dist/JLP DSP.app"
cp -R "$ART/VST3/JLP DSP.vst3" dist/
cp -R "$ART/AU/JLP DSP.component" dist/
cp -R "$ART/Standalone/JLP DSP.app" dist/

echo
echo "Done:"
echo "  dist/JLP DSP.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/JLP DSP.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/JLP DSP.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
