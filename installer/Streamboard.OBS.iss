#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
[Setup]
AppId={{A54E72C6-8CD0-4D1F-8D6C-1B25FC319CA8}
AppName=Streamboard OBS
AppVersion={#AppVersion}
AppPublisher=Webvibecoding
AppPublisherURL=https://github.com/webvibecoding/streamboard-obs
DefaultDirName={commonappdata}\obs-studio\plugins\streamboard-obs
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\artifacts
OutputBaseFilename=Streamboard.OBS-Setup-{#AppVersion}
SetupIconFile=..\data\streamboard.ico
UninstallDisplayIcon={app}\data\streamboard.ico
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
UninstallDisplayName=Streamboard OBS
VersionInfoVersion={#AppVersion}.0
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
[Files]
Source: "..\artifacts\package\plugins\streamboard-obs\streamboard-obs.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\artifacts\package\plugins\streamboard-obs\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
var Locator, Services, Processes: Variant;
begin
  Result := True;
  if CurPageID <> wpReady then Exit;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT Name FROM Win32_Process WHERE Name = "obs64.exe" OR Name = "obs.exe"');
    if Processes.Count > 0 then begin
      MsgBox('Close OBS Studio before installing Streamboard OBS / Закройте OBS Studio перед установкой.', mbInformation, MB_OK);
      Result := False;
    end;
  except
    { Windows may restrict WMI. Never terminate OBS automatically. }
  end;
end;
