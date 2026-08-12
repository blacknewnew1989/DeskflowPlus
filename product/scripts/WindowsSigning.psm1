function Protect-RelayDeskSigningText {
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [string]$Text,
        [string[]]$SensitiveValues = @()
    )

    $Protected = $Text
    foreach ($Value in $SensitiveValues) {
        if (-not [string]::IsNullOrEmpty($Value)) {
            $Protected = $Protected.Replace($Value, "***")
        }
    }
    return $Protected
}

function Find-RelayDeskSignTool {
    param([string]$ExplicitPath)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "WIN_SIGNTOOL_NOT_FOUND: configured SignTool path does not exist"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $Command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }

    $WindowsKits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $WindowsKits -PathType Container) {
        $Candidates = @(Get-ChildItem -LiteralPath $WindowsKits -Filter signtool.exe -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match '\\x64$' } |
            Sort-Object FullName -Descending)
        if ($Candidates.Count -gt 0) { return $Candidates[0].FullName }
    }
    return $null
}

function New-RelayDeskWindowsSigningPlan {
    [CmdletBinding()]
    param(
        [string]$CertificatePath,
        [string]$CertificateThumbprint,
        [SecureString]$CertificatePassword,
        [string]$TimestampUrl,
        [string]$SignToolPath
    )

    $HasPath = -not [string]::IsNullOrWhiteSpace($CertificatePath)
    $HasThumbprint = -not [string]::IsNullOrWhiteSpace($CertificateThumbprint)
    if (-not $HasPath -and -not $HasThumbprint) {
        return [pscustomobject]@{ Enabled = $false; Status = "unsigned" }
    }
    if ($HasPath -and $HasThumbprint) {
        throw "WIN_SIGN_IDENTITY_AMBIGUOUS: configure a PFX path or certificate thumbprint, not both"
    }

    $ResolvedCertificate = $null
    if ($HasPath) {
        if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
            throw "WIN_SIGN_CERTIFICATE_NOT_FOUND: configured certificate file does not exist"
        }
        $ResolvedCertificate = (Resolve-Path -LiteralPath $CertificatePath).Path
    }

    $ResolvedTool = Find-RelayDeskSignTool -ExplicitPath $SignToolPath
    if ([string]::IsNullOrWhiteSpace($ResolvedTool)) {
        throw "WIN_SIGNTOOL_NOT_FOUND: signing was requested but SignTool is unavailable"
    }

    [pscustomobject]@{
        Enabled = $true
        Status = "signed"
        SignToolPath = $ResolvedTool
        CertificatePath = $ResolvedCertificate
        CertificateThumbprint = if ($CertificateThumbprint) { $CertificateThumbprint.Trim() } else { "" }
        CertificatePassword = $CertificatePassword
        TimestampUrl = if ($TimestampUrl) { $TimestampUrl.Trim() } else { "" }
    }
}

function ConvertFrom-RelayDeskSecureString {
    param([SecureString]$Value)
    if ($null -eq $Value) { return $null }
    $Pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($Pointer) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($Pointer) }
}

function Invoke-RelayDeskSignTool {
    param(
        [Parameter(Mandatory = $true)]$Plan,
        [Parameter(Mandatory = $true)][string[]]$Files,
        [switch]$VerifyOnly
    )

    if (-not $Plan.Enabled) { return }
    foreach ($File in $Files) {
        $ResolvedFile = (Resolve-Path -LiteralPath $File -ErrorAction Stop).Path
        $PlainPassword = $null
        try {
            if ($VerifyOnly) {
                $Arguments = @("verify", "/pa", "/all", $ResolvedFile)
            }
            else {
                $Arguments = @("sign", "/fd", "SHA256")
                if ($Plan.CertificatePath) {
                    $Arguments += @("/f", $Plan.CertificatePath)
                    $PlainPassword = ConvertFrom-RelayDeskSecureString $Plan.CertificatePassword
                    if (-not [string]::IsNullOrEmpty($PlainPassword)) { $Arguments += @("/p", $PlainPassword) }
                }
                else {
                    $Arguments += @("/sha1", $Plan.CertificateThumbprint, "/s", "My")
                }
                if (-not [string]::IsNullOrWhiteSpace($Plan.TimestampUrl)) {
                    $Arguments += @("/tr", $Plan.TimestampUrl, "/td", "SHA256")
                }
                $Arguments += $ResolvedFile
            }

            $Output = & $Plan.SignToolPath @Arguments 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0) {
                $SafeOutput = Protect-RelayDeskSigningText -Text $Output -SensitiveValues @(
                    $PlainPassword, $Plan.CertificatePath, $Plan.CertificateThumbprint
                )
                throw "WIN_SIGNTOOL_FAILED: $([IO.Path]::GetFileName($ResolvedFile)): $($SafeOutput.Trim())"
            }
            $Action = if ($VerifyOnly) { "VERIFIED_SIGNED_FILE" } else { "SIGNED_FILE" }
            Write-Host "$Action=$([IO.Path]::GetFileName($ResolvedFile))"
        }
        finally {
            $PlainPassword = $null
            $Arguments = $null
        }
    }
}

function Get-RelayDeskWindowsSignablePayloads {
    param([Parameter(Mandatory = $true)][string]$BuildDir)
    $BinDir = Join-Path $BuildDir "bin"
    if (-not (Test-Path -LiteralPath $BinDir -PathType Container)) {
        throw "WIN_SIGN_PAYLOAD_MISSING: build bin directory does not exist"
    }
    return @(Get-ChildItem -LiteralPath $BinDir -File -Recurse | Where-Object {
        $_.Extension -in @(".exe", ".dll")
    } | Sort-Object FullName | Select-Object -ExpandProperty FullName)
}

Export-ModuleMember -Function @(
    "Get-RelayDeskWindowsSignablePayloads",
    "Invoke-RelayDeskSignTool",
    "New-RelayDeskWindowsSigningPlan",
    "Protect-RelayDeskSigningText"
)
