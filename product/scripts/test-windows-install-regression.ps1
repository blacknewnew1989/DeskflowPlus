param(
    [Parameter(Mandatory = $true)]
    [string]$MsiPath,
    [Parameter(Mandatory = $true)]
    [string]$PortablePath,
    [string]$RepoRoot = "",
    [string]$ReportPath = "",
    [string]$SevenZipPath = "",
    [string]$PreviousMsiPath = "",
    [switch]$GeneratePreviousPackage,
    [switch]$AllowSystemInstall,
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

function Format-MsiGuid {
    param([guid]$Value)
    return "{$($Value.ToString('D').ToUpperInvariant())}"
}

function Get-WixPackageAttribute {
    param([string]$Text, [string]$Name)
    $PackageMatch = [regex]::Match($Text, '<Package\b[^>]*>')
    if (-not $PackageMatch.Success) { throw "TEST005_MSI_PACKAGE_MISSING" }
    $AttributeMatch = [regex]::Match(
        $PackageMatch.Value,
        "$([regex]::Escape($Name))\s*=\s*`"([^`"]+)`""
    )
    if (-not $AttributeMatch.Success) { throw "TEST005_MSI_ATTRIBUTE_MISSING: $Name" }
    return $AttributeMatch.Groups[1].Value
}

function Quote-ProcessArgument {
    param([string]$Value)
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-CheckedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [int[]]$AllowedExitCodes = @(0),
        [int]$TimeoutSeconds = 180
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

function Invoke-MsiExec {
    param(
        [string[]]$Arguments,
        [string]$LogPath,
        [int[]]$AllowedExitCodes = @(0, 3010)
    )
    $MsiArguments = @($Arguments) + @(
        "/qn",
        "/norestart",
        "/l*v",
        (Quote-ProcessArgument $LogPath)
    )
    return Invoke-CheckedProcess `
        -FilePath $script:Msiexec `
        -Arguments $MsiArguments `
        -AllowedExitCodes $AllowedExitCodes `
        -TimeoutSeconds 300
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
    Invoke-CheckedProcess -FilePath $SevenZip -Arguments @(
        "x", "-y", "-o$Destination", (Quote-ProcessArgument $Archive)
    ) | Out-Null
}

function Get-MsiProperty {
    param([string]$Path, [string]$PropertyName)
    $Installer = $null
    $Database = $null
    $View = $null
    $Record = $null
    try {
        $Installer = New-Object -ComObject WindowsInstaller.Installer
        $Database = $Installer.OpenDatabase($Path, 0)
        $View = $Database.OpenView(
            "SELECT `Value` FROM `Property` WHERE `Property` = '$PropertyName'"
        )
        $View.Execute() | Out-Null
        $Record = $View.Fetch()
        if ($null -eq $Record) { throw "TEST005_MSI_PROPERTY_MISSING: $PropertyName" }
        return [string]$Record.StringData(1)
    }
    finally {
        if ($null -ne $Record) {
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Record) | Out-Null
        }
        if ($null -ne $View) {
            $View.Close() | Out-Null
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($View) | Out-Null
        }
        if ($null -ne $Database) {
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Database) | Out-Null
        }
        if ($null -ne $Installer) {
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Installer) | Out-Null
        }
    }
}

function Get-MsiIdentity {
    param([string]$Path)
    return [pscustomobject]@{
        ProductName = Get-MsiProperty -Path $Path -PropertyName "ProductName"
        Manufacturer = Get-MsiProperty -Path $Path -PropertyName "Manufacturer"
        ProductCode = Normalize-GuidText (Get-MsiProperty -Path $Path -PropertyName "ProductCode")
        ProductVersion = Get-MsiProperty -Path $Path -PropertyName "ProductVersion"
        UpgradeCode = Normalize-GuidText (Get-MsiProperty -Path $Path -PropertyName "UpgradeCode")
    }
}

function Get-LowerMsiVersion {
    param([string]$CurrentVersion)
    $Current = [version]$CurrentVersion
    if ($Current.Build -gt 0) {
        return "$($Current.Major).$($Current.Minor).$($Current.Build - 1)"
    }
    if ($Current.Minor -gt 0) {
        return "$($Current.Major).$($Current.Minor - 1).0"
    }
    if ($Current.Major -gt 0) {
        return "$($Current.Major - 1).0.0"
    }
    throw "TEST005_CANNOT_DERIVE_PREVIOUS_VERSION: $CurrentVersion"
}

function New-SyntheticPreviousMsi {
    param([string]$SourcePath, [string]$DestinationPath, [string]$CurrentVersion)
    [IO.File]::Copy($SourcePath, $DestinationPath, $true)
    $PreviousVersion = Get-LowerMsiVersion -CurrentVersion $CurrentVersion
    $PreviousProductCode = Format-MsiGuid ([guid]::NewGuid())
    $PreviousPackageCode = Format-MsiGuid ([guid]::NewGuid())
    $Installer = $null
    $Database = $null
    $Summary = $null
    try {
        $Installer = New-Object -ComObject WindowsInstaller.Installer
        $Database = $Installer.OpenDatabase($DestinationPath, 1)
        foreach ($Update in @(
            @{ Property = "ProductCode"; Value = $PreviousProductCode },
            @{ Property = "ProductVersion"; Value = $PreviousVersion }
        )) {
            $View = $null
            try {
                $View = $Database.OpenView(
                    "UPDATE `Property` SET `Value` = '$($Update.Value)' " +
                    "WHERE `Property` = '$($Update.Property)'"
                )
                $View.Execute() | Out-Null
                $View.Close() | Out-Null
            }
            finally {
                if ($null -ne $View) {
                    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($View) | Out-Null
                }
            }
        }
        $Database.Commit() | Out-Null
        [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Database) | Out-Null
        $Database = $null

        $Summary = $Installer.SummaryInformation($DestinationPath, 20)
        try {
            $Summary.Property(9) = $PreviousPackageCode
            $Summary.Persist() | Out-Null
        }
        finally {
            if ($null -ne $Summary) {
                [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Summary) | Out-Null
                $Summary = $null
            }
        }
    }
    finally {
        if ($null -ne $Database) {
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Database) | Out-Null
        }
        if ($null -ne $Installer) {
            [Runtime.InteropServices.Marshal]::FinalReleaseComObject($Installer) | Out-Null
        }
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
    }
    return Get-MsiIdentity -Path $DestinationPath
}

function Get-ProductRegistration {
    param([string]$ProductCode)
    $Code = Format-MsiGuid ([guid]$ProductCode)
    $Candidates = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$Code",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$Code"
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            $Entry = Get-ItemProperty -LiteralPath $Candidate
            $DisplayName = $Entry.PSObject.Properties["DisplayName"]
            $DisplayVersion = $Entry.PSObject.Properties["DisplayVersion"]
            $InstallLocation = $Entry.PSObject.Properties["InstallLocation"]
            $UninstallString = $Entry.PSObject.Properties["UninstallString"]
            return [pscustomobject]@{
                RegistryPath = $Candidate
                DisplayName = if ($null -eq $DisplayName) { "" } else { [string]$DisplayName.Value }
                DisplayVersion = if ($null -eq $DisplayVersion) { "" } else { [string]$DisplayVersion.Value }
                InstallLocation = if ($null -eq $InstallLocation) { "" } else { [string]$InstallLocation.Value }
                UninstallString = if ($null -eq $UninstallString) { "" } else { [string]$UninstallString.Value }
            }
        }
    }
    return $null
}

function Get-ProductRegistrationsByName {
    param([string]$ProductName)
    $Matches = @()
    foreach ($Root in @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
    )) {
        if (-not (Test-Path -LiteralPath $Root)) { continue }
        foreach ($Key in Get-ChildItem -LiteralPath $Root -ErrorAction Stop) {
            $Entry = Get-ItemProperty -LiteralPath $Key.PSPath -ErrorAction SilentlyContinue
            $DisplayName = if ($null -eq $Entry) { $null } else { $Entry.PSObject.Properties["DisplayName"] }
            if ($null -ne $DisplayName -and [string]$DisplayName.Value -eq $ProductName) {
                $Matches += $Key.PSChildName
            }
        }
    }
    return @($Matches | Sort-Object -Unique)
}

function Get-RelayDeskFirewallRules {
    param([string]$ProductName)
    $Rules = @()
    foreach ($DisplayName in @("$ProductName Server", "$ProductName Client")) {
        $Rules += @(Get-NetFirewallRule -DisplayName $DisplayName -ErrorAction SilentlyContinue)
    }
    return @($Rules)
}

function Assert-ProductInstalled {
    param([pscustomobject]$Identity)
    $Registration = Get-ProductRegistration -ProductCode $Identity.ProductCode
    if ($null -eq $Registration) {
        throw "TEST005_PRODUCT_NOT_REGISTERED: $($Identity.ProductCode)"
    }
    if ($Registration.DisplayName -ne $Identity.ProductName) {
        throw "TEST005_REGISTERED_NAME_MISMATCH: $($Registration.DisplayName)"
    }
    if ([version]$Registration.DisplayVersion -ne [version]$Identity.ProductVersion) {
        throw "TEST005_REGISTERED_VERSION_MISMATCH: $($Registration.DisplayVersion)"
    }
    if ([string]::IsNullOrWhiteSpace($Registration.InstallLocation)) {
        throw "TEST005_INSTALL_LOCATION_MISSING"
    }
    $InstallRoot = $Registration.InstallLocation.TrimEnd('\')
    foreach ($Executable in @("deskflow.exe", "deskflow-core.exe", "deskflow-daemon.exe")) {
        if (-not (Test-Path -LiteralPath (Join-Path $InstallRoot $Executable) -PathType Leaf)) {
            throw "TEST005_INSTALLED_EXECUTABLE_MISSING: $Executable"
        }
    }
    return [pscustomobject]@{
        Registration = $Registration
        InstallRoot = $InstallRoot
    }
}

function Assert-ServiceInstalled {
    param([string]$ServiceName, [string]$InstallRoot)
    $Deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $Service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        if ($null -ne $Service -and $Service.Status -eq [ServiceProcess.ServiceControllerStatus]::Running) {
            break
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    if ($null -eq $Service) { throw "TEST005_SERVICE_NOT_REGISTERED: $ServiceName" }
    if ($Service.Status -ne [ServiceProcess.ServiceControllerStatus]::Running) {
        throw "TEST005_SERVICE_NOT_RUNNING: $($Service.Status)"
    }
    $CimService = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
    if ($null -eq $CimService) { throw "TEST005_SERVICE_CIM_MISSING: $ServiceName" }
    if ([string]$CimService.StartMode -ne "Auto") {
        throw "TEST005_SERVICE_START_MODE_MISMATCH: $($CimService.StartMode)"
    }
    $ExpectedDaemon = Join-Path $InstallRoot "deskflow-daemon.exe"
    if (-not ([string]$CimService.PathName).Contains($ExpectedDaemon, [StringComparison]::OrdinalIgnoreCase)) {
        throw "TEST005_SERVICE_PATH_MISMATCH: $($CimService.PathName)"
    }
}

function Assert-FirewallInstalled {
    param([string]$ProductName, [string]$InstallRoot)
    $Rules = Get-RelayDeskFirewallRules -ProductName $ProductName
    if ($Rules.Count -ne 2) { throw "TEST005_FIREWALL_RULE_COUNT: $($Rules.Count)" }
    foreach ($Rule in $Rules) {
        if ([string]$Rule.Enabled -ne "True") {
            throw "TEST005_FIREWALL_RULE_DISABLED: $($Rule.DisplayName)"
        }
        if ([string]$Rule.Direction -ne "Inbound") {
            throw "TEST005_FIREWALL_DIRECTION_MISMATCH: $($Rule.DisplayName)"
        }
        if (-not ([string]$Rule.Profile).Contains("Private", [StringComparison]::OrdinalIgnoreCase)) {
            throw "TEST005_FIREWALL_PROFILE_MISMATCH: $($Rule.DisplayName)"
        }
        $Application = Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $Rule
        $ExpectedCore = Join-Path $InstallRoot "deskflow-core.exe"
        if (-not ([string]$Application.Program).Equals($ExpectedCore, [StringComparison]::OrdinalIgnoreCase)) {
            throw "TEST005_FIREWALL_PROGRAM_MISMATCH: $($Application.Program)"
        }
    }
}

function Assert-UserDataPreserved {
    param(
        [hashtable]$ExpectedHashes,
        [string]$PreExistingConfigPath = "",
        [byte[]]$PreExistingConfigBytes = $null
    )
    foreach ($Path in $ExpectedHashes.Keys) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "TEST005_USER_DATA_SENTINEL_REMOVED: $Path"
        }
        $ActualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($ActualHash -ne $ExpectedHashes[$Path]) {
            throw "TEST005_USER_DATA_SENTINEL_CHANGED: $Path"
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($PreExistingConfigPath)) {
        if (-not (Test-Path -LiteralPath $PreExistingConfigPath -PathType Leaf)) {
            throw "TEST005_PREEXISTING_CONFIG_REMOVED: $PreExistingConfigPath"
        }
        $ActualConfigBytes = [IO.File]::ReadAllBytes($PreExistingConfigPath)
        if (-not (Test-ByteSubsequence -Container $ActualConfigBytes -Candidate $PreExistingConfigBytes)) {
            throw "TEST005_PREEXISTING_CONFIG_CONTENT_REMOVED: $PreExistingConfigPath"
        }
    }
}

function Test-ByteSubsequence {
    param([byte[]]$Container, [byte[]]$Candidate)
    if ($null -eq $Candidate -or $Candidate.Length -eq 0) { return $true }
    if ($null -eq $Container -or $Container.Length -lt $Candidate.Length) { return $false }
    $LastStart = $Container.Length - $Candidate.Length
    for ($Start = 0; $Start -le $LastStart; ++$Start) {
        $Matches = $true
        for ($Offset = 0; $Offset -lt $Candidate.Length; ++$Offset) {
            if ($Container[$Start + $Offset] -ne $Candidate[$Offset]) {
                $Matches = $false
                break
            }
        }
        if ($Matches) { return $true }
    }
    return $false
}

function Assert-SystemResidueRemoved {
    param(
        [pscustomobject[]]$Identities,
        [string]$ServiceName,
        [string]$ProductName,
        [string[]]$InstallRoots
    )
    foreach ($Identity in $Identities) {
        if ($null -ne (Get-ProductRegistration -ProductCode $Identity.ProductCode)) {
            throw "TEST005_PRODUCT_REGISTRATION_REMAINS: $($Identity.ProductCode)"
        }
    }
    if ($null -ne (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue)) {
        throw "TEST005_SERVICE_REMAINS: $ServiceName"
    }
    $Rules = Get-RelayDeskFirewallRules -ProductName $ProductName
    if ($Rules.Count -ne 0) { throw "TEST005_FIREWALL_RULE_REMAINS: $($Rules.Count)" }
    foreach ($InstallRoot in @($InstallRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)) {
        if (Test-Path -LiteralPath $InstallRoot) {
            throw "TEST005_INSTALL_DIRECTORY_REMAINS: $InstallRoot"
        }
    }
    foreach ($ShortcutRoot in @(
        (Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs\$ProductName"),
        (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$ProductName")
    )) {
        if (Test-Path -LiteralPath $ShortcutRoot) {
            throw "TEST005_START_MENU_RESIDUE: $ShortcutRoot"
        }
    }
}

function Invoke-ProductUninstall {
    param([pscustomobject]$Identity, [string]$LogPath, [switch]$AllowMissing)
    $Allowed = if ($AllowMissing) { @(0, 1605, 1614, 3010) } else { @(0, 3010) }
    Invoke-MsiExec -Arguments @(
        "/x", (Format-MsiGuid ([guid]$Identity.ProductCode))
    ) -LogPath $LogPath -AllowedExitCodes $Allowed | Out-Null
}

if (-not $AllowSystemInstall) {
    throw "TEST005_SYSTEM_INSTALL_OPT_IN_REQUIRED: pass -AllowSystemInstall only on a disposable Windows runner"
}
if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw "TEST005_WINDOWS_REQUIRED"
}
$WindowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$WindowsPrincipal = [Security.Principal.WindowsPrincipal]::new($WindowsIdentity)
if (-not $WindowsPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "TEST005_ADMINISTRATOR_REQUIRED"
}
if (-not [string]::IsNullOrWhiteSpace($PreviousMsiPath) -and $GeneratePreviousPackage) {
    throw "TEST005_PREVIOUS_PACKAGE_MODE_CONFLICT"
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
$ExpectedServiceName = Get-BrandValue -BrandFile $BrandFile -Name "RELAYDESK_WINDOWS_SERVICE_NAME"

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
$script:Msiexec = Join-Path $env:SystemRoot "System32\msiexec.exe"
$TestRoot = Join-Path ([IO.Path]::GetTempPath()) ("relaydesk-test005-" + [guid]::NewGuid().ToString("N"))
$ExpectedTempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$ResolvedTestRoot = [IO.Path]::GetFullPath($TestRoot)
if (-not $ResolvedTestRoot.StartsWith($ExpectedTempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($ResolvedTestRoot) -notmatch '^relaydesk-test005-[0-9a-f]{32}$') {
    throw "TEST005_UNSAFE_TEST_ROOT: $ResolvedTestRoot"
}

$PortableRoot = Join-Path $TestRoot "portable"
$DecompileRoot = Join-Path $TestRoot "decompiled"
$WxsPath = Join-Path $DecompileRoot "package.wxs"
$EvidenceRoot = if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    Join-Path $TestRoot "evidence"
} else {
    $ReportPath = [IO.Path]::GetFullPath($ReportPath)
    Split-Path -Parent $ReportPath
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { $EvidenceRoot = $TestRoot }

$Logs = [ordered]@{
    cleanInstall = Join-Path $EvidenceRoot "test005-msiexec-clean-install.log"
    repair = Join-Path $EvidenceRoot "test005-msiexec-repair.log"
    cleanUninstall = Join-Path $EvidenceRoot "test005-msiexec-clean-uninstall.log"
    previousInstall = Join-Path $EvidenceRoot "test005-msiexec-previous-install.log"
    majorUpgrade = Join-Path $EvidenceRoot "test005-msiexec-major-upgrade.log"
    upgradedUninstall = Join-Path $EvidenceRoot "test005-msiexec-upgraded-uninstall.log"
    emergencyCurrentUninstall = Join-Path $EvidenceRoot "test005-msiexec-emergency-current-uninstall.log"
    emergencyPreviousUninstall = Join-Path $EvidenceRoot "test005-msiexec-emergency-previous-uninstall.log"
}

$UserDataRoot = Join-Path $env:APPDATA $ExpectedProductName
$UserDataCreatedByHarness = -not (Test-Path -LiteralPath $UserDataRoot)
$UserDataMarkerId = [guid]::NewGuid().ToString("N")
$UserConfigPath = Join-Path $UserDataRoot "$ExpectedProductName.conf"
$UserConfigPreExisted = Test-Path -LiteralPath $UserConfigPath -PathType Leaf
$UserConfigBackup = Join-Path $TestRoot "user-config-before-test005.conf"
$UserConfigOriginalBytes = if ($UserConfigPreExisted) {
    [IO.File]::ReadAllBytes($UserConfigPath)
} else { $null }
$UserDataPaths = @(
    $UserConfigPath,
    (Join-Path $UserDataRoot "test005-$UserDataMarkerId-trusted-devices.json"),
    (Join-Path $UserDataRoot "test005-$UserDataMarkerId-transfer-history.json")
)
$UserDataHashes = @{}
$UserDataFilesCreatedByHarness = @()
$CandidateIdentity = $null
$PreviousIdentity = $null
$InstallRoots = @()
$HadFailure = $false

$Result = [ordered]@{
    task = "TEST-005-windows"
    status = "NOT_RUN"
    msi = [IO.Path]::GetFileName($MsiPath)
    portable = [IO.Path]::GetFileName($PortablePath)
    signatureStatus = "unsigned"
    systemInstallOptIn = "PASS"
    wixValidate = "NOT_RUN"
    identity = "NOT_RUN"
    portableDependencyLaunch = "NOT_RUN"
    cleanSystemInstall = "NOT_RUN"
    productRegistration = "NOT_RUN"
    serviceRegistration = "NOT_RUN"
    firewallRegistration = "NOT_RUN"
    sameVersionRepair = "NOT_RUN"
    cleanUninstall = "NOT_RUN"
    cleanUninstallResidue = "NOT_RUN"
    majorUpgrade = "NOT_RUN"
    upgradedUninstall = "NOT_RUN"
    upgradedUninstallResidue = "NOT_RUN"
    userDataPreserved = "NOT_RUN"
    preexistingConfigPreserved = "NOT_RUN"
    unrelatedUserDataHashPreserved = "NOT_RUN"
    interactiveUnsignedWarning = "NOT_RUN"
    interactiveUnsignedWarningNote = "quiet runner install cannot observe SmartScreen or interactive UAC UI"
    userConfigMode = if ($UserConfigPreExisted) { "backup-append-restore" } else { "created-for-test" }
    previousPackageSource = "NOT_RUN"
    logs = $Logs
}

try {
    New-Item -ItemType Directory -Path $PortableRoot, $DecompileRoot, $EvidenceRoot -Force | Out-Null
    $ExistingRegistrations = @(Get-ProductRegistrationsByName -ProductName $ExpectedProductName)
    if ($ExistingRegistrations.Count -ne 0) {
        throw "TEST005_EXISTING_PRODUCT_REFUSED: $ExpectedProductName"
    }
    if ($null -ne (Get-Service -Name $ExpectedServiceName -ErrorAction SilentlyContinue)) {
        throw "TEST005_EXISTING_SERVICE_REFUSED: $ExpectedServiceName"
    }
    $ExistingFirewallRules = @(Get-RelayDeskFirewallRules -ProductName $ExpectedProductName)
    if ($ExistingFirewallRules.Count -ne 0) {
        throw "TEST005_EXISTING_FIREWALL_RULE_REFUSED: $ExpectedProductName"
    }
    foreach ($UserDataPath in $UserDataPaths[1..2]) {
        if (Test-Path -LiteralPath $UserDataPath) {
            throw "TEST005_EXISTING_USER_DATA_REFUSED: $UserDataPath"
        }
    }

    New-Item -ItemType Directory -Path $UserDataRoot -Force | Out-Null
    if ($UserConfigPreExisted) {
        [IO.File]::Copy($UserConfigPath, $UserConfigBackup, $true)
        Add-Content -LiteralPath $UserConfigPath -Value @(
            "", "[test005-$UserDataMarkerId]", "sentinel=$UserDataMarkerId"
        ) -Encoding UTF8
    }
    else {
        Set-Content -LiteralPath $UserConfigPath -Value @(
            "[test005-$UserDataMarkerId]", "sentinel=$UserDataMarkerId"
        ) -Encoding UTF8
        $UserDataFilesCreatedByHarness += $UserConfigPath
    }
    Set-Content -LiteralPath $UserDataPaths[1] -Value '{"test005":"trusted-device-sentinel"}' -Encoding UTF8
    $UserDataFilesCreatedByHarness += $UserDataPaths[1]
    Set-Content -LiteralPath $UserDataPaths[2] -Value '{"test005":"transfer-history-sentinel"}' -Encoding UTF8
    $UserDataFilesCreatedByHarness += $UserDataPaths[2]
    foreach ($UserDataPath in $UserDataPaths[1..2]) {
        $UserDataHashes[$UserDataPath] = (Get-FileHash -LiteralPath $UserDataPath -Algorithm SHA256).Hash
    }

    Invoke-CheckedProcess -FilePath $Wix.Source -Arguments @(
        "msi", "validate", (Quote-ProcessArgument $MsiPath)
    ) | Out-Null
    $Result.wixValidate = "PASS"
    Invoke-CheckedProcess -FilePath $Wix.Source -Arguments @(
        "msi", "decompile", "-o", (Quote-ProcessArgument $WxsPath),
        "-x", (Quote-ProcessArgument (Join-Path $DecompileRoot "payload")),
        (Quote-ProcessArgument $MsiPath)
    ) | Out-Null

    $Wxs = Get-Content -LiteralPath $WxsPath -Raw
    $ProductName = Get-WixPackageAttribute -Text $Wxs -Name "Name"
    $Manufacturer = Get-WixPackageAttribute -Text $Wxs -Name "Manufacturer"
    $UpgradeCode = Normalize-GuidText (Get-WixPackageAttribute -Text $Wxs -Name "UpgradeCode")
    $ProductCode = Normalize-GuidText (Get-WixPackageAttribute -Text $Wxs -Name "ProductCode")
    $ProductVersion = Get-WixPackageAttribute -Text $Wxs -Name "Version"
    if ($ProductName -ne $ExpectedProductName) { throw "TEST005_PRODUCT_NAME_MISMATCH: $ProductName" }
    if ($Manufacturer -ne $ExpectedVendor) { throw "TEST005_VENDOR_MISMATCH: $Manufacturer" }
    if ($UpgradeCode -ne $ExpectedUpgradeCode) { throw "TEST005_UPGRADE_CODE_MISMATCH: $UpgradeCode" }
    if ($Wxs -notmatch '<ServiceInstall\b' -or
        $Wxs -notmatch '<CustomTable\s+Id="Wix[45]FirewallException"') {
        throw "TEST005_MACHINE_MUTATION_DETECTION_FAILED"
    }
    if ($Wxs -notmatch '<MajorUpgrade\b[^>]*AllowSameVersionUpgrades="yes"') {
        throw "TEST005_MAJOR_UPGRADE_POLICY_MISSING"
    }
    if ($Wxs -match '(?i)(LocalAppDataFolder|AppDataFolder|PersonalFolder).{0,300}(RemoveFolder|RemoveFile)') {
        throw "TEST005_USER_DATA_REMOVAL_DETECTED"
    }
    $CandidateIdentity = Get-MsiIdentity -Path $MsiPath
    if ($CandidateIdentity.ProductCode -ne $ProductCode -or
        [version]$CandidateIdentity.ProductVersion -ne [version]$ProductVersion) {
        throw "TEST005_IDENTITY_READBACK_MISMATCH"
    }
    $Result.identity = "PASS"
    $Result.identityEvidence = [ordered]@{
        product = $ProductName
        version = $ProductVersion
        productCode = $ProductCode
        upgradeCode = $UpgradeCode
    }

    $SevenZip = if ($PortablePath.EndsWith(".7z", [StringComparison]::OrdinalIgnoreCase)) {
        Find-SevenZip -Requested $SevenZipPath
    } else { "" }
    Expand-PortableArchive -Archive $PortablePath -Destination $PortableRoot -SevenZip $SevenZip
    $PortableGui = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow.exe" | Select-Object -First 1
    $PortableCore = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow-core.exe" | Select-Object -First 1
    $PortableSettings = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "$ExpectedProductName.conf" | Select-Object -First 1
    $PortableDaemon = Get-ChildItem -LiteralPath $PortableRoot -Recurse -Filter "deskflow-daemon.exe" | Select-Object -First 1
    if (-not $PortableGui -or -not $PortableCore -or -not $PortableSettings) {
        throw "TEST005_PORTABLE_LAYOUT_INVALID"
    }
    if ($PortableDaemon) { throw "TEST005_PORTABLE_DAEMON_PRESENT" }
    Invoke-CheckedProcess -FilePath $PortableCore.FullName -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    Invoke-CheckedProcess -FilePath $PortableGui.FullName -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    $Result.portableDependencyLaunch = "PASS"

    if (-not [string]::IsNullOrWhiteSpace($PreviousMsiPath)) {
        $PreviousMsiPath = Resolve-RequiredFile -Path $PreviousMsiPath -Code "TEST005_PREVIOUS_MSI_MISSING"
        $PreviousIdentity = Get-MsiIdentity -Path $PreviousMsiPath
        $Result.previousPackageSource = "PASS"
        $Result.previousPackageEvidence = "historical package=$([IO.Path]::GetFileName($PreviousMsiPath))"
    }
    elseif ($GeneratePreviousPackage) {
        $PreviousMsiPath = Join-Path $TestRoot "relaydesk-test005-previous-unsigned.msi"
        $PreviousIdentity = New-SyntheticPreviousMsi `
            -SourcePath $MsiPath `
            -DestinationPath $PreviousMsiPath `
            -CurrentVersion $CandidateIdentity.ProductVersion
        $Result.previousPackageSource = "PASS"
        $Result.previousPackageEvidence = "synthetic identity-only predecessor; real MSI payload and major-upgrade engine"
    }
    else {
        throw "TEST005_PREVIOUS_PACKAGE_REQUIRED: pass -PreviousMsiPath or -GeneratePreviousPackage"
    }
    if ($PreviousIdentity.ProductName -ne $CandidateIdentity.ProductName -or
        $PreviousIdentity.Manufacturer -ne $CandidateIdentity.Manufacturer -or
        $PreviousIdentity.UpgradeCode -ne $CandidateIdentity.UpgradeCode) {
        throw "TEST005_PREVIOUS_IDENTITY_MISMATCH"
    }
    if ($PreviousIdentity.ProductCode -eq $CandidateIdentity.ProductCode) {
        throw "TEST005_PREVIOUS_PRODUCT_CODE_NOT_UNIQUE"
    }
    if ([version]$PreviousIdentity.ProductVersion -ge [version]$CandidateIdentity.ProductVersion) {
        throw "TEST005_PREVIOUS_VERSION_NOT_LOWER: $($PreviousIdentity.ProductVersion)"
    }
    if ((Get-AuthenticodeSignature -LiteralPath $PreviousMsiPath).Status -ne
        [System.Management.Automation.SignatureStatus]::NotSigned) {
        throw "TEST005_PREVIOUS_MSI_MUST_BE_UNSIGNED"
    }

    # Lifecycle 1: install the candidate itself on a clean machine, repair it,
    # then uninstall it and prove that only intentionally retained user data remains.
    Invoke-MsiExec -Arguments @(
        "/i", (Quote-ProcessArgument $MsiPath)
    ) -LogPath $Logs.cleanInstall | Out-Null
    $Installed = Assert-ProductInstalled -Identity $CandidateIdentity
    $InstallRoots += $Installed.InstallRoot
    Assert-ServiceInstalled -ServiceName $ExpectedServiceName -InstallRoot $Installed.InstallRoot
    Assert-FirewallInstalled -ProductName $ExpectedProductName -InstallRoot $Installed.InstallRoot
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes
    Invoke-CheckedProcess -FilePath (Join-Path $Installed.InstallRoot "deskflow-core.exe") -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    Invoke-CheckedProcess -FilePath (Join-Path $Installed.InstallRoot "deskflow.exe") -Arguments @("--version") -TimeoutSeconds 20 | Out-Null
    $Result.cleanSystemInstall = "PASS"
    $Result.cleanSystemInstallEvidence = "unsigned MSI installed on disposable runner"
    $Result.productRegistration = "PASS"
    $Result.productRegistrationEvidence = "code=$($CandidateIdentity.ProductCode) location=$($Installed.InstallRoot)"
    $Result.serviceRegistration = "PASS"
    $Result.serviceRegistrationEvidence = "name=$ExpectedServiceName status=Running start=Auto"
    $Result.firewallRegistration = "PASS"
    $Result.firewallRegistrationEvidence = "private inbound server/client rules target installed core"

    Invoke-MsiExec -Arguments @(
        "/i", (Quote-ProcessArgument $MsiPath), "REINSTALL=ALL", "REINSTALLMODE=vomus"
    ) -LogPath $Logs.repair | Out-Null
    $Repaired = Assert-ProductInstalled -Identity $CandidateIdentity
    Assert-ServiceInstalled -ServiceName $ExpectedServiceName -InstallRoot $Repaired.InstallRoot
    Assert-FirewallInstalled -ProductName $ExpectedProductName -InstallRoot $Repaired.InstallRoot
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes
    $Result.sameVersionRepair = "PASS"
    $Result.sameVersionRepairEvidence = "Windows Installer REINSTALL=ALL REINSTALLMODE=vomus"

    Invoke-ProductUninstall -Identity $CandidateIdentity -LogPath $Logs.cleanUninstall
    Assert-SystemResidueRemoved `
        -Identities @($CandidateIdentity) `
        -ServiceName $ExpectedServiceName `
        -ProductName $ExpectedProductName `
        -InstallRoots $InstallRoots
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes
    $Result.cleanUninstall = "PASS"
    $Result.cleanUninstallResidue = "PASS"
    $Result.cleanUninstallResidueEvidence = "product/service/firewall/install-dir/start-menu removed"

    # Lifecycle 2: install a lower-version MSI that has the exact production
    # UpgradeCode but a distinct ProductCode, then exercise a genuine major
    # upgrade to the candidate. A generated predecessor changes only MSI
    # identity/version; callers can pass a historical package instead.
    Invoke-MsiExec -Arguments @(
        "/i", (Quote-ProcessArgument $PreviousMsiPath)
    ) -LogPath $Logs.previousInstall | Out-Null
    $PreviousInstalled = Assert-ProductInstalled -Identity $PreviousIdentity
    $InstallRoots += $PreviousInstalled.InstallRoot
    Assert-ServiceInstalled -ServiceName $ExpectedServiceName -InstallRoot $PreviousInstalled.InstallRoot
    Assert-FirewallInstalled -ProductName $ExpectedProductName -InstallRoot $PreviousInstalled.InstallRoot
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes

    Invoke-MsiExec -Arguments @(
        "/i", (Quote-ProcessArgument $MsiPath)
    ) -LogPath $Logs.majorUpgrade | Out-Null
    if ($null -ne (Get-ProductRegistration -ProductCode $PreviousIdentity.ProductCode)) {
        throw "TEST005_PREVIOUS_PRODUCT_REMAINS_AFTER_UPGRADE"
    }
    $Upgraded = Assert-ProductInstalled -Identity $CandidateIdentity
    Assert-ServiceInstalled -ServiceName $ExpectedServiceName -InstallRoot $Upgraded.InstallRoot
    Assert-FirewallInstalled -ProductName $ExpectedProductName -InstallRoot $Upgraded.InstallRoot
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes
    $Result.majorUpgrade = "PASS"
    $Result.majorUpgradeEvidence = "$($PreviousIdentity.ProductVersion)/$($PreviousIdentity.ProductCode) -> $($CandidateIdentity.ProductVersion)/$($CandidateIdentity.ProductCode), UpgradeCode=$ExpectedUpgradeCode"

    Invoke-ProductUninstall -Identity $CandidateIdentity -LogPath $Logs.upgradedUninstall
    Assert-SystemResidueRemoved `
        -Identities @($CandidateIdentity, $PreviousIdentity) `
        -ServiceName $ExpectedServiceName `
        -ProductName $ExpectedProductName `
        -InstallRoots $InstallRoots
    Assert-UserDataPreserved `
        -ExpectedHashes $UserDataHashes `
        -PreExistingConfigPath $(if ($UserConfigPreExisted) { $UserConfigPath } else { "" }) `
        -PreExistingConfigBytes $UserConfigOriginalBytes
    $Result.upgradedUninstall = "PASS"
    $Result.upgradedUninstallResidue = "PASS"
    $Result.upgradedUninstallResidueEvidence = "product/service/firewall/install-dir/start-menu removed"
    $Result.userDataPreserved = "PASS"
    $Result.userDataPreservedEvidence = "RelayDesk.conf, trust marker, and history marker survived install/repair/upgrade/uninstall"
    $Result.preexistingConfigPreserved = if ($UserConfigPreExisted) { "PASS" } else { "NOT_RUN" }
    $Result.preexistingConfigPreservedEvidence = if ($UserConfigPreExisted) {
        "original RelayDesk.conf bytes remained a contiguous subsequence while the running service could append/update other data"
    } else { "no pre-existing RelayDesk.conf on runner" }
    $Result.unrelatedUserDataHashPreserved = "PASS"
    $Result.unrelatedUserDataHashPreservedEvidence = "unique trust/history marker files retained exact SHA-256 through both lifecycles"
    $Result.status = "PASS"
}
catch {
    $HadFailure = $true
    $Result.status = "FAIL"
    $Result.failure = $_.Exception.Message
    throw
}
finally {
    if ($HadFailure) {
        $CleanupErrors = @()
        foreach ($Cleanup in @(
            @{ Identity = $CandidateIdentity; Log = $Logs.emergencyCurrentUninstall },
            @{ Identity = $PreviousIdentity; Log = $Logs.emergencyPreviousUninstall }
        )) {
            if ($null -eq $Cleanup.Identity) { continue }
            try {
                Invoke-ProductUninstall -Identity $Cleanup.Identity -LogPath $Cleanup.Log -AllowMissing
            }
            catch {
                $CleanupErrors += $_.Exception.Message
            }
        }
        $Result.emergencyCleanup = if ($CleanupErrors.Count -eq 0) { "PASS" } else { "FAIL" }
        if ($CleanupErrors.Count -ne 0) {
            $Result.emergencyCleanupError = $CleanupErrors -join '; '
        }
    }

    $Result.testRootRetained = [bool]$KeepTestRoot
    if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $ReportPath) -Force | Out-Null
        $Result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
    }
    $Result | ConvertTo-Json -Depth 6

    if ($UserConfigPreExisted -and (Test-Path -LiteralPath $UserConfigBackup -PathType Leaf)) {
        [IO.File]::Copy($UserConfigBackup, $UserConfigPath, $true)
    }
    foreach ($UserDataPath in $UserDataFilesCreatedByHarness) {
        if (Test-Path -LiteralPath $UserDataPath -PathType Leaf) {
            [IO.File]::Delete($UserDataPath)
        }
    }
    if ($UserDataCreatedByHarness -and (Test-Path -LiteralPath $UserDataRoot)) {
        if (@(Get-ChildItem -LiteralPath $UserDataRoot -Force).Count -eq 0) {
            [IO.Directory]::Delete($UserDataRoot, $false)
        }
    }
    if (-not $KeepTestRoot -and (Test-Path -LiteralPath $TestRoot)) {
        [IO.Directory]::Delete($TestRoot, $true)
    }
}
