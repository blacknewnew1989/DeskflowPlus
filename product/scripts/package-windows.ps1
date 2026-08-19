param(
    [string]$RepoRoot = "",
    [ValidateSet("Release")]
    [string]$Configuration = "Release",
    [string]$SigningCertificatePath = $env:RELAYDESK_WINDOWS_SIGN_CERTIFICATE,
    [string]$SigningCertificateThumbprint = $env:RELAYDESK_WINDOWS_SIGN_THUMBPRINT,
    [SecureString]$SigningCertificatePassword,
    [string]$SigningTimestampUrl = $env:RELAYDESK_WINDOWS_SIGN_TIMESTAMP_URL,
    [string]$SignToolPath = $env:RELAYDESK_WINDOWS_SIGNTOOL
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (& git rev-parse --show-toplevel).Trim()
}
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $ScriptRoot "WindowsPackageContract.psm1") -Force
Import-Module (Join-Path $ScriptRoot "WindowsSigning.psm1") -Force

& (Join-Path $ScriptRoot "setup-windows.ps1") -RepoRoot $RepoRoot

if ($null -eq $SigningCertificatePassword -and -not [string]::IsNullOrEmpty($env:RELAYDESK_WINDOWS_SIGN_PASSWORD)) {
    $SigningCertificatePassword = ConvertTo-SecureString $env:RELAYDESK_WINDOWS_SIGN_PASSWORD -AsPlainText -Force
}
$SigningPlan = New-RelayDeskWindowsSigningPlan `
    -CertificatePath $SigningCertificatePath `
    -CertificateThumbprint $SigningCertificateThumbprint `
    -CertificatePassword $SigningCertificatePassword `
    -TimestampUrl $SigningTimestampUrl `
    -SignToolPath $SignToolPath
Write-Host "WINDOWS_SIGNING_STATUS=$($SigningPlan.Status)"
if (-not $SigningPlan.Enabled) {
    Write-Host "WINDOWS_SIGNING_DIAGNOSTIC=no signing identity configured; producing unsigned internal packages"
}

& (Join-Path $ScriptRoot "build-windows.ps1") `
    -RepoRoot $RepoRoot `
    -Configuration $Configuration `
    -RunTests `
    -SkipAutoSetup `
    -PackageVariant $SigningPlan.Status `
    -CleanBuild

$BuildDir = Join-Path $RepoRoot "build\windows\release"
if ($SigningPlan.Enabled) {
    $Payloads = @(Get-RelayDeskWindowsSignablePayloads -BuildDir $BuildDir)
    if ($Payloads.Count -eq 0) { throw "WIN_SIGN_PAYLOAD_MISSING: no EXE or DLL payloads were found" }
    Invoke-RelayDeskSignTool -Plan $SigningPlan -Files $Payloads
    Invoke-RelayDeskSignTool -Plan $SigningPlan -Files $Payloads -VerifyOnly
}
& cmake --build $BuildDir --config Release --target package package_source --parallel
if ($LASTEXITCODE -ne 0) { throw "Windows package target failed." }

if ($SigningPlan.Enabled) {
    $Installers = @(Get-ChildItem -LiteralPath $BuildDir -File -Filter "*-signed.msi" | Select-Object -ExpandProperty FullName)
    if ((Get-Command wix -ErrorAction SilentlyContinue) -and $Installers.Count -ne 1) {
        throw "WIN_SIGN_MSI_MISSING: WiX is available but the signed MSI was not generated"
    }
    if ($Installers.Count -gt 0) {
        Invoke-RelayDeskSignTool -Plan $SigningPlan -Files $Installers
        Invoke-RelayDeskSignTool -Plan $SigningPlan -Files $Installers -VerifyOnly
    }
}

$FullSha = (& git -C $RepoRoot rev-parse HEAD).Trim()
$OutDir = Join-Path $RepoRoot "dist\windows\$FullSha"
$Collector = Join-Path $RepoRoot "product\scripts\collect-ci-artifacts.py"
$Python = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } else { "py" }
if ($Python -eq "py") {
    $CollectorArguments = @($Collector, "--build-dir", $BuildDir, "--out-dir", $OutDir, "--platform", "windows-x64", "--commit", $FullSha)
    if ($SigningPlan.Enabled) { $CollectorArguments += "--signed" }
    & py -3 @CollectorArguments
}
else {
    $CollectorArguments = @($Collector, "--build-dir", $BuildDir, "--out-dir", $OutDir, "--platform", "windows-x64", "--commit", $FullSha)
    if ($SigningPlan.Enabled) { $CollectorArguments += "--signed" }
    & python @CollectorArguments
}
if ($LASTEXITCODE -ne 0) { throw "Artifact collection failed." }
$Contract = Test-RelayDeskWindowsPackageArtifacts `
    -OutDir $OutDir `
    -RequireMsi ([bool](Get-Command wix -ErrorAction SilentlyContinue))
Write-Host "WINDOWS_SIGNATURE_STATUS=$($Contract.SignatureStatus)"
Write-Host "WINDOWS_PORTABLE=$($Contract.Portable.Name)"
if ($Contract.Installer) { Write-Host "WINDOWS_INSTALLER=$($Contract.Installer.Name)" }
Write-Host "WINDOWS_ARTIFACTS=$OutDir"
