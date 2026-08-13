# RelayDesk File Transfer Protocol (RDFT) v1

This is the normative implementation-facing companion to `docs/05_FILE_TRANSFER_PROTOCOL.md`.

## Wire

```text
TLS/TCP stream
  -> 32-byte fixed header
  -> metadataLength bytes of CBOR
  -> payloadLength bytes of binary payload
```

All integer fields in the fixed header are unsigned and big-endian.

```text
0               4               8              12
+---------------+---------------+---------------+---------------+
| magic "RDFT"  | ver | type    | flags                         |
+---------------+---------------+---------------+---------------+
| metadata len                  | payload len (uint64)           |
+---------------+---------------+---------------+---------------+
| payload len continued         | stream id (uint64)             |
+---------------+---------------+---------------+---------------+
| stream id continued                                           |
+---------------------------------------------------------------+
```

Exact offsets:

```text
0  magic[4]
4  version:u16
6  type:u16
8  flags:u32
12 metadataLength:u32
16 payloadLength:u64
24 streamId:u64
```

## Parser requirements

- Incremental stream parser.
- Never allocate from remote lengths before validating limits.
- Checked arithmetic for total length.
- Consume exactly one frame; leave following bytes in buffer.
- Fatal error for invalid magic, unsupported major, length overflow or illegal type/state.
- `NeedMoreData` is not an error.
- Default metadata max 1 MiB.
- Default payload max 4 MiB.
- CBOR parsing has depth/container limits.
- A frame's metadata must be exactly one CBOR map where that message requires metadata.

## Message registry

| Value | Name | Metadata | Payload |
|---:|---|---|---|
| 0x0001 | HELLO | hello | empty |
| 0x0002 | AUTH_RESULT | auth-result | empty |
| 0x0003 | CAPABILITIES | capabilities | empty |
| 0x0004 | HEARTBEAT | heartbeat | empty |
| 0x0005 | HEARTBEAT_ACK | heartbeat | empty |
| 0x0100 | TRANSFER_OFFER | transfer-offer | empty |
| 0x0101 | TRANSFER_ACCEPT | transfer-accept | empty |
| 0x0102 | TRANSFER_REJECT | transfer-reject | empty |
| 0x0103 | MANIFEST_PAGE | manifest-page | empty |
| 0x0104 | MANIFEST_COMPLETE | manifest-complete | empty |
| 0x0200 | FILE_BEGIN | file-begin | empty |
| 0x0201 | FILE_CHUNK | file-chunk | bytes |
| 0x0202 | FILE_CHECKPOINT | file-checkpoint | empty |
| 0x0203 | FILE_END | file-end | empty |
| 0x0204 | FILE_RESULT | file-result | empty |
| 0x0300 | TRANSFER_PAUSE | transfer-command | empty |
| 0x0301 | TRANSFER_RESUME | transfer-command | empty |
| 0x0302 | TRANSFER_CANCEL | transfer-command | empty |
| 0x0303 | TRANSFER_COMPLETE | transfer-complete | empty |
| 0x0304 | TRANSFER_RESULT | transfer-result | empty |
| 0x0400 | RESUME_QUERY | resume-query | empty |
| 0x0401 | RESUME_RESPONSE | resume-response | empty |
| 0x7FFE | ERROR | error | empty |
| 0x7FFF | GOODBYE | goodbye | empty |

Unknown values are protocol errors in v1 unless a future capability explicitly declares an extension range.

### Session liveness

`HEARTBEAT` and `HEARTBEAT_ACK` share the deterministic CBOR map
`{1: sequence, 2: timestampMs}`. `sequence` is in `0..2^63-1` and
`timestampMs` is UTC milliseconds in `1..2^63-1`. Both messages use
`streamId=0`, empty payload, and no metadata fields other than keys 1 and 2.
`HEARTBEAT` carries only `AckRequired`; `HEARTBEAT_ACK` carries only
`Response` and echoes both values byte-for-byte.

The sender starts at sequence 0 on each TLS connection and increments by one.
The receiver idempotently re-acknowledges the current sequence. A lower stale
sequence is ignored; a skipped sequence or an ACK whose fields do not match the
outstanding heartbeat is a session protocol error. Heartbeat state never
survives reconnect.

## IDs

- UUID fields are 16-byte CBOR byte strings.
- SHA-256 fields are 32-byte CBOR byte strings.
- `streamId=0` is connection control.
- File data streams use nonzero IDs unique for the TLS connection.
- `sequence` starts at 0 or 1 by negotiated convention; v1 implementation uses 0 and increments by one.
- Offset is authoritative and must equal receiver expected offset for P0.

## Canonical manifest digest

To calculate `manifestSha256`:

1. Normalize every protocol path to UTF-8 NFC and `/`.
2. Sort entries by UTF-8 byte order of path, then type, then fileId bytes.
3. Serialize the canonical manifest form using deterministic CBOR.
4. SHA-256 over those bytes.

The test suite must freeze concrete vectors before compatibility release. Do not use non-deterministic `QHash` iteration.

## Resume

Receiver persists only durable bytes. A checkpoint can be sent after:

- at least 8 MiB additional durable data; or
- one second since the last checkpoint; or
- pause/connection drain.

On process restart, receiver validates the resume file and `.part` length. It may rehash existing partial bytes. It never trusts a stored offset beyond the actual file size.

## Completion

A file is complete only after:

1. exactly expected size;
2. SHA-256 match;
3. staging file closed/flushed;
4. conflict policy resolved;
5. atomic or safe final commit;
6. FILE_RESULT OK.

A transfer is complete only after all non-skipped files/directories reach terminal success and both sides exchange transfer result.

## Security limits

Negotiated values are `min(local, remote)` and may not exceed local hard maximums. A peer cannot raise local limits.

Recommended hard maximums:

```text
metadata/frame          1 MiB
payload/frame           4 MiB
manifest encoded        64 MiB
entries                 100,000
path UTF-8              4,096 bytes
path depth              128
concurrent transfers    2 default
files per transfer      2 default
queued payload/peer     16 MiB
```

## Error behavior

- Protocol/auth/trust failures close the session.
- A single transfer I/O failure normally fails that transfer, not all peers.
- Malformed paths reject the offer before disk writes.
- Hash mismatch never commits.
- Retryable is a hint; local policy decides.
