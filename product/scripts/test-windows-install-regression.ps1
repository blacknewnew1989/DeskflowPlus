param(
    [Parameter(Mandatory = $true)]
    [string]$MsiPath,
    [Parameter(Mandatory = $true)]
    [string]$PortablePath,
    [string]$RepoRoot = "",
    [string]$ReportPath = "",
    [string]$SevenZipPath = "",
    [switch]$KeepTestRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-RequiredFile {
    param([string]$Path, [string]$Code)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "${Code}: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-BrandValue {
    param([string]$BrandFile, [string]$Name)
    $Text = Get-Content -LiteralPath $BrandFile -Raw
    $Match = [regex]::Match($Text, "set\($([regex]::Escape($Name))\s+`"([^`"]+)`"\)")
    if (-not $Match.Success) { throw "TEST005_BRAND_VALUE_MISSING: $Name" }
    return $Match.Groups[1].Value
}

function Normalize-GuidText {
    param([string]$Value)
    return ([guid]$Value.Trim("{}" )).ToString("D").ToUpperInvariant()
}

function Get-WixAttribute {
    param([string]$Text, [string]$Name)
    $Match = [regex]::Match($Text, "$([regex]::Escape($Name))\s*=\s*`"([^`"]+)`"")
    if (-not $Match.Success) { throw "TEST005_MSI_ATTRIBUTE_MISSING: $Name" }
    return $Match.Groups[1].Value
}

function Invoke-CheckedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 120
    )
    $Process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Process.Id -Force
        throw "TEST005_PROCESS_TIMEOUT: $FilePath"
    }
    if ($AllowedExitCodes -notcontains $Process.ExitCode) {
        throw "TEST005_PROCESS_FAILED: $FilePath exit=$($Process.ExitCode)"
    }
    return $Process.ExitCode
}

function Find-SevenZip {
    param([string]$Requested)
    $Candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($Requested)) { $Candidates += $Requested }
    $Command = Get-Command 7z -ErrorAction SilentlyContinue
    if ($Command) { $Candidates += $Command.Source }
    $Candidates += @(
        "$env:ProgramFiles\7-Zip\7z.exe",
        "$env:LOCALAPPDATA\Programs\7-Zip\7z.exe"
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }
    throw "TEST005_7ZIP_MISSING: install or pass -SevenZipPath"
}

function Expand-PortableArchive {
    param([string]$Archive, [string]$Destination, [string]$SevenZip)
    if ($Archive.EndsWith(".zip", [StringComparison]::OrdinalIgnoreCase)) {
        Expand-Archive -LiteralPath $Archive -DestinationPath $Destination
        return
    }
    if (-not $Archive.EndsWith(".7z", [StringComparison]::OrdinalIgnoreCase)) {
        throw "TEST005_PORTABLE_FORMAT_UNSUPPORTED: $Archive"
    }
    Invoke-CheckedProcess -FilePath $SevenZip -Arguments @("x", "-y", "-o$Destination", $Archive) | Out-Null
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (& git rev-parse --show-toplevel).Trim()
}
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$MsiPath = Resolve-RequiredFile -Path $MsiPath -Code "TEST005_MSI_MISSING"
$PortablePath = Resolve-RequiredFile -Path $PortablePath -Code "TEST005_PORTABLE_MISSING"

$BrandFile = Join-Path $RepoRoot "product\branding\RelayDeskBrand.cmake"
$ExpectedProductName = Get-BrandValue -BrandFile $BrandFile -Name "RELAYDESK_PRODUCT_NAME"
$ExpectedVendor = Get-BrandValue -BrandFile $BrandFile -Name "RELAYDESK_VENDOR_NAME"
$ExpectedUpgradeCode = Normalize-GuidText (Get-BrandValue -BrandFile $BrandFile -Name "RELAYDESK_WINDOWS_WIX_UPGRADE_GUID")

if ([IO.Path]::GetFileName($MsiPath) -notmatch '-unsigned\.msi$') {
    throw "TEST005_MSI_UNSIGNED_NAME_REQUIRED: $MsiPath"
}
if ([IO.Path]::GetFileName($PortablePath) -notmatch '-unsigned-portable\.(7z|zip)$') {
    throw "TEST005_PORTABLE_UNSIGNED_NAME_REQUIRED: $PortablePath"
}
$MsiSignature = Get-AuthenticodeSignature -LiteralPath $MsiPath
if ($MsiSignature.Status -ne [System.Management.Automation.SignatureStatus]::NotSigned) {
    throw "TEST005_MSI_SIGNATURE_UNEXPECTED: $($MsiSignature.Status)"
}

$Wix = Get-Command wix -ErrorAction Stop
$Msiexec = Join-Path $env:SystemRoot "System32\msiexec.exe"
$TestRoot = Join-Path ([IO.Path]::GetTempPath()) ("relaydesk-test005-" + [guid]::NewGuid().ToString("N"))
$ExpectedTempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$ResolvedTestRoot = [IO.Path]::GetFullPath($TestRoot)
if (-not $ResolvedTestRoot.StartsWith($ExpectedTempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($ResolvedTestRoot) -notmatch '^relaydesk-test005-[0-9a-f]{32}$') {
    throw "TEST005_UNSAFE_TEST_ROOT: $ResolvedTestRoot"
}

$AdminRoot = Join-Path $TestRoot "admin-image"
$PortableRoot = Join-Path $TestRoot "portable"
$DecompileRoot = Join-Path $TestRoot "decompiled"
$UserDataRoot = Join-Path $TestRoot "user-data\RelayDesk"
$Sentinel = Join-Path $UserDataRoot "preserve-me.txt"
$FirstLog = Join-Path $TestRoot "msiexec-admin-clean.log"
$RefreshLog = Join-Path $TestRoot "msiexec-admin-refresh.log"
$WxsPath = Join-Path $DecompileRoot "package.wxs"

$Result = [ordered]@{
    task = "TEST-005-windows"
    msi = [IO.Path]::GetFileName($MsiPath)
    portable = [IO.Path]::GetFileName($PortablePath)
    signatureStatus = "unsigned"
    wixValidate = "NOT_RUN"
    identity = "NOT_RUN"
    administrativeInstall = "NOT_RUN"
    sameVersionAdministrativeRefresh = "NOT_RUN"
    administrativeImageRemoval = "NOT_RUN"
    portableDependencyLaunch = "NOT_RUN"
    systemInstall = "NOT_RUN: MSI installs an automatic service and firewall rules"
    systemRepairOrUpgrade = "NOT_RUN: requires a registered system product"
    systemUninstall = "NOT_RUN: no system product was registered"
    userDataPreserved = "NOT_RUN"
}

try {
    New-Item -ItemType Directory -Path $AdminRoot, $PortableRoot, $DecompileRoot, $UserDataRoot | Out-Null
    Set-Content -LiteralPath $Sentinel -Value "TEST-005 user data sentinel" -Encoding UTF8

    Invoke-CheckedProcess -FilePath $Wix.Source -Arguments @("msi", "validate", $MsiPath) | Out-Null
    $Result.wixValidate = "PASS"
    Invoke-CheckedProcess -FilePath $Wix.Source -Arguments @(
        "msi", "decompile", "-o", $WxsPath, "-x", (Join-Path $DecompileRoot "payload"), $MsiPath
    ) | Out-Null

    $Wxs = Get-Content -LiteralPath $WxsPath -Raw
    $ProductName = Get-WixAttribute -Text $Wxs -Name "Name"
    $Manufacturer = Get-WixAttribute -Text $Wxs -Name "Manufacturer"
    $UpgradeCode = Normalize-GuidText (Get-WixAttribute -Text $Wxs -Name "UpgradeCode")
    $ProductCode = Normalize-GuidText (Get-WixAttribute -Text $Wxs -Name "ProductCode")
    if ($ProductName -ne $ExpectedProductName) { throw "TEST005_PRODUCT_NAME_MISMATCH: $ProductName" }
    if ($Manufacturer -ne $ExpectedVendor) { throw "TEST005_VENDOR_MISMATCH: $Manufacturer" }
    if ($UpgradeCode -ne $ExpectedUpgradeCode) { throw "TEST005_UPGRADE_CODE_MISMATCH: $UpgradeCode" }
    if ($Wxs -notmatch '<ServiceInstall\b' -or $Wxs -notmatch '<firewall:FirewallException\b') {
        throw "TEST005_MACHINE_MUTATION_DETECTION_FAILED"
    }
    if ($Wxs -match '(?i)(LocalAppDataFolder|AppDataFolder|PersonalFolder).{0,300}(RemoveFolder|RemoveFile)') {
        throw "TEST005_USER_DATA_REMOVAL_DETECTED"
    }
    $Result.identity = "PASS product=$ProductName productCode=$ProductCode upgradeCode=$UpgradeCode"

    Invoke-CheckedProcess -FilePath $Msiexec -Arguments @(
        "/a", $MsiPath, "TARGETDIR=$AdminRoot", "/qn", "/norestart", "/l*v", $FirstLog
    ) -AllowedExitCodes @(0, 3010) | Out-Null
    $InstalledGui = Get-ChildItem -LiteralPath $AdminRoot -Recurse -Filter "deskflow.exe" | Select-Object -First 1
    $InstalledCore = Get-ChildItem -LiteralPath $AdminRoot -Recurse -Filter "deskflow-core.exe" | Select-Object -First 1
    if (-not $InstalledGui -or -not $InstalledCore) { throw "TEST005_ADMIN_IMAGE_EXECUTABLES_MISSING" }
    $Result.administrativeInstall = "PASS"

    $RefreshMarker = Join-Path $AdminRoot "test005-refresh-marker.txt"
    Set-Content -LiteralPath $RefreshMarker -Value "same-version refresh marker" -Encoding UTF8
    Invoke-CheckedProcess -FilePath $Msiexec -Arguments @(
        "/a", $MsiPath, "TARGETDIR=$AdminRoot", "/qn", "/norestart", "/l*v", $RefreshLog
    ) -AllowedExitCodes @(0, 3010) | Out-Null
    if (-not (Test-Path -LiteralPath $RefreshMarker -PathType Leaf)) {
        throw "TEST005_ADMIN_REFRESH_REMOVED_UNRELATED_FILE"
    }
    $Result.sameVersionAdministrativeRefresh = "PASS"

    $SevenZip = if ($PortablePath.EndsWith(".7z", [StringComparison]::OrdinalIgnoreCase)) {
        Find-SevenZip -Requested $SevenZipPath
    } else { "" }
    Expand-PortableArchive -Archive $PortablePath -Destination $PortableRoot -SevenZip $SevenZip
    $PortableGui = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow.exe" | Select-Object -First 1
    $PortableCore = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow-core.exe" | Select-Object -First 1
    $PortableSettings = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "RelayDesk.conf" | Select-Object -First 1
    $PortableDaemon = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow-daemon.exe" | Select-Object -First 1
    if (-not $PortableGui -or -not $PortableCore -or -not $PortableSettings) {
        throw "TEST005_PORTABLE_LAYOUT_INVALID"
    }
    if ($PortableDaemon) { throw "TEST005_PORTABLE_DAEMON_PRESENT" }
    Invoke-CheckedProcess -FilePath $PortableCore.FullName -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    Invoke-CheckedProcess -FilePath $PortableGui.FullName -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    $Result.portableDependencyLaunch = "PASS"

    [IO.Directory]::Delete($AdminRoot, $true)
    if (Test-Path -LiteralPath $AdminRoot) { throw "TEST005_ADMIN_IMAGE_REMOVE_FAILED" }
    $Result.administrativeImageRemoval = "PASS"
    if (-not (Test-Path -LiteralPath $Sentinel -PathType Leaf)) {
        throw "TEST005_USER_DATA_SENTINEL_REMOVED"
    }
    $Result.userDataPreserved = "PASS external user-data sentinel survived image refresh/removal"
}
finally {
    $Result.testRootRetained = [bool]$KeepTestRoot
    if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
        $ReportParent = Split-Path -Parent $ReportPath
        if (-not [string]::IsNullOrWhiteSpace($ReportParent)) {
            New-Item -ItemType Directory -Path $ReportParent -Force | Out-Null
        }
        $Result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
    }
    $Result | ConvertTo-Json -Depth 4
    if (-not $KeepTestRoot -and (Test-Path -LiteralPath $TestRoot)) {
        [IO.Directory]::Delete($TestRoot, $true)
    }
}

