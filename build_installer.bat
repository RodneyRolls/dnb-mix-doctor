@echo off
REM ---------------------------------------------------------------------------
REM  Builds the Windows installers:  dist\Fix Dr Setup 1.0.0.exe (VST3 + app)
REM                                 dist\Fix Dr VST3 Setup 1.0.0.exe (VST3 only)
REM  Runs build_windows.bat first, then Inno Setup 6
REM  (install it with:  winget install JRSoftware.InnoSetup).
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

call build_windows.bat || goto :fail

set "ISCC="
for %%P in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do (
    if exist %%P set "ISCC=%%~P"
)
if not defined ISCC (
    echo  Inno Setup 6 not found - run:  winget install JRSoftware.InnoSetup
    goto :fail
)

"%ISCC%" /Q "installer\FixDr.iss" || goto :fail
"%ISCC%" /Q "installer\FixDrVST3.iss" || goto :fail
echo.
echo  Installers ready:
echo    dist\Fix Dr Setup 1.0.0.exe       ^<-- VST3 + standalone app
echo    dist\Fix Dr VST3 Setup 1.0.0.exe  ^<-- VST3 plugin only
exit /b 0

:fail
echo.
echo  Installer build failed - see messages above.
exit /b 1
