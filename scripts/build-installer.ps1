param(
    [string]$InstallerScript = "installer/WevoaSetup.iss"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$resolvedScript = if ([System.IO.Path]::IsPathRooted($InstallerScript)) {
    [System.IO.Path]::GetFullPath($InstallerScript)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $InstallerScript))
}
$versionHeader = Join-Path $root "utils/version.h"

if (-not (Test-Path $resolvedScript)) {
    throw "Installer script not found: $resolvedScript"
}

if (-not (Test-Path $versionHeader)) {
    throw "Version header not found: $versionHeader"
}

$versionHeaderContents = Get-Content $versionHeader -Raw
$versionMatch = [regex]::Match($versionHeaderContents, '#define\s+WEVOA_VERSION\s+"([^"]+)"')
if (-not $versionMatch.Success) {
    throw "Unable to read WEVOA_VERSION from $versionHeader"
}

$appVersion = $versionMatch.Groups[1].Value

& (Join-Path $PSScriptRoot "build-release.ps1")

$iscc = Get-Command ISCC -ErrorAction SilentlyContinue
if (-not $iscc) {
    $commonPaths = @(
        "C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe",
        "C:\\Program Files\\Inno Setup 6\\ISCC.exe"
    )

    foreach ($candidate in $commonPaths) {
        if (Test-Path $candidate) {
            $iscc = [pscustomobject]@{ Source = $candidate }
            break
        }
    }
}

if (-not $iscc) {
    throw "Inno Setup compiler (ISCC) was not found on PATH or in the standard Inno Setup install folders. Install Inno Setup, then rerun this script."
}

Write-Host "[Wevoa] Building Windows installer..."
& $iscc.Source "/DAppVersion=$appVersion" $resolvedScript

if ($LASTEXITCODE -ne 0) {
    throw "Installer build failed."
}

Write-Host "[Wevoa] Installer build complete"
Write-Host "[Wevoa] Output: $(Join-Path $root 'dist\\installer\\WevoaSetup.exe')"
