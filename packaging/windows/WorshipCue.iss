#ifndef Version
  #error Version is required
#endif
#ifndef Stage
  #error Stage is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef ArtifactName
  #error ArtifactName is required
#endif
#ifndef AppIcon
  #error AppIcon is required
#endif
[Setup]
AppId={{863A109D-5E0A-4DA4-ADE4-4100DA9C0C54}
AppName=WorshipCue
AppVersion={#Version}
AppPublisher=WorshipCue
DefaultDirName={localappdata}\Programs\WorshipCue
DefaultGroupName=WorshipCue
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#ArtifactName}
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\bin\WorshipCue.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes
#ifdef Signed
SignTool=worshipcue
SignedUninstaller=yes
#else
AppVerName=WorshipCue {#Version} — Unsigned Development
#endif
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{autoprograms}\WorshipCue"; Filename: "{app}\bin\WorshipCue.exe"
Name: "{autodesktop}\WorshipCue"; Filename: "{app}\bin\WorshipCue.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\bin\WorshipCue.exe"; Description: "Launch WorshipCue"; Flags: nowait postinstall skipifsilent
; Inno Setup tracks installed runtime files for upgrades/uninstall. No [UninstallDelete]
; rule removes AppData, library.sqlite3, media, preferences or DPAPI license state.
