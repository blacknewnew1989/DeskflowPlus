$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$ScriptPath = Join-Path $RepoRoot "product\scripts\test-windows-install-regression.ps1"
$WorkflowPath = Join-Path $RepoRoot ".github\workflows\relaydesk-build.yml"
$Script = Get-Content -LiteralPath $ScriptPath -Raw
$Workflow = Get-Content -LiteralPath $WorkflowPath -Raw

function Assert-Contains {
    param([string]$Text, [string]$Expected, [string]$Code)
    if (-not $Text.Contains($Expected, [StringComparison]::Ordinal)) {
        throw "${Code}: missing $Expected"
    }
}

Assert-Contains $Script '[switch]$AllowSystemInstall' "TEST005_PS_GATE_SWITCH"
Assert-Contains $Script '"/i", (Quote-ProcessArgument $MsiPath)' "TEST005_PS_INSTALL"
Assert-Contains $Script '"REINSTALL=ALL", "REINSTALLMODE=vomus"' "TEST005_PS_REPAIR"
Assert-Contains $Script 'New-SyntheticPreviousMsi' "TEST005_PS_UPGRADE_FIXTURE"
Assert-Contains $Script 'Assert-SystemResidueRemoved' "TEST005_PS_RESIDUE"
Assert-Contains $Script 'Get-NetFirewallRule' "TEST005_PS_FIREWALL"
Assert-Contains $Workflow 'if: runner.os == ''Windows''' "TEST005_PS_WORKFLOW_OS"
Assert-Contains $Workflow '-AllowSystemInstall' "TEST005_PS_WORKFLOW_GATE"
Assert-Contains $Workflow '-GeneratePreviousPackage' "TEST005_PS_WORKFLOW_UPGRADE"

$GateFailure = $null
try {
    & $ScriptPath -MsiPath "missing-test005.msi" -PortablePath "missing-test005.7z"
}
catch {
    $GateFailure = $_.Exception.Message
}
if ([string]::IsNullOrWhiteSpace($GateFailure) -or
    -not $GateFailure.Contains("TEST005_SYSTEM_INSTALL_OPT_IN_REQUIRED", [StringComparison]::Ordinal)) {
    throw "TEST005_PS_GATE_EXECUTION: expected opt-in rejection, actual=$GateFailure"
}

Write-Output "TEST005_WINDOWS_INSTALL_REGRESSION_SCRIPT_TEST=PASS"
