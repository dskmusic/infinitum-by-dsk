; Infinitum by DSK - Inno Setup installer. Build: iscc /DAppVersion=1.0.0 /DArtDir=<artefacts\Release> installer\infinitum.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ArtDir
  #define ArtDir "..\build\Infinitum_artefacts\Release"
#endif

[Setup]
AppId={{A5B06B69-D1FB-4B0A-BCC2-0FF6BD966FC3}
AppName=Infinitum by DSK
AppVersion={#AppVersion}
AppPublisher=DSK Music
AppPublisherURL=https://dskmusic.com
DefaultDirName={autopf}\DSK Music\Infinitum
DefaultGroupName=DSK Music
OutputBaseFilename=Infinitum-by-DSK-win-x64-setup
OutputDir=..\dist
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
LicenseFile=..\LICENSE
DisableProgramGroupPage=yes
SetupIconFile=..\branding\infinitum.ico
UninstallDisplayIcon={app}\Infinitum by DSK.exe

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#ArtDir}\VST3\Infinitum by DSK.vst3\*"; DestDir: "{commoncf64}\VST3\Infinitum by DSK.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtDir}\Standalone\Infinitum by DSK.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\Infinitum by DSK"; Filename: "{app}\Infinitum by DSK.exe"; Components: standalone
