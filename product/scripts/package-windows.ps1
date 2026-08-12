param(
    [string]$RepoRoot = "",
    [ValidateSet("Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (& git rev-parse --show-toplevel).Trim()
}
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $ScriptRoot "WindowsPackageContract.psm1") -Force

& (Join-Path $ScriptRoot "setup-windows.ps1") -RepoRoot $RepoRoot
& (Join-Path $ScriptRoot "build-windows.ps1") -RepoRoot $RepoRoot -Configuration $Configuration -RunTests -SkipAutoSetup

$BuildDir = Join-Path $RepoRoot "build\windows\release"
& cmake --build $BuildDir --config Release --target package package_source --parallel
if ($LASTEXITCODE -ne 0) { throw "Windows package target failed." }

$FullSha = (& git -C $RepoRoot rev-parse HEAD).Trim()
$OutDir = Join-Path $RepoRoot "dist\windows\$FullSha"
$Collector = Join-Path $RepoRoot "product\scripts\collect-ci-artifacts.py"
$Python = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "py" }
if ($Python -eq "py") {
    & py -3 $Collector --build-dir $BuildDir --out-dir $OutDir --platform windows-x64 --commit $FullSha
}
else {
    & python $Collector --build-dir $BuildDir --out-dir $OutDir --platform windows-x64 --commit $FullSha
}
if ($LASTEXITCODE -ne 0) { throw "Artifact collection failed." }
$Contract = Test-RelayDeskWindowsPackageArtifacts `
    -OutDir $OutDir `
    -RequireMsi ([bool](Get-Command wix -ErrorAction SilentlyContinue))
Write-Host "WINDOWS_SIGNATURE_STATUS=$($Contract.SignatureStatus)"
Write-Host "WINDOWS_PORTABLE=$($Contract.Portable.Name)"
if ($Contract.Installer) { Write-Host "WINDOWS_INSTALLER=$($Contract.Installer.Name)" }
Write-Host "WINDOWS_ARTIFACTS=$OutDir"
