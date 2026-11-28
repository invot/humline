; Installs the standalone app into Program Files and the VST3 bundle into the
; shared VST3 folder, where every DAW finds it.

#define AppName "Humline"
#define AppVersion "1.0.0"
#define AppPublisher "Codebuff"
#define AppURL "https://github.com/invot/humline"
#define AppExeName "Humline.exe"

[Setup]
AppId={{FBEADC69-F57B-4DEE-B0C9-26BC205694B7}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
LicenseFile=..\LICENSE
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Humline-Setup-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; Both binaries are statically linked, so there are no runtime DLLs to ship.
Source: "..\build\Humline_artefacts\Release\Standalone\Humline.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Humline_artefacts\Release\VST3\Humline.vst3\*"; DestDir: "{commoncf64}\VST3\Humline.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
