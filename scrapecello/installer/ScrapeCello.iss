; Scrape Cello Windows installer (Inno Setup 6). Built by CI:
;   ISCC /DAppVersion=1.0.0 /DSrc=<folder holding Scrape Cello.vst3 and Scrape Cello.exe> Scrape Cello.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Src
  #define Src "..\..\dist\scrapecello"
#endif

[Setup]
AppId={{4D2A9E71-3B5C-4F8E-A6D2-7C1E90B3F5A2}
AppName=Scrape Cello
AppVersion={#AppVersion}
AppPublisher=Scrape Cello
DefaultDirName={autopf}\Scrape Cello
DefaultGroupName=Scrape Cello
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=ScrapeCello-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Scrape Cello

[Types]
Name: "full"; Description: "VST3 plug-in and standalone application"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (for your DAW)"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Src}\Scrape Cello.vst3\*"; DestDir: "{commoncf64}\VST3\Scrape Cello.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#Src}\Scrape Cello.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\Scrape Cello"; Filename: "{app}\Scrape Cello.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Scrape Cello.vst3"
