; ---------------------------------------------------------------------------
;  Rolls Mix Fixer - Windows installer (Inno Setup 6)
;  Build with build_installer.bat (or: ISCC.exe installer\RollsMixFixer.iss)
;  after build_windows.bat has produced the Release VST3 + Standalone.
; ---------------------------------------------------------------------------

#define AppName      "Rolls Mix Fixer"
#define AppVersion   "1.0.0"
#define AppPublisher "Mod Con DSP"
#define Art          "..\build\RollsMixFixer_artefacts\Release"

[Setup]
AppId={{6B1E4C1A-52F3-4E7B-9C0E-1D2A9F3B7A11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Rolls Mix Fixer Setup {#AppVersion}
SetupIconFile=RollsMixFixer.ico
UninstallDisplayIcon={app}\{#AppName}.exe
UninstallDisplayName={#AppName} {#AppVersion}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes

[Types]
Name: "full";   Description: "VST3 plugin + standalone app"
Name: "vst3";   Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin  (C:\Program Files\Common Files\VST3)"; Types: full vst3 custom
Name: "standalone"; Description: "Standalone app";                                   Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: standalone

[InstallDelete]
; Clear any older copy of the bundle (e.g. one copied in by hand) before installing the new one.
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3

[Files]
Source: "{#Art}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Art}\Standalone\{#AppName}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\{#AppName}.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";     Filename: "{app}\{#AppName}.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Launch {#AppName}"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Messages]
FinishedLabel=Setup has installed [name] on your computer.%n%nOpen your DAW and rescan plugins - it appears under "Mod Con DSP".