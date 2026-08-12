$ErrorActionPreference = "Stop"
$Module = Join-Path (Split-Path -Parent $PSScriptRoot) "WindowsSigning.psm1"
Import-Module $Module -Force

$Unsigned = New-RelayDeskWindowsSigningPlan
if ($Unsigned.Enabled -or $Unsigned.Status -ne "unsigned") {
    throw "missing credentials must produce an unsigned plan"
}

$Protected = Protect-RelayDeskSigningText `
    -Text "password=secret certificate=C:\secret\relaydesk.pfx thumbprint=AABBCC" `
    -SensitiveValues @("secret", "C:\secret\relaydesk.pfx", "AABBCC")
if ($Protected -match "secret|AABBCC") { throw "sensitive signing text was not masked" }

$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("relaydesk-win004-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $TestRoot | Out-Null
    $Certificate = Join-Path $TestRoot "private-certificate.pfx"
    $Payload = Join-Path $TestRoot "relaydesk.exe"
    $FakeSignTool = Join-Path $TestRoot "fake-signtool.cmd"
    New-Item -ItemType File -Path $Certificate, $Payload | Out-Null
    "@echo arguments=%* 1>&2`r`n@exit /b 7" | Set-Content -LiteralPath $FakeSignTool -Encoding Ascii
    $PasswordText = "never-print-this-password"
    $Password = ConvertTo-SecureString $PasswordText -AsPlainText -Force
    $Plan = New-RelayDeskWindowsSigningPlan `
        -CertificatePath $Certificate `
        -CertificatePassword $Password `
        -SignToolPath $FakeSignTool

    $SafeFailure = $false
    try {
        Invoke-RelayDeskSignTool -Plan $Plan -Files @($Payload)
    }
    catch {
        $Message = $_.Exception.Message
        $SafeFailure = $Message -like "WIN_SIGNTOOL_FAILED:*" -and
            $Message -notmatch [regex]::Escape($PasswordText) -and
            $Message -notmatch [regex]::Escape($Certificate)
    }
    if (-not $SafeFailure) { throw "SignTool failure did not redact secrets" }

    $AmbiguousFailed = $false
    try {
        New-RelayDeskWindowsSigningPlan `
            -CertificatePath $Certificate `
            -CertificateThumbprint "AABBCC" `
            -SignToolPath $FakeSignTool | Out-Null
    }
    catch { $AmbiguousFailed = $_.Exception.Message -like "WIN_SIGN_IDENTITY_AMBIGUOUS:*" }
    if (-not $AmbiguousFailed) { throw "ambiguous signing identity was not rejected" }

    Write-Host "WIN004_SIGNING_TEST=PASS"
}
finally {
    if (Test-Path -LiteralPath $TestRoot) {
        [System.IO.Directory]::Delete($TestRoot, $true)
    }
}
exit 0
