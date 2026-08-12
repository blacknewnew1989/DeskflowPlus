param(
    [string]$RepoRoot = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$RunTests,
    [switch]$SkipAutoSetup,
    [ValidateSet("signed", "unsigned")]
    [string]$PackageVariant = "unsigned"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (& git rev-parse --show-toplevel).Trim()
}
if (-not $RepoRoot) { throw "Not inside a Git repository." }
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$EnvFile = Join-Path $RepoRoot ".relaydesk-toolchain-windows.ps1"
if (-not $SkipAutoSetup -and -not (Test-Path $EnvFile)) {
    & (Join-Path $ScriptRoot "setup-windows.ps1") -RepoRoot $RepoRoot
}
if (Test-Path $EnvFile) { . $EnvFile }

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWhere) {
        $VsPathResult = @(
            & $VsWhere -latest -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath 2>$null
        ) | Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($VsPathResult)) {
            $VsPath = $VsPathResult.Trim()
            $DevShell = Join-Path $VsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
            if (Test-Path $DevShell) {
                Import-Module $DevShell
                Enter-VsDevShell -VsInstallPath $VsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
            }
        }
    }
}

foreach ($command in @("cmake", "ninja", "cl.exe")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command is unavailable. A0 must use the GitHub Actions Windows runner."
    }
}
$QtRequired = @(
    "lib\cmake\Qt6\Qt6Config.cmake",
    "lib\cmake\Qt6Svg\Qt6SvgConfig.cmake",
    "bin\lrelease.exe",
    "plugins\platforms\qwindows.dll"
)
$QtReady = $env:RELAYDESK_QT_PREFIX
foreach ($relative in $QtRequired) {
    if (-not $env:RELAYDESK_QT_PREFIX -or -not (Test-Path (Join-Path $env:RELAYDESK_QT_PREFIX $relative))) {
        $QtReady = $false
        break
    }
}
if (-not $QtReady) {
    throw "Qt is unavailable. A0 must use the GitHub Actions Windows runner."
}

$ConfigLower = $Configuration.ToLowerInvariant()
$BuildDir = Join-Path $RepoRoot ("build\windows\" + $ConfigLower)
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Triplet = if ($Configuration -eq "Release") { "x64-windows-release" } else { "x64-windows" }
$Arguments = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DSKIP_BUILD_TESTS=ON",
    "-DBUILD_TESTS=ON",
    "-DBUILD_INSTALLER=ON",
    "-DRELAYDESK_PACKAGE_VARIANT=$PackageVariant",
    "-DCMAKE_PREFIX_PATH=$env:RELAYDESK_QT_PREFIX"
)
if ($env:VCPKG_ROOT) {
    $Arguments += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
    $Arguments += "-DVCPKG_TARGET_TRIPLET=$Triplet"
}

& cmake @Arguments
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
& cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

if ($RunTests) {
    & ctest --test-dir (Join-Path $BuildDir "src/unittests") -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
}

Write-Host "BUILD_DIR=$BuildDir"
