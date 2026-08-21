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
    "docs/19_PROTOCOL_V1_FREEZE.md",
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

# Decode protocol test vectors against the explicit v1 vector schemas.
try:
    vectors = json.loads(
        (ROOT / "spec/protocol/test-vectors.json").read_text(encoding="utf-8")
    )
    if vectors.get("schemaVersion") != 1:
        errors.append("protocol vector schemaVersion must be 1")
    if vectors.get("fixedHeaderBytes") != 32:
        errors.append("protocol fixedHeaderBytes must be 32")

    header_fields = {
        "version",
        "messageType",
        "flags",
        "metadataLength",
        "payloadLength",
        "streamId",
    }
    error_name = re.compile(r"^[A-Z][A-Za-z0-9]*$")
    vector_names: set[str] = set()

    for vector in vectors["vectors"]:
        name = vector.get("name")
        if not isinstance(name, str) or not name:
            errors.append("protocol vector has an empty or non-string name")
            continue
        if name in vector_names:
            errors.append(f"duplicate protocol vector name: {name}")
        vector_names.add(name)

        kind = vector.get("kind")
        if kind == "frame-positive":
            allowed = {
                "kind",
                "name",
                "headerHex",
                "metadataHex",
                "payloadHex",
                "expected",
            }
            unknown = set(vector) - allowed
            if unknown:
                errors.append(
                    f"protocol vector {name} has unknown fields: {sorted(unknown)}"
                )
                continue
            header = bytes.fromhex(vector["headerHex"])
            metadata = bytes.fromhex(vector["metadataHex"])
            payload = bytes.fromhex(vector["payloadHex"])
            if len(header) != 32:
                errors.append(f"protocol vector {name} header is {len(header)} bytes")
                continue
            magic, version, message_type, flags, meta_len, payload_len, stream_id = (
                struct.unpack(">4sHHIIQQ", header)
            )
            if magic != b"RDFT":
                errors.append(f"protocol vector {name} has bad magic")
            if len(metadata) != meta_len:
                errors.append(
                    f"protocol vector {name} metadata length: "
                    f"header says {meta_len}, hex has {len(metadata)}"
                )
            if len(payload) != payload_len:
                errors.append(
                    f"protocol vector {name} payload length: "
                    f"header says {payload_len}, hex has {len(payload)}"
                )

            expected = vector.get("expected")
            if not isinstance(expected, dict) or set(expected) != header_fields:
                actual_fields = set(expected) if isinstance(expected, dict) else set()
                errors.append(
                    f"protocol vector {name} expected fields: "
                    f"required {sorted(header_fields)}, got {sorted(actual_fields)}"
                )
                continue
            actual = {
                "version": version,
                "messageType": message_type,
                "flags": flags,
                "metadataLength": meta_len,
                "payloadLength": payload_len,
                "streamId": stream_id,
            }
            for key, expected_value in expected.items():
                actual_value = actual[key]
                if actual_value != expected_value:
                    errors.append(
                        f"protocol vector {name} {key}: "
                        f"expected {expected_value!r}, got {actual_value!r}"
                    )
        elif kind == "frame-negative":
            allowed = {"kind", "name", "headerHex", "expectedError"}
            unknown = set(vector) - allowed
            if unknown:
                errors.append(
                    f"protocol vector {name} has unknown fields: {sorted(unknown)}"
                )
                continue
            header = bytes.fromhex(vector["headerHex"])
            if len(header) != 32:
                errors.append(f"protocol vector {name} header is {len(header)} bytes")
            expected_error = vector.get("expectedError")
            if not isinstance(expected_error, str) or not error_name.fullmatch(
                expected_error
            ):
                errors.append(
                    f"protocol vector {name} expectedError is not a stable error name"
                )
        elif kind == "metadata-negative":
            allowed = {
                "kind",
                "name",
                "messageType",
                "metadataHex",
                "expectedCodecError",
            }
            unknown = set(vector) - allowed
            if unknown:
                errors.append(
                    f"protocol vector {name} has unknown fields: {sorted(unknown)}"
                )
                continue
            message_type = vector.get("messageType")
            if not isinstance(message_type, int) or not 0 < message_type <= 0xFFFF:
                errors.append(f"protocol vector {name} messageType is invalid")
            metadata = bytes.fromhex(vector["metadataHex"])
            if not metadata or len(metadata) > 1024 * 1024:
                errors.append(f"protocol vector {name} metadata size is invalid")
            expected_error = vector.get("expectedCodecError")
            if not isinstance(expected_error, str) or not error_name.fullmatch(
                expected_error
            ):
                errors.append(
                    f"protocol vector {name} expectedCodecError is not a stable error name"
                )
        else:
            errors.append(f"protocol vector {name} has unknown kind: {kind!r}")
except Exception as exc:  # noqa: BLE001
    errors.append(f"unable to inspect protocol vectors: {exc}")

# Keep the RDFT v1 freeze index tied to the source registry, shared vectors,
# fixed envelope, and the one canonical cross-platform workflow. This does not
# duplicate the C++ codec/vector tests; it only guards the release index.
try:
    freeze_index = (ROOT / "docs/19_PROTOCOL_V1_FREEZE.md").read_text(encoding="utf-8")
    registry_text = (
        ROOT.parent / "src/lib/relaydesk/transfer/ProtocolMessageRegistry.def"
    ).read_text(encoding="utf-8")
    protocol_header = (
        ROOT.parent / "src/lib/relaydesk/transfer/Protocol.h"
    ).read_text(encoding="utf-8")
    vector_payload = json.loads(
        (ROOT / "spec/protocol/test-vectors.json").read_text(encoding="utf-8")
    )

    registry_count = sum(
        1 for line in registry_text.splitlines() if re.match(r"^\s*RDFT_MESSAGE\(", line)
    )
    vector_count = len(vector_payload.get("vectors", []))
    fixed_header_match = re.search(
        r"\bkFixedHeaderBytes\s*=\s*(\d+)\s*;", protocol_header
    )
    source_fixed_header = (
        int(fixed_header_match.group(1)) if fixed_header_match is not None else None
    )

    def freeze_field(name: str) -> str | None:
        match = re.search(
            rf"^\|\s*`{re.escape(name)}`\s*\|\s*`([^`]+)`\s*\|",
            freeze_index,
            re.MULTILINE,
        )
        return match.group(1) if match is not None else None

    indexed_registry_count = freeze_field("registryMessageTypes")
    indexed_vector_count = freeze_field("sharedJsonVectors")
    indexed_fixed_header = freeze_field("fixedHeaderBytes")
    if indexed_registry_count != str(registry_count):
        errors.append(
            "protocol freeze index registryMessageTypes: "
            f"source has {registry_count}, index has {indexed_registry_count!r}"
        )
    if indexed_vector_count != str(vector_count):
        errors.append(
            "protocol freeze index sharedJsonVectors: "
            f"source has {vector_count}, index has {indexed_vector_count!r}"
        )
    vector_fixed_header = vector_payload.get("fixedHeaderBytes")
    if source_fixed_header is None or source_fixed_header != vector_fixed_header:
        errors.append(
            "protocol fixed header mismatch between Protocol.h and test-vectors.json: "
            f"{source_fixed_header!r} != {vector_fixed_header!r}"
        )
    if indexed_fixed_header != str(source_fixed_header):
        errors.append(
            "protocol freeze index fixedHeaderBytes: "
            f"source has {source_fixed_header!r}, index has {indexed_fixed_header!r}"
        )

    for authority_reference in [
        "[`ProtocolMessageRegistry.def`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.def)",
        "[`messages.cddl`](../spec/protocol/messages.cddl)",
        "[`test-vectors.json`](../spec/protocol/test-vectors.json)",
    ]:
        if authority_reference not in freeze_index:
            errors.append(
                f"protocol freeze index authority missing: {authority_reference}"
            )

    tag_trigger = "relaydesk-protocol-v1-*"
    if freeze_field("freezeTagPattern") != tag_trigger:
        errors.append(f"protocol freeze tag pattern must be {tag_trigger}")

    authoritative_commit = freeze_field("authoritativeCommit")
    if authoritative_commit != "TO_BE_TAGGED" and not re.fullmatch(
        r"[0-9a-f]{40}", authoritative_commit or ""
    ):
        errors.append(
            "protocol freeze authoritativeCommit must be TO_BE_TAGGED or a full lowercase Git SHA"
        )

    freeze_tag = freeze_field("freezeTag")
    if freeze_tag != "TO_BE_TAGGED" and not re.fullmatch(
        r"relaydesk-protocol-v1-[A-Za-z0-9][A-Za-z0-9._-]*", freeze_tag or ""
    ):
        errors.append(
            "protocol freeze freezeTag must be TO_BE_TAGGED or match relaydesk-protocol-v1-*"
        )

    canonical_workflow_bytes = (
        ROOT.parent / ".github/workflows/relaydesk-build.yml"
    ).read_bytes()
    template_workflow_bytes = (
        ROOT / "templates/github/workflows/relaydesk-build.yml"
    ).read_bytes()
    canonical_workflow = canonical_workflow_bytes.decode("utf-8")
    template_workflow = template_workflow_bytes.decode("utf-8")
    trigger_line = f'      - "{tag_trigger}"'
    for label, workflow in [
        ("canonical", canonical_workflow),
        ("template", template_workflow),
    ]:
        tags_block = re.search(
            r"(?m)^    tags:\s*\n((?:      - [^\r\n]+(?:\r?\n|$))+)", workflow
        )
        tag_lines = tags_block.group(1).splitlines() if tags_block is not None else []
        if trigger_line not in tag_lines:
            errors.append(
                f"{label} workflow missing protocol freeze tag trigger: {tag_trigger}"
            )
    if canonical_workflow_bytes != template_workflow_bytes:
        errors.append("canonical and template RelayDesk workflows must be byte-identical")
except Exception as exc:  # noqa: BLE001
    errors.append(f"unable to validate protocol freeze index: {exc}")

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
        "macos-14",
        "workflow_dispatch",
        "actions/upload-artifact",
        "collect-ci-artifacts.py",
        '-DPACKAGE_VERSION_LABEL="${{ github.sha }}"',
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
