; ---------------------------------------------------------------------------
;  MC6400 DSP - VST3-only Windows installer (Inno Setup 6)
;  Installs the 64-bit VST3 plugin (there is no standalone app).
;  Build with build_installer.bat (or: ISCC.exe installer\Mc6400DspVST3.iss)
;  after build_windows.bat has produced the Release VST3.
; ---------------------------------------------------------------------------

#define AppName      "MC6400 DSP"
#define AppVersion   "1.0.0"
#define AppPublisher "Mod Con DSP"
#define Art          "..\build\Mc6400Dsp_artefacts\Release"

[Setup]
AppId={{8C5F02B9-E47D-4A31-9D86-71B0F3E2C5A8}
AppName={#AppName} VST3
AppVersion={#AppVersion}
AppVerName={#AppName} VST3 {#AppVersion}
AppPublisher={#AppPublisher}
; only the uninstaller lives here; the plugin goes to the standard VST3 folder
DefaultDirName={autopf}\{#AppPublisher}\{#AppName} VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=MC6400 DSP VST3 Setup {#AppVersion}
SetupIconFile=Mc6400DspSetup.ico
UninstallDisplayIcon={commoncf64}\VST3\{#AppName}.vst3\Plugin.ico
UninstallDisplayName={#AppName} VST3 {#AppVersion}
WizardStyle=modern
; zip, not LZMA: the LZMA compressor crashed intermittently on the build machine
Compression=zip/9
SolidCompression=yes

[InstallDelete]
; Clear any older copy of the bundle (e.g. one copied in by hand) before installing the new one.
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Files]
Source: "{#Art}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Messages]
WelcomeLabel2=This will install the [name] plugin into%n%nC:\Program Files\Common Files\VST3%n%nClose your DAW before continuing.
FinishedLabel=Setup has installed the [name] plugin.%n%nOpen your DAW and rescan plugins - it appears under "Mod Con DSP".
