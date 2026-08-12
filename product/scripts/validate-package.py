#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

REQUIRED = [
    "README.md",
    "START_HERE.md",
    "AGENTS.md",
    "CODEX_START_PROMPT.txt",
    "PROJECT_STATE.md",
    "TASK_BOARD.md",
    "PACKAGE_INFO.json",
    "THIRD_PARTY_NOTICES.md",
    "VALIDATION_REPORT.md",
    "LICENSES/GPL-2.0-only.txt",
    "LICENSES/LicenseRef-OpenSSL-Exception.txt",
    "docs/00_MASTER_PLAN.md",
    "docs/01_PRD.md",
    "docs/02_SYSTEM_ARCHITECTURE.md",
    "docs/05_FILE_TRANSFER_PROTOCOL.md",
    "docs/11_TEST_AND_ACCEPTANCE.md",
    "docs/12_SECURITY_AND_PATH_SAFETY.md",
    "docs/13_LICENSE_AND_COMPLIANCE.md",
    "docs/18_SHARED_CONTRACTS.md",
    "docs/19_DEMO_RUNBOOK.md",
    "docs/20_AUTONOMOUS_EXECUTION_AND_GIT_WORKFLOW.md",
    "prompts/A0_ORCHESTRATOR.md",
    "prompts/A6_FILE_TRANSFER.md",
    "spec/protocol/messages.cddl",
    "spec/protocol/test-vectors.json",
    "starter/CMakeLists.txt",
    "starter/src/FrameCodec.cpp",
    "starter/src/PathPolicy.cpp",
    "starter/tests/FrameCodecTests.cpp",
    "starter/tests/PathPolicyTests.cpp",
    "scripts/bootstrap-upstream.sh",
    "scripts/bootstrap-upstream.ps1",
    "scripts/autonomous-init-repo.py",
    "scripts/complete-stage.py",
    "scripts/run-github-actions.py",
    "scripts/install-github-workflows.py",
    "scripts/setup-macos.sh",
    "scripts/build-macos.sh",
    "scripts/package-macos.sh",
    "scripts/setup-windows.ps1",
    "scripts/build-windows.ps1",
    "scripts/package-windows.ps1",
    "scripts/collect-ci-artifacts.py",
    "scripts/install-package.py",
    "config/branding.example.json",
    "config/product.defaults.json",
    "templates/FINAL_ACCEPTANCE.md",
    "templates/github/workflows/relaydesk-build.yml",
]

errors: list[str] = []
warnings: list[str] = []


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


for item in REQUIRED:
    path = ROOT / item
    if not path.is_file():
        errors.append(f"missing required file: {item}")
    elif path.stat().st_size == 0:
        errors.append(f"empty required file: {item}")

# Validate every JSON file, not only a hand-picked subset.
for path in sorted(ROOT.rglob("*.json")):
    try:
        json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001 - report malformed delivery data
        errors.append(f"invalid JSON {relative(path)}: {exc}")

# Decode protocol test vectors and compare every fixed-header field.
try:
    vectors = json.loads(
        (ROOT / "spec/protocol/test-vectors.json").read_text(encoding="utf-8")
    )
    if vectors.get("fixedHeaderBytes") != 32:
        errors.append("protocol fixedHeaderBytes must be 32")

    for vector in vectors["vectors"]:
        name = vector["name"]
        header = bytes.fromhex(vector["headerHex"])
        if len(header) != 32:
            errors.append(f"protocol vector {name} header is {len(header)} bytes")
            continue

        magic, version, message_type, flags, meta_len, payload_len, stream_id = (
            struct.unpack(">4sHHIIQQ", header)
        )
        if name != "invalid-magic" and magic != b"RDFT":
            errors.append(f"protocol vector {name} has bad magic")

        expected = vector.get("expected")
        if expected:
            actual = {
                "version": version,
                "messageType": message_type,
                "flags": flags,
                "metadataLength": meta_len,
                "payloadLength": payload_len,
                "streamId": stream_id,
            }
            for key, expected_value in expected.items():
                if key == "payloadUtf8":
                    payload = bytes.fromhex(vector.get("payloadHex", ""))
                    actual_value = payload.decode("utf-8")
                else:
                    actual_value = actual[key]
                if actual_value != expected_value:
                    errors.append(
                        f"protocol vector {name} {key}: "
                        f"expected {expected_value!r}, got {actual_value!r}"
                    )
except Exception as exc:  # noqa: BLE001
    errors.append(f"unable to inspect protocol vectors: {exc}")

# Text hygiene and Markdown fence balance.
for path in sorted(ROOT.rglob("*")):
    if not path.is_file() or path.name in {"MANIFEST.json"}:
        continue
    try:
        data = path.read_bytes()
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        continue

    if b"\r\n" in data and path.suffix.lower() not in {".ps1"}:
        warnings.append(f"CRLF found outside PowerShell: {relative(path)}")
    if "\x00" in text:
        errors.append(f"NUL found: {relative(path)}")
    if re.search(r"(?i)\bTODO:\s*$", text, re.MULTILINE):
        warnings.append(f"empty TODO found: {relative(path)}")
    if path.suffix.lower() == ".md":
        fences = sum(1 for line in text.splitlines() if line.lstrip().startswith("```"))
        if fences % 2 != 0:
            errors.append(f"unbalanced Markdown code fence: {relative(path)}")

# Prevent stale/unverified build instructions and wrong exception identifiers.
for path in sorted(ROOT.rglob("*")):
    if not path.is_file():
        continue
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    if "cmake --workflow --preset release" in text and path.name != "validate-package.py":
        errors.append(f"unverified CMake workflow instruction: {relative(path)}")
    if (
        "GPL-2.0-only WITH OpenSSL-exception" in text
        and path.name != "validate-package.py"
    ):
        errors.append(f"non-upstream SPDX exception spelling: {relative(path)}")


# Autonomous execution contract checks.
prd = (ROOT / "docs/01_PRD.md").read_text(encoding="utf-8")
for required_phrase in [
    "用户只负责",
    "autonomous-init-repo.py",
    "小功能完成立即提交",
    "阶段完成",
    "Windows 与 macOS 协同开发",
    "GitHub Actions 回退",
]:
    if required_phrase not in prd:
        errors.append(f"autonomous PRD phrase missing: {required_phrase}")

for stale_phrase in [
    "人工只需准备什么",
    "Fork 或克隆 Deskflow",
    "不自动提交",
    "不自动推送",
    "所有 Phase 1+ 任务均由 Phase 0 阻塞",
]:
    for path in [ROOT / "README.md", ROOT / "START_HERE.md", ROOT / "AGENTS.md", ROOT / "docs/01_PRD.md"]:
        if stale_phrase in path.read_text(encoding="utf-8"):
            errors.append(f"stale manual workflow phrase in {relative(path)}: {stale_phrase}")

baseline = (ROOT / "references/UPSTREAM_BASELINE.md").read_text(encoding="utf-8")
if "v1.26.0" not in baseline or "760e3b9" not in baseline:
    errors.append("pinned upstream tag/commit missing from baseline reference")

agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
if "GPL-2.0-only WITH LicenseRef-OpenSSL-Exception" not in agents:
    errors.append("exact upstream license identifier missing from AGENTS.md")

cmake = (ROOT / "starter/CMakeLists.txt").read_text(encoding="utf-8")
if "qt_standard_project_setup()" not in cmake:
    errors.append("starter CMake does not enable Qt standard project setup/AUTOMOC")

installer_text = (ROOT / "scripts/install-package.py").read_text(encoding="utf-8")
if '"AGENTS.md"' not in installer_text:
    errors.append("install-package.py must copy AGENTS.md into product/ for idempotent installed execution")

prompt_lines = [
    line.strip()
    for line in (ROOT / "CODEX_START_PROMPT.txt").read_text(encoding="utf-8").splitlines()
    if line.strip()
]
if len(prompt_lines) != 1:
    errors.append("CODEX_START_PROMPT.txt must contain exactly one non-empty line")

for duplicate in ["scripts/autonomous-bootstrap.py", "templates/github-actions"]:
    if (ROOT / duplicate).exists():
        errors.append(f"duplicate automation entrypoint/workflow present: {duplicate}")

workflow_templates = list((ROOT / "templates/github/workflows").glob("*.yml"))
if len(workflow_templates) != 1 or workflow_templates[0].name != "relaydesk-build.yml":
    errors.append("exactly one canonical workflow template must exist: templates/github/workflows/relaydesk-build.yml")
else:
    workflow_text = workflow_templates[0].read_text(encoding="utf-8")
    for required_workflow_phrase in [
        "windows-2022",
        "macos-15",
        "workflow_dispatch",
        "actions/upload-artifact",
        "collect-ci-artifacts.py",
    ]:
        if required_workflow_phrase not in workflow_text:
            errors.append(f"canonical workflow phrase missing: {required_workflow_phrase}")

actions_runner = (ROOT / "scripts/run-github-actions.py").read_text(encoding="utf-8")
for required_runner_phrase in [
    "^{commit}",
    "excluded_ids",
    "DOWNLOAD_SHA256SUMS.txt",
    "artifactFiles",
    "[skip ci]",
]:
    if required_runner_phrase not in actions_runner:
        errors.append(f"Actions runner reliability feature missing: {required_runner_phrase}")

stage_runner = (ROOT / "scripts/complete-stage.py").read_text(encoding="utf-8")
if '"--ref", tag, "--no-trigger"' not in stage_runner:
    errors.append("complete-stage.py must monitor the pushed stage tag")

for path in [ROOT / "templates/PR.md", ROOT / "templates/TASK.md", ROOT / "prompts/AGENT_MATRIX.md"]:
    text = path.read_text(encoding="utf-8")
    if "Manual validation" in text or "Manual/E2E" in text:
        errors.append(f"intermediate manual-validation wording remains: {relative(path)}")

for forbidden in ["__pycache__", ".DS_Store", "Thumbs.db"]:
    hits = list(ROOT.rglob(forbidden))
    if hits:
        errors.append(
            f"generated/OS metadata present ({forbidden}): "
            + ", ".join(relative(hit) for hit in hits)
        )

if errors:
    print("PACKAGE VALIDATION FAILED", file=sys.stderr)
    for item in errors:
        print(f"ERROR: {item}", file=sys.stderr)
    for item in warnings:
        print(f"WARNING: {item}", file=sys.stderr)
    raise SystemExit(2)

print(f"PACKAGE VALIDATION PASS ({len(REQUIRED)} required files)")
print(f"JSON files checked: {len(list(ROOT.rglob('*.json')))}")
print(f"Protocol vectors checked: {len(vectors['vectors'])}")
for item in warnings:
    print(f"WARNING: {item}")
