; Pump Organ Windows installer (Inno Setup 6). Built by CI:
;   ISCC /DAppVersion=1.0.0 /DSrc=<folder holding Pump Organ.vst3 and Pump Organ.exe> Pump Organ.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Src
  #define Src "..\..\dist\pumporgan"
#endif

[Setup]
AppId={{8B3F1C52-6E0D-4A97-B1E4-2D5F7A9C3E68}
AppName=Pump Organ
AppVersion={#AppVersion}
AppPublisher=Pump Organ
DefaultDirName={autopf}\Pump Organ
DefaultGroupName=Pump Organ
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=PumpOrgan-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Pump Organ

[Types]
Name: "full"; Description: "VST3 plug-in and standalone application"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (for your DAW)"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Src}\Pump Organ.vst3\*"; DestDir: "{commoncf64}\VST3\Pump Organ.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#Src}\Pump Organ.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\Pump Organ"; Filename: "{app}\Pump Organ.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Pump Organ.vst3"
