#!/usr/bin/env bash
# Builds "DnB Mix Doctor" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target DnBMixDoctor_VST3 DnBMixDoctor_AU DnBMixDoctor_Standalone

ART="build/DnBMixDoctor_artefacts/Release"
mkdir -p dist
rm -rf "dist/DnB Mix Doctor.vst3" "dist/DnB Mix Doctor.component" "dist/DnB Mix Doctor.app"
cp -R "$ART/VST3/DnB Mix Doctor.vst3" dist/
cp -R "$ART/AU/DnB Mix Doctor.component" dist/
cp -R "$ART/Standalone/DnB Mix Doctor.app" dist/

echo
echo "Done:"
echo "  dist/DnB Mix Doctor.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/DnB Mix Doctor.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/DnB Mix Doctor.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
