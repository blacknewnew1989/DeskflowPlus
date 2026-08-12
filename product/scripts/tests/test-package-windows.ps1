$ErrorActionPreference = "Stop"
$Module = Join-Path (Split-Path -Parent $PSScriptRoot) "WindowsPackageContract.psm1"
Import-Module $Module -Force

$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("relaydesk-win003-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $TestRoot | Out-Null
    New-Item -ItemType File -Path (Join-Path $TestRoot "relaydesk-1.26.0-win-x64-unsigned-portable.7z") | Out-Null
    New-Item -ItemType File -Path (Join-Path $TestRoot "relaydesk-1.26.0-win-x64-unsigned.msi") | Out-Null
    @{ platform = "windows-x64"; commit = "test"; signed = $false; files = @() } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TestRoot "artifact-manifest.json") -Encoding UTF8

    $Result = Test-RelayDeskWindowsPackageArtifacts -OutDir $TestRoot -RequireMsi $true
    if ($Result.SignatureStatus -ne "unsigned") { throw "unexpected signature status" }
    if ($Result.Portable.Name -notmatch "unsigned-portable") { throw "portable package was not selected" }
    if ($Result.Installer.Name -notmatch "unsigned\.msi$") { throw "MSI package was not selected" }

    Move-Item -LiteralPath $Result.Installer.FullName -Destination (Join-Path $TestRoot "quarantined.msi")
    $MissingMsiFailed = $false
    try {
        Test-RelayDeskWindowsPackageArtifacts -OutDir $TestRoot -RequireMsi $true | Out-Null
    }
    catch {
        $MissingMsiFailed = $_.Exception.Message -like "WIN_MSI_ARTIFACT_MISSING:*"
    }
    if (-not $MissingMsiFailed) { throw "missing MSI was not rejected" }

    Move-Item -LiteralPath (Join-Path $TestRoot "relaydesk-1.26.0-win-x64-unsigned-portable.7z") `
        -Destination (Join-Path $TestRoot "relaydesk-1.26.0-win-x64-signed-portable.7z")
    Move-Item -LiteralPath (Join-Path $TestRoot "quarantined.msi") `
        -Destination (Join-Path $TestRoot "relaydesk-1.26.0-win-x64-signed.msi")
    @{ platform = "windows-x64"; commit = "test"; signed = $true; files = @() } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TestRoot "artifact-manifest.json") -Encoding UTF8
    $SignedResult = Test-RelayDeskWindowsPackageArtifacts -OutDir $TestRoot -RequireMsi $true
    if ($SignedResult.SignatureStatus -ne "signed") { throw "signed manifest was not honored" }

    Write-Host "WIN003_PACKAGE_CONTRACT_TEST=PASS"
}
finally {
    if (Test-Path -LiteralPath $TestRoot) {
        [System.IO.Directory]::Delete($TestRoot, $true)
    }
}
