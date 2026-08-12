$ErrorActionPreference = "Continue"
$Failed = $false

function Show-CommandVersion(
    [string]$Name,
    [scriptblock]$VersionCommand
) {
    if (Get-Command $Name -ErrorAction SilentlyContinue) {
        Write-Host "== $Name =="
        & $VersionCommand
    }
    else {
        Write-Error "MISSING: $Name"
        $script:Failed = $true
    }
}

Write-Host "RelayDesk Windows development environment"

Show-CommandVersion "git" { git --version }
Show-CommandVersion "cmake" { cmake --version | Select-Object -First 2 }

$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $VsWhere) {
    Write-Host "== Visual Studio =="
    & $VsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}
else {
    Write-Error "MISSING: vswhere / Visual Studio Installer"
    $Failed = $true
}

$QtFound = $false
foreach ($name in @("qtpaths6", "qmake6")) {
    if (Get-Command $name -ErrorAction SilentlyContinue) {
        Write-Host "== Qt =="
        if ($name -eq "qtpaths6") { & qtpaths6 --qt-version }
        else { & qmake6 -query QT_VERSION }
        $QtFound = $true
        break
    }
}
if (-not $QtFound -and $env:Qt6_DIR) {
    Write-Host "Qt6_DIR=$env:Qt6_DIR"
    $QtFound = $true
}
if (-not $QtFound) {
    Write-Error "MISSING: Qt 6 detection (qtpaths6/qmake6/Qt6_DIR)"
    $Failed = $true
}

if (Get-Command openssl -ErrorAction SilentlyContinue) {
    openssl version
}
else {
    Write-Warning "OpenSSL not found on PATH; vcpkg may provide it during configure."
}

if ($env:VCPKG_ROOT) {
    Write-Host "VCPKG_ROOT=$env:VCPKG_ROOT"
}
else {
    Write-Warning "VCPKG_ROOT is not set; verify the pinned upstream build instructions."
}

if ($Failed) {
    throw "Environment is incomplete. Use official Deskflow build documentation."
}

Write-Host "Basic tools detected. A real v1.26.0 configure/build is still required."
