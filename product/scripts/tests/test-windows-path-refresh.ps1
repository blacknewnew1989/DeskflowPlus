$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
. (Join-Path $RepoRoot "product\scripts\setup-windows.ps1") -PathContractTest

$windowsPowerShell = "C:\Windows\System32\WindowsPowerShell\v1.0"
$first = Refresh-ProcessPath `
    -CurrentPath "C:\RelayDesk\bin;$windowsPowerShell\;C:\Stable\tools" `
    -MachinePath "C:\Machine\bin\;c:\relaydesk\bin;$windowsPowerShell" `
    -UserPath "C:\User\Scripts;C:\MACHINE\bin" `
    -PythonUserScripts "C:\User\Scripts\" `
    -WindowsPowerShell $windowsPowerShell `
    -DotNetTools "C:\Users\Relay\.dotnet\tools\" `
    -PassThru

$expected = @(
    "C:\RelayDesk\bin",
    $windowsPowerShell,
    "C:\Stable\tools",
    "C:\Machine\bin",
    "C:\User\Scripts",
    "C:\Users\Relay\.dotnet\tools"
) -join ";"
if ($first -ne $expected) { throw "WIN006_PATH_ORDER_OR_DEDUP: actual=$first" }

$second = Refresh-ProcessPath `
    -CurrentPath $first `
    -MachinePath "C:\Machine\bin\;c:\relaydesk\bin;$windowsPowerShell" `
    -UserPath "C:\User\Scripts;C:\MACHINE\bin" `
    -PythonUserScripts "C:\User\Scripts\" `
    -WindowsPowerShell $windowsPowerShell `
    -DotNetTools "C:\Users\Relay\.dotnet\tools\" `
    -PassThru
if ($second -ne $first) { throw "WIN006_PATH_NON_CONVERGENT: first=$first second=$second" }
if (@($first -split ";" | Where-Object { $_ -ieq $windowsPowerShell }).Count -ne 1) {
    throw "WIN006_POWERSHELL_PATH_LOST_OR_DUPLICATED"
}

Write-Output "WIN006_PATH_REFRESH_TEST=PASS"
