#!/usr/bin/env bash
# Builds "Dr Rolls Mix Fixer" (Mod Con DSP) for macOS: universal VST3 + AU + Standalone.
# Needs: Xcode 14+ (with command line tools), CMake 3.22+ and Git. First run downloads JUCE 9.0.5.
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target DrRollsMixFixer_VST3 DrRollsMixFixer_AU DrRollsMixFixer_Standalone

ART="build/DrRollsMixFixer_artefacts/Release"
mkdir -p dist
rm -rf "dist/Dr Rolls Mix Fixer.vst3" "dist/Dr Rolls Mix Fixer.component" "dist/Dr Rolls Mix Fixer.app"
cp -R "$ART/VST3/Dr Rolls Mix Fixer.vst3" dist/
cp -R "$ART/AU/Dr Rolls Mix Fixer.component" dist/
cp -R "$ART/Standalone/Dr Rolls Mix Fixer.app" dist/

echo
echo "Done:"
echo "  dist/Dr Rolls Mix Fixer.vst3       -> ~/Library/Audio/Plug-Ins/VST3/"
echo "  dist/Dr Rolls Mix Fixer.component  -> ~/Library/Audio/Plug-Ins/Components/"
echo "  dist/Dr Rolls Mix Fixer.app        (standalone)"
echo "Sign and notarise before distributing to other Macs."
