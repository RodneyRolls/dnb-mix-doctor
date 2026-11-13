#!/usr/bin/env bash
# Builds "Fix Dr" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target FixDr_VST3 FixDr_AU FixDr_Standalone

ART="build/FixDr_artefacts/Release"
mkdir -p dist
rm -rf "dist/Fix Dr.vst3" "dist/Fix Dr.component" "dist/Fix Dr.app"
cp -R "$ART/VST3/Fix Dr.vst3" dist/
cp -R "$ART/AU/Fix Dr.component" dist/
cp -R "$ART/Standalone/Fix Dr.app" dist/

echo
echo "Done:"
echo "  dist/Fix Dr.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/Fix Dr.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/Fix Dr.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
