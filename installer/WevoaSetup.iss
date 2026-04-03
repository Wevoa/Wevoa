#define AppName "Wevoa"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppPublisher "WevoaWeb"
#define AppURL "https://github.com/AHADBAVA/Wevoa-Web"
#define AppExeName "wevoa.exe"

[Setup]
AppId={{6F35B02D-4D46-4B1B-8AA8-B617DFA87A5E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} Setup v{#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\Wevoa
DefaultGroupName=Wevoa
DisableProgramGroupPage=no
LicenseFile=..\LICENSE
OutputDir=..\dist\installer
OutputBaseFilename=WevoaSetup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
PrivilegesRequired=admin
SetupLogging=yes
UninstallDisplayIcon={app}\wevoa.exe

[Tasks]
Name: "addtopath"; Description: "Add Wevoa to PATH"; GroupDescription: "Options:"; Flags: checkedonce
Name: "startmenuicon"; Description: "Create Start Menu shortcut"; GroupDescription: "Options:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create Desktop shortcut"; GroupDescription: "Options:"

[Files]
Source: "..\dist\windows\wevoa.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\Wevoa CLI"; Filename: "{app}\wevoa.exe"; Tasks: startmenuicon
Name: "{commondesktop}\Wevoa CLI"; Filename: "{app}\wevoa.exe"; Tasks: desktopicon

[Run]
Filename: "{cmd}"; Parameters: "/K cd /d ""{app}"""; Description: "Open terminal"; Flags: postinstall shellexec skipifsilent unchecked
Filename: "{app}\README.md"; Description: "View documentation"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C echo Wevoa has been removed from this machine."; Flags: runhidden skipifdoesntexist; RunOnceId: "WevoaUninstallNotice"

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function NeedsAddPath(const Dir: string): Boolean;
var
  PathValue: string;
begin
  Result := True;
  if not RegQueryStringValue(HKLM, EnvironmentKey, 'Path', PathValue) then
    Exit;

  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(PathValue) + ';') = 0;
end;

procedure AddPath(const Dir: string);
var
  PathValue: string;
begin
  if not NeedsAddPath(Dir) then
    Exit;

  if not RegQueryStringValue(HKLM, EnvironmentKey, 'Path', PathValue) then
    PathValue := '';

  if (PathValue <> '') and (Copy(PathValue, Length(PathValue), 1) <> ';') then
    PathValue := PathValue + ';';

  PathValue := PathValue + Dir;
  RegWriteStringValue(HKLM, EnvironmentKey, 'Path', PathValue);
end;

procedure RemovePath(const Dir: string);
var
  PathValue: string;
  NewValue: string;
  StartPos: Integer;
begin
  if not RegQueryStringValue(HKLM, EnvironmentKey, 'Path', PathValue) then
    Exit;

  NewValue := ';' + PathValue + ';';
  StartPos := Pos(';' + Dir + ';', NewValue);
  if StartPos = 0 then
    StartPos := Pos(';' + Lowercase(Dir) + ';', Lowercase(NewValue));

  if StartPos = 0 then
    Exit;

  Delete(NewValue, StartPos, Length(Dir) + 1);
  if Copy(NewValue, 1, 1) = ';' then
    Delete(NewValue, 1, 1);
  if (NewValue <> '') and (Copy(NewValue, Length(NewValue), 1) = ';') then
    Delete(NewValue, Length(NewValue), 1);

  RegWriteStringValue(HKLM, EnvironmentKey, 'Path', NewValue);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then begin
    AddPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    RemovePath(ExpandConstant('{app}'));
  end;
end;
