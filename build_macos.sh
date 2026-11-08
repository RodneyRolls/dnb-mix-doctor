#!/usr/bin/env bash
# Builds "JLP Mix Doctor" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target JlpMixDoctor_VST3 JlpMixDoctor_AU JlpMixDoctor_Standalone

ART="build/JlpMixDoctor_artefacts/Release"
mkdir -p dist
rm -rf "dist/JLP Mix Doctor.vst3" "dist/JLP Mix Doctor.component" "dist/JLP Mix Doctor.app"
cp -R "$ART/VST3/JLP Mix Doctor.vst3" dist/
cp -R "$ART/AU/JLP Mix Doctor.component" dist/
cp -R "$ART/Standalone/JLP Mix Doctor.app" dist/

echo
echo "Done:"
echo "  dist/JLP Mix Doctor.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/JLP Mix Doctor.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/JLP Mix Doctor.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
