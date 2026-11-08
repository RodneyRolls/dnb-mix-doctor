@echo off
REM ---------------------------------------------------------------------------
REM  Builds "JLP Mix Fixer" (Mod Con DSP) for Windows: 64-bit VST3 + Standalone
REM  Needs: Visual Studio 2022 or 2026 ("Desktop development with C++"),
REM         CMake 3.22+ and Git on the PATH. First run downloads JUCE 9.0.5.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

cmake -B build -A x64 || goto :fail
cmake --build build --config Release --target JlpMixFixer_VST3 JlpMixFixer_Standalone || goto :fail

set "ART=build\JlpMixFixer_artefacts\Release"
if not exist dist mkdir dist
xcopy /E /I /Y "%ART%\VST3\JLP Mix Fixer.vst3" "dist\JLP Mix Fixer.vst3" >nul
copy /Y "%ART%\Standalone\JLP Mix Fixer.exe" "dist\JLP Mix Fixer.exe" >nul

echo.
echo  Done.
echo    dist\JLP Mix Fixer.vst3\  ^<-- copy this folder to  C:\Program Files\Common Files\VST3\
echo    dist\JLP Mix Fixer.exe    ^<-- standalone version
echo  Then rescan plugins in your DAW.
exit /b 0

:fail
echo.
echo  Build failed - see messages above.
exit /b 1
