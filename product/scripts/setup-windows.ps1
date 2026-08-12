param(
    [string]$RepoRoot = "",
    [string]$QtVersion = "6.10.1"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (& git rev-parse --show-toplevel).Trim()
}
if (-not $RepoRoot) { throw "Not inside a Git repository." }
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ToolsRoot = Join-Path $RepoRoot ".tools"
$Working = Join-Path $RepoRoot "product\working\toolchains"
New-Item -ItemType Directory -Force -Path $ToolsRoot, $Working | Out-Null

function Has-Command([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Refresh-ProcessPath {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user;$env:Path"
}

function Install-Winget([string]$Id, [string[]]$Extra = @()) {
    if (-not (Has-Command "winget")) { return $false }
    $arguments = @(
        "install", "--id", $Id, "--exact", "--silent",
        "--accept-package-agreements", "--accept-source-agreements"
    ) + $Extra
    & winget @arguments
    Refresh-ProcessPath
    return ($LASTEXITCODE -eq 0)
}

$ActionsFallback = $false
$Tools = @(
    @{ Name = "git"; Id = "Git.Git" },
    @{ Name = "cmake"; Id = "Kitware.CMake" },
    @{ Name = "ninja"; Id = "Ninja-build.Ninja" },
    @{ Name = "python"; Id = "Python.Python.3.12" },
    @{ Name = "dotnet"; Id = "Microsoft.DotNet.SDK.8" },
    @{ Name = "7z"; Id = "7zip.7zip" }
)
foreach ($item in $Tools) {
    if (-not (Has-Command $item.Name)) {
        if (-not (Install-Winget $item.Id)) { $ActionsFallback = $true }
    }
}

$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) {
    $ok = Install-Winget "Microsoft.VisualStudio.2022.BuildTools" @(
        "--override",
        "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    )
    if (-not $ok) { $ActionsFallback = $true }
}
if (-not (Test-Path $VsWhere)) { $ActionsFallback = $true }

$PythonExe = $null
if (Has-Command "python") { $PythonExe = "python" }
elseif (Has-Command "py") { $PythonExe = "py" }

$QtRoot = Join-Path $ToolsRoot "Qt"
$QtPrefix = Join-Path $QtRoot "$QtVersion\msvc2022_64"
if (-not (Test-Path (Join-Path $QtPrefix "lib\cmake\Qt6"))) {
    if ($PythonExe -eq "py") {
        & py -3 -m pip install --disable-pip-version-check --user aqtinstall
        if ($LASTEXITCODE -eq 0) {
            & py -3 -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O $QtRoot
        }
    }
    elseif ($PythonExe -eq "python") {
        & python -m pip install --disable-pip-version-check --user aqtinstall
        if ($LASTEXITCODE -eq 0) {
            & python -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O $QtRoot
        }
    }
    else {
        $ActionsFallback = $true
    }
    if (-not (Test-Path (Join-Path $QtPrefix "lib\cmake\Qt6"))) {
        $ActionsFallback = $true
    }
}

$VcpkgRoot = Join-Path $ToolsRoot "vcpkg"
if (-not (Test-Path (Join-Path $VcpkgRoot ".git"))) {
    if (Has-Command "git") {
        & git clone --filter=blob:none https://github.com/microsoft/vcpkg.git $VcpkgRoot
    }
}
if (Test-Path (Join-Path $VcpkgRoot ".git")) {
    $IsShallow = (& git -C $VcpkgRoot rev-parse --is-shallow-repository 2>$null).Trim()
    if ($IsShallow -eq "true") {
        & git -C $VcpkgRoot fetch --unshallow --tags
        if ($LASTEXITCODE -ne 0) { $ActionsFallback = $true }
    }
}
$BootstrapVcpkg = Join-Path $VcpkgRoot "bootstrap-vcpkg.bat"
if (Test-Path $BootstrapVcpkg) {
    & $BootstrapVcpkg -disableMetrics
    if ($LASTEXITCODE -ne 0) { $ActionsFallback = $true }
}
else {
    $ActionsFallback = $true
}

$WixReady = $false
if (Has-Command "dotnet") {
    if (Has-Command "wix") {
        & dotnet tool update --global wix --version 5.0.2 | Out-Host
    }
    else {
        & dotnet tool install --global wix --version 5.0.2 | Out-Host
    }
    Refresh-ProcessPath
    if (Has-Command "wix") {
        foreach ($extension in @(
            "WixToolset.UI.wixext/5.0.2",
            "WixToolset.Util.wixext/5.0.2",
            "WixToolset.Firewall.wixext/5.0.2"
        )) {
            & wix extension add --global $extension 2>$null | Out-Host
        }
        $WixReady = $true
    }
}

$EnvFile = Join-Path $RepoRoot ".relaydesk-toolchain-windows.ps1"
@"
`$env:RELAYDESK_QT_VERSION = "$QtVersion"
`$env:RELAYDESK_QT_PREFIX = "$QtPrefix"
`$env:VCPKG_ROOT = "$VcpkgRoot"
`$env:PATH = "$QtPrefix\bin;$VcpkgRoot;`$env:USERPROFILE\.dotnet\tools;`$env:PATH"
"@ | Set-Content -Encoding UTF8 $EnvFile

$Report = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    repo = $RepoRoot
    qtVersion = $QtVersion
    qtPrefix = $QtPrefix
    vcpkgRoot = $VcpkgRoot
    cmake = if (Get-Command cmake -ErrorAction SilentlyContinue) { (Get-Command cmake).Source } else { $null }
    ninja = if (Get-Command ninja -ErrorAction SilentlyContinue) { (Get-Command ninja).Source } else { $null }
    vswhere = $VsWhere
    wixReady = $WixReady
    actionsFallback = $ActionsFallback
}
$Report | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Working "windows.json")

if ($ActionsFallback) {
    Write-Host "ACTIONS_FALLBACK=true"
    Write-Warning "Local Windows toolchain incomplete. A0 must use relaydesk-build.yml and must not ask the user to install tools."
}
else {
    Write-Host "ACTIONS_FALLBACK=false"
    Write-Host "Windows toolchain prepared."
}
