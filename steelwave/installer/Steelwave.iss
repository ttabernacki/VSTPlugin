; Steelwave Windows installer (Inno Setup 6). Built by CI:
;   ISCC /DAppVersion=1.0.0 /DSrc=<folder holding Steelwave.vst3 and Steelwave.exe> Steelwave.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Src
  #define Src "..\..\dist\steelwave"
#endif

[Setup]
AppId={{8B1F3C52-6C0E-4C1B-9B1E-5A7E57EE1A01}
AppName=Steelwave
AppVersion={#AppVersion}
AppPublisher=Steelwave
DefaultDirName={autopf}\Steelwave
DefaultGroupName=Steelwave
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=Steelwave-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Steelwave

[Types]
Name: "full"; Description: "VST3 plug-in and standalone application"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (for your DAW)"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Src}\Steelwave.vst3\*"; DestDir: "{commoncf64}\VST3\Steelwave.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#Src}\Steelwave.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\Steelwave"; Filename: "{app}\Steelwave.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Steelwave.vst3"
