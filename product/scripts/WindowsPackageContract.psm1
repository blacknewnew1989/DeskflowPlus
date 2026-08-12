function Test-RelayDeskWindowsPackageArtifacts {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutDir,
        [bool]$RequireMsi = $false
    )

    if (-not (Test-Path -LiteralPath $OutDir -PathType Container)) {
        throw "WIN_PACKAGE_OUTPUT_MISSING: $OutDir"
    }

    $Files = @(Get-ChildItem -LiteralPath $OutDir -File)
    $Portable = @($Files | Where-Object {
        $_.Name -match '^relaydesk-.+-win-(x64|arm64)-unsigned-portable\.(7z|zip)$'
    })
    if ($Portable.Count -ne 1) {
        throw "WIN_PORTABLE_ARTIFACT_INVALID: expected one unsigned portable archive, found $($Portable.Count)"
    }

    $Installer = @($Files | Where-Object {
        $_.Name -match '^relaydesk-.+-win-(x64|arm64)-unsigned\.msi$'
    })
    if ($Installer.Count -gt 1) {
        throw "WIN_MSI_ARTIFACT_INVALID: expected at most one unsigned MSI, found $($Installer.Count)"
    }
    if ($RequireMsi -and $Installer.Count -ne 1) {
        throw "WIN_MSI_ARTIFACT_MISSING: WiX is available but no unsigned MSI was collected"
    }

    $ManifestPath = Join-Path $OutDir "artifact-manifest.json"
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "WIN_ARTIFACT_MANIFEST_MISSING: $ManifestPath"
    }
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ($Manifest.signed -ne $false) {
        throw "WIN_SIGNATURE_STATUS_INVALID: unsigned packages require manifest signed=false"
    }

    [pscustomobject]@{
        Portable = $Portable[0]
        Installer = if ($Installer.Count -eq 1) { $Installer[0] } else { $null }
        SignatureStatus = "unsigned"
    }
}

Export-ModuleMember -Function Test-RelayDeskWindowsPackageArtifacts
