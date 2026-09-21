; SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef RelayVersion
#define RelayVersion "0.1.0-beta.3"
#endif
[Setup]
AppId={{7F243DF9-2836-4F51-961A-63EC63DC4CC4}
AppName=Relay
AppVersion={#RelayVersion}
AppPublisher=Relay contributors
AppPublisherURL=https://relay-terminal.ai
DefaultDirName={localappdata}\Programs\Relay
DefaultGroupName=Relay
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\..\dist
OutputBaseFilename=relay_{#RelayVersion}_windows_x64_setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\relay.exe
CloseApplications=yes
[Files]
Source: "{#RelayStage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Relay"; Filename: "{app}\bin\relay.exe"; WorkingDir: "{userdocs}"
Name: "{autodesktop}\Relay"; Filename: "{app}\bin\relay.exe"; WorkingDir: "{userdocs}"; Tasks: desktopicon
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
[Run]
Filename: "{app}\bin\relay.exe"; WorkingDir: "{userdocs}"; Description: "Open Relay"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Classes\relay"; ValueType: string; ValueData: "URL:Relay"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\relay"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\relay\shell\open\command"; ValueType: string; ValueData: """{app}\runtime\python\pythonw.exe"" -X utf8 ""{app}\share\relay\scripts\relay-open"" ""%1"""
