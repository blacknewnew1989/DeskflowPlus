$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$BuildScriptPath = Join-Path $RepoRoot "product\scripts\build-windows.ps1"
$BuildScript = Get-Content -LiteralPath $BuildScriptPath -Raw
$DevShellOffset = $BuildScript.IndexOf("Enter-VsDevShell", [StringComparison]::Ordinal)
$RestoreStatement = 'if (Test-Path $EnvFile) { . $EnvFile }'
$RestoreOffset = $BuildScript.IndexOf($RestoreStatement, [StringComparison]::Ordinal)
if ($DevShellOffset -lt 0 -or $RestoreOffset -le $DevShellOffset) {
    throw "WIN007_PROJECT_TOOLCHAIN_NOT_RESTORED_AFTER_DEVSHELL"
}
if (-not $BuildScript.Contains(
    '$Arguments += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"',
    [StringComparison]::Ordinal
)) {
    throw "WIN007_CMAKE_TOOLCHAIN_ARGUMENT_MISSING"
}

$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("relaydesk-win007-" + [guid]::NewGuid().ToString("N"))
$OriginalPath = $env:PATH
$OriginalQtPrefix = $env:RELAYDESK_QT_PREFIX
$OriginalVcpkgRoot = $env:VCPKG_ROOT
try {
    $ProjectQt = Join-Path $TestRoot ".tools\Qt"
    $ProjectVcpkg = Join-Path $TestRoot ".tools\vcpkg"
    $EnvFile = Join-Path $TestRoot ".relaydesk-toolchain-windows.ps1"
    New-Item -ItemType Directory -Force -Path $ProjectQt, $ProjectVcpkg | Out-Null
    @"
`$env:RELAYDESK_QT_PREFIX = "$ProjectQt"
`$env:VCPKG_ROOT = "$ProjectVcpkg"
`$env:PATH = "$ProjectQt\bin;$ProjectVcpkg;`$env:PATH"
"@ | Set-Content -LiteralPath $EnvFile -Encoding UTF8

    $env:RELAYDESK_QT_PREFIX = "C:\Program Files\Microsoft Visual Studio\2022\Qt"
    $env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\2022\VC\vcpkg"
    $env:PATH = "$env:VCPKG_ROOT;$OriginalPath"
    . $EnvFile

    if ($env:RELAYDESK_QT_PREFIX -ne $ProjectQt -or $env:VCPKG_ROOT -ne $ProjectVcpkg) {
        throw "WIN007_DEVSHELL_OVERRIDE_LEAKED"
    }
    if (($env:PATH -split ";")[0] -ne (Join-Path $ProjectQt "bin") -or
        ($env:PATH -split ";")[1] -ne $ProjectVcpkg) {
        throw "WIN007_PROJECT_PATH_NOT_RESTORED"
    }
    $CMakeToolchainArgument = "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
    $ExpectedToolchainArgument = "-DCMAKE_TOOLCHAIN_FILE=$ProjectVcpkg\scripts\buildsystems\vcpkg.cmake"
    if ($CMakeToolchainArgument -ne $ExpectedToolchainArgument -or
        -not $CMakeToolchainArgument.Contains(".tools\vcpkg", [StringComparison]::OrdinalIgnoreCase)) {
        throw "WIN007_CMAKE_TOOLCHAIN_NOT_REPOSITORY_LOCAL: $CMakeToolchainArgument"
    }

    Write-Output "WIN007_TOOLCHAIN_PRECEDENCE_TEST=PASS"
}
finally {
    $env:PATH = $OriginalPath
    $env:RELAYDESK_QT_PREFIX = $OriginalQtPrefix
    $env:VCPKG_ROOT = $OriginalVcpkgRoot
    if (Test-Path -LiteralPath $TestRoot) {
        [System.IO.Directory]::Delete($TestRoot, $true)
    }
}
