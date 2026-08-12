param(
    [string]$Repo = (git rev-parse --show-toplevel)
)

$ErrorActionPreference = "Stop"
$PackageRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

$Python = $null
foreach ($candidate in @("python", "py")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) {
        $Python = $candidate
        break
    }
}
if (-not $Python) { throw "Python 3 is required." }

$Script = Join-Path $PackageRoot "scripts\autonomous-init-repo.py"
if ($Python -eq "py") {
    & py -3 $Script --package-root $PackageRoot --repo $Repo
}
else {
    & python $Script --package-root $PackageRoot --repo $Repo
}
if ($LASTEXITCODE -ne 0) { throw "RelayDesk autonomous bootstrap failed." }
