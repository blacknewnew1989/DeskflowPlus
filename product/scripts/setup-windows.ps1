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
    $pythonUserScripts = $null
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $pythonUserScripts = (
            & python -c "import sysconfig; print(sysconfig.get_path('scripts', scheme='nt_user'))" 2>$null
        ).Trim()
    }
    $windowsPowerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0"
    $env:Path = "$machine;$user;$pythonUserScripts;$windowsPowerShell;$env:USERPROFILE\.dotnet\tools;$env:Path"
}

function Get-VcToolsInstallPath([string]$VsWhere) {
    if (-not (Test-Path $VsWhere)) { return $null }
    $result = @(
        & $VsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
    ) | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($result)) { return $null }
    return $result.Trim()
}

function Test-QtPrefix([string]$Prefix) {
    $required = @(
        "lib\cmake\Qt6\Qt6Config.cmake",
        "lib\cmake\Qt6Svg\Qt6SvgConfig.cmake",
        "bin\lrelease.exe",
        "bin\qmlimportscanner.exe",
        "plugins\platforms\qwindows.dll"
    )
    foreach ($relative in $required) {
        if (-not (Test-Path (Join-Path $Prefix $relative))) { return $false }
    }
    return $true
}

function Install-QtArchives(
    [string]$PythonCommand,
    [string]$Version,
    [string]$OutputRoot
) {
    foreach ($archive in @("qtbase", "qtdeclarative", "qttools", "qtsvg", "qttranslations")) {
        $installed = $false
        for ($attempt = 1; $attempt -le 4; $attempt++) {
            if ($PythonCommand -eq "py") {
                & py -3 -m aqt install-qt windows desktop $Version win64_msvc2022_64 `
                    -O $OutputRoot --archives $archive
            }
            else {
                & python -m aqt install-qt windows desktop $Version win64_msvc2022_64 `
                    -O $OutputRoot --archives $archive
            }
            if ($LASTEXITCODE -eq 0) {
                $installed = $true
                break
            }
            Start-Sleep -Seconds (3 * $attempt)
        }
        if (-not $installed) { return $false }
    }
    return $true
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

# Winget package paths are not always visible to the current process. Python's
# self-contained CMake/Ninja wheels are a non-admin fallback before using CI.
Refresh-ProcessPath
if ((-not (Has-Command "cmake")) -or (-not (Has-Command "ninja"))) {
    if (Has-Command "python") {
        & python -m pip install --disable-pip-version-check --user cmake ninja
        Refresh-ProcessPath
    }
}
if ((-not (Has-Command "cmake")) -or (-not (Has-Command "ninja"))) {
    $ActionsFallback = $true
}

$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$VsPath = Get-VcToolsInstallPath $VsWhere
if (-not $VsPath) {
    $ok = Install-Winget "Microsoft.VisualStudio.2022.BuildTools" @(
        "--override",
        "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    )
    if (-not $ok) { $ActionsFallback = $true }
    $VsPath = Get-VcToolsInstallPath $VsWhere
}
if (-not $VsPath) { $ActionsFallback = $true }

$PythonExe = $null
if (Has-Command "python") { $PythonExe = "python" }
elseif (Has-Command "py") { $PythonExe = "py" }
$PythonUserScripts = $null
if ($PythonExe -eq "python") {
    $PythonUserScripts = (
        & python -c "import sysconfig; print(sysconfig.get_path('scripts', scheme='nt_user'))" 2>$null
    ).Trim()
}
elseif ($PythonExe -eq "py") {
    $PythonUserScripts = (
        & py -3 -c "import sysconfig; print(sysconfig.get_path('scripts', scheme='nt_user'))" 2>$null
    ).Trim()
}

$QtRoot = Join-Path $ToolsRoot "Qt"
$QtPrefix = Join-Path $QtRoot "$QtVersion\msvc2022_64"
$QtStagingRoot = Join-Path $ToolsRoot "Qt-staging"
$QtStagingPrefix = Join-Path $QtStagingRoot "$QtVersion\msvc2022_64"
if (-not (Test-QtPrefix $QtPrefix)) {
    if (Test-QtPrefix $QtStagingPrefix) {
        $QtPrefix = $QtStagingPrefix
    }
    elseif ($PythonExe -eq "py") {
        & py -3 -m pip install --disable-pip-version-check --user aqtinstall
        if ($LASTEXITCODE -eq 0) {
            $null = Install-QtArchives "py" $QtVersion $QtStagingRoot
        }
    }
    elseif ($PythonExe -eq "python") {
        & python -m pip install --disable-pip-version-check --user aqtinstall
        if ($LASTEXITCODE -eq 0) {
            $null = Install-QtArchives "python" $QtVersion $QtStagingRoot
        }
    }
    else {
        $ActionsFallback = $true
    }
    if (Test-QtPrefix $QtStagingPrefix) {
        $QtPrefix = $QtStagingPrefix
    }
    else {
        $ActionsFallback = $true
    }
}

$VcpkgRoot = Join-Path $ToolsRoot "vcpkg"
$BootstrapVcpkg = Join-Path $VcpkgRoot "bootstrap-vcpkg.bat"
if (-not (Test-Path $BootstrapVcpkg)) {
    if ((Has-Command "git") -and (-not (Test-Path (Join-Path $VcpkgRoot ".git")))) {
        for ($attempt = 1; $attempt -le 4; $attempt++) {
            & git -c http.sslBackend=openssl -c http.version=HTTP/1.1 `
                -c http.lowSpeedLimit=1 -c http.lowSpeedTime=30 `
                clone --depth=1 --filter=blob:none https://github.com/microsoft/vcpkg.git $VcpkgRoot
            if ($LASTEXITCODE -eq 0) { break }
            Start-Sleep -Seconds (3 * $attempt)
        }
    }
    elseif (Test-Path (Join-Path $VcpkgRoot ".git")) {
        for ($attempt = 1; $attempt -le 4; $attempt++) {
            & git -C $VcpkgRoot -c http.sslBackend=openssl -c http.version=HTTP/1.1 `
                -c http.lowSpeedLimit=1 -c http.lowSpeedTime=30 `
                fetch --depth=1 --filter=blob:none origin master
            if ($LASTEXITCODE -eq 0) {
                & git -C $VcpkgRoot checkout -B master FETCH_HEAD
                break
            }
            Start-Sleep -Seconds (3 * $attempt)
        }
    }
}
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
`$env:PATH = "$QtPrefix\bin;$VcpkgRoot;$PythonUserScripts;`$env:SystemRoot\System32\WindowsPowerShell\v1.0;`$env:USERPROFILE\.dotnet\tools;`$env:PATH"
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
    vsInstallPath = $VsPath
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
