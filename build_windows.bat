@echo off
REM ---------------------------------------------------------------------------
REM  Builds "MC6400 DSP" (Mod Con DSP) for Windows: 64-bit VST3
REM  Needs: Visual Studio 2022 or 2026 ("Desktop development with C++"),
REM         CMake 3.22+ and Git on the PATH. First run downloads JUCE 9.0.5.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

cmake -B build -A x64 || goto :fail
cmake --build build --config Release --target Mc6400Dsp_VST3 || goto :fail

set "ART=build\Mc6400Dsp_artefacts\Release"
if not exist dist mkdir dist
xcopy /E /I /Y "%ART%\VST3\MC6400 DSP.vst3" "dist\MC6400 DSP.vst3" >nul

echo.
echo  Done.
echo    dist\MC6400 DSP.vst3\  ^<-- copy this folder to  C:\Program Files\Common Files\VST3\
echo  Then rescan plugins in your DAW.
exit /b 0

:fail
echo.
echo  Build failed - see messages above.
exit /b 1
