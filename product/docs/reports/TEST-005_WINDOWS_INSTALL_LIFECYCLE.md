# TEST-005 Windows Installer Lifecycle

## Result

TEST-005 is **PASS** for the automated Windows installer lifecycle at commit
`f1f4bed433846048149eed0fc3cfd98b7784c5db`.

The GitHub Actions run has an overall `failure` conclusion, but the installer
step completed with `success` and its machine-readable report has
`"status": "PASS"`. The run conclusion is explained by an unrelated CTest
failure on this agent branch's older baseline; it is not an installer failure.

## Execution record

| Field | Value |
|---|---|
| Task | `TEST-005-windows` |
| Date | 2026-08-13 |
| Commit | `f1f4bed433846048149eed0fc3cfd98b7784c5db` |
| Branch under test | `release/test005-windows-install-regression` (temporary trigger branch; deleted after evidence capture) |
| Runner | GitHub-hosted Windows x64 |
| Workflow run | `31657498852` — <https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31657498852> |
| Overall workflow conclusion | `failure` — older-baseline `RelayDeskTransferRecoveryMatrixTests` CTest failure, described below |
| Windows job | `94315075642` — `failure` only because the final aggregator also consumed the CTest outcome |
| Installer step | `Exercise Windows install, repair, major upgrade and uninstall` — `success` |
| Installer report | `test005-windows-install-regression.json` — `PASS` |
| Operator | Codex A7 |

The workflow invoked `product/scripts/test-windows-install-regression.ps1`
with both `-GeneratePreviousPackage` and the explicit disposable-runner opt-in
`-AllowSystemInstall`. Without the opt-in, the script refuses all machine-level
MSI operations.

## Artifact and package identity

| Field | Value |
|---|---|
| Artifact ID | `9165078568` |
| Artifact name | `relaydesk-windows-x64-f1f4bed433846048149eed0fc3cfd98b7784c5db` |
| Artifact size | 32,863,770 bytes |
| GitHub artifact API digest | `sha256:d2fc4e054f4a150ec246ebcd8cdb3235545fb8ecead79ba69bfcf8a2665817db` |
| Report SHA-256 | `208fdfbc9f1dd633d3fdfa2f56b01f24fc247a628b7189bd4e75e7861c3ea96e` |
| Product | `RelayDesk` |
| Candidate version | `1.26.0.9999` |
| Candidate ProductCode | `BAEA988D-DDA6-4DC3-866D-97364E500E47` |
| UpgradeCode | `50C1FCAB-2BF8-447C-806D-A53C21C6A237` |
| Signature state | unsigned internal package |

| Package | Bytes | SHA-256 |
|---|---:|---|
| `relaydesk-f1f4bed433846048149eed0fc3cfd98b7784c5db-win-x64-unsigned.msi` | 15,895,609 | `8816bacab2f37825dd19976ed20cf6a551856abe064f0d5688fa1a8d11b5b64e` |
| `relaydesk-f1f4bed433846048149eed0fc3cfd98b7784c5db-win-x64-unsigned-portable.7z` | 12,937,939 | `1f65cc98442ca1fe5a42ca77a33e6386f47b22dcd2981009a27a2d5656c4de36` |

The artifact also contains the source packages, `artifact-manifest.json`,
`SHA256SUMS.txt`, the CTest log, the installer report, and all six Windows
Installer verbose logs.

## Lifecycle evidence

| Stage | Expected | Actual evidence | Result |
|---|---|---|---|
| MSI validation | Candidate is a structurally valid unsigned WiX MSI | WiX validation passed; package identity matched RelayDesk branding and the configured UpgradeCode | PASS |
| Portable dependency launch | Portable GUI and core start with deployed dependencies | `deskflow.exe --version` and `deskflow-core.exe --version` returned successfully; no service executable was present in the portable layout | PASS |
| Clean system install | Candidate registers and installs under the machine-wide product path | ProductCode `BAEA988D-DDA6-4DC3-866D-97364E500E47` registered at `C:\Program Files\RelayDesk`; installed GUI, core, and daemon were present | PASS |
| Service registration | RelayDesk service is installed with the intended runtime policy | Service `RelayDesk` existed, was `Running`, used automatic start, and targeted the installed daemon | PASS |
| Firewall registration | Installer creates the intended private-network rules | Exactly two enabled inbound Private-profile rules, RelayDesk Server and RelayDesk Client, targeted the installed `deskflow-core.exe` | PASS |
| Same-version repair | Registered candidate can be repaired without losing data or registrations | Windows Installer executed `REINSTALL=ALL REINSTALLMODE=vomus`; product, service, firewall, configuration, and sentinels remained valid | PASS |
| Clean uninstall | Candidate can be removed by ProductCode | Windows Installer removed the candidate successfully | PASS |
| First residue assertion | Machine mutations are removed while external user data remains | Product registration, service, firewall rules, install directory, and Start Menu folder were absent; configuration and sentinels remained | PASS |
| Predecessor install | A lower-version package with the production UpgradeCode can register | Synthetic identity-only predecessor `1.25.0`, ProductCode `D0818C14-5DD3-4DA6-B438-BE19D4FD1DE5`, installed successfully | PASS |
| Major upgrade | Candidate performs a real Windows Installer major-upgrade transaction | `1.25.0/D0818C14-5DD3-4DA6-B438-BE19D4FD1DE5` upgraded to `1.26.0.9999/BAEA988D-DDA6-4DC3-866D-97364E500E47`; both used UpgradeCode `50C1FCAB-2BF8-447C-806D-A53C21C6A237`; the predecessor registration was gone | PASS |
| Upgraded product validation | Candidate remains operational after the major upgrade | Candidate registration, service, firewall rules, installed binaries, configuration, and sentinels were valid | PASS |
| Upgraded uninstall | The upgraded candidate can be removed | Windows Installer removed the upgraded candidate successfully | PASS |
| Second residue assertion | Neither predecessor nor candidate leaves machine mutations | Both ProductCodes, service, firewall rules, install directory, and Start Menu folder were absent | PASS |

The generated predecessor changes only MSI identity and version while retaining
the real candidate payload. This proves the Windows Installer major-upgrade
transaction, stable UpgradeCode, ProductCode replacement, service/firewall
transition, user-data retention, and cleanup. It does not claim migration
coverage between two historically different application payloads.

## Configuration and sentinel preservation

The runner already contained `RelayDesk.conf` from earlier test execution. The
harness therefore used `backup-append-restore` mode:

1. Back up the pre-existing file byte-for-byte.
2. Add a unique TEST-005 sentinel without replacing existing content.
3. Verify after install, repair, uninstall, predecessor install, major upgrade,
   and upgraded uninstall that the original byte sequence still exists.
4. Permit the running RelayDesk service to append or update unrelated active
   settings.
5. Restore the original file bytes in the final cleanup path.

Two uniquely named external marker files represented trust and transfer-history
data. Their exact SHA-256 values were checked after every lifecycle transition.
The final report records:

| Report field | Result | Evidence |
|---|---|---|
| `userDataPreserved` | PASS | `RelayDesk.conf`, trust marker, and history marker survived both lifecycles |
| `preexistingConfigPreserved` | PASS | Original configuration bytes remained a contiguous subsequence while the service could update other settings |
| `unrelatedUserDataHashPreserved` | PASS | Trust/history marker SHA-256 values were unchanged through install, repair, upgrade, and uninstall |
| `testRootRetained` | `false` | The unique temporary test root was removed |

## Windows Installer verbose logs

Every lifecycle transaction produced a verbose log, contained a successful
Windows Installer product event, and ended with `MainEngineThread is returning 0`.

| Log | SHA-256 | Final engine result |
|---|---|---|
| `test005-msiexec-clean-install.log` | `86c318ae35f8d71d96f644017d3deaf2eb9e703ad1200cd3a07d1d8ed84db548` | `0` |
| `test005-msiexec-repair.log` | `8b520584e5ab43644aa0cb3c40fe3378f856eae3c8607266b364b1343b393ac6` | `0` |
| `test005-msiexec-clean-uninstall.log` | `147652d96cad0414920e11735306f7a80bf1c017df0d546526f5c4dd5f7c25ac` | `0` |
| `test005-msiexec-previous-install.log` | `1236c410a8026782bee061d78c59275ffc23e5eddeaa952d596c0cbfce508982` | `0` |
| `test005-msiexec-major-upgrade.log` | `26270a45449ca5eadda2cf26dd9346b4fc61754b279b003e5a6a459885e753e3` | `0` |
| `test005-msiexec-upgraded-uninstall.log` | `d8c549a5a0da46caefe9bc990eed469d7299047d01a77ae53123df4d558fc5c4` | `0` |

## Workflow conclusion versus installer result

The Windows CTest log reports 68 passes and one failure out of 69 tests:

```text
48/69 Test #48: RelayDeskTransferRecoveryMatrixTests ... ***Exception: SegFault
99% tests passed, 1 tests failed out of 69
```

This temporary agent branch predates the already integrated DeviceId
function-local regular-expression fix (`da0428940`). The stale-baseline CTest
failure caused the Windows job's final aggregation step, and therefore the
workflow, to conclude `failure`. The TEST-005 step ran after CTest under
diagnostic continuation, concluded `success`, uploaded its report and logs, and
recorded `status=PASS`. No installer result is inferred from the workflow's
overall color.

## NOT_RUN boundary

| Check | Result | Reason |
|---|---|---|
| Interactive unsigned SmartScreen/UAC presentation | NOT_RUN | The quiet GitHub-hosted runner install cannot observe or validate interactive SmartScreen or UAC UI |

Unsigned package installation itself was exercised successfully. The NOT_RUN
entry is limited to the user-visible interactive warning behavior and must be
verified during final installation acceptance.
