# Reconstructed Protocol Notes

## Legacy Async TCP Header

Normal server packets use the legacy type-3 async TCP header:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 4 | packet type, normally `3` |
| `0x04` | 4 | opcode |
| `0x08` | 4 | total packet bytes, including this header |
| `0x0c` | 1 | weighted checksum |
| `0x0d` | variable | payload |

The checksum is the low 8 bits of every payload byte multiplied by
`(offset % 9) + 1`, starting at offset `0x0d`. TCP receives are decoded by the
length field; one `recv` is not assumed to contain exactly one packet.

## Base Requests

| Request | Response | Purpose |
|---:|---:|---|
| `0x01` | `0x02` | login and status code |
| `0x03` | `0x04` | account/profile creation |
| `0x39` | `0x3a` | account creation screen text |
| `0x12` | `0x13` | current lobby user page |
| `0x14` | `0x15` | lobby channel page |
| `0x05` | `0x06` | join existing lobby |
| `0x08` | `0x09` | create lobby |
| `0x2a` | type `0` | chat/status text |
| `0x83` | `0x84` | user search |
| `0x19` | `0x1a` | advertise a game and configure host relay |
| `0x1d` | `0x1e` or `0x27` | game-browser page or live game add |
| `0x1b` | `0x26` | remove public game advertisement |
| `0x28` | `0x26` | start game and hide public advertisement |
| `0x96` | `0x97` | save the authenticated user's lobby mark |
| `0x98` | `0x99` | submit an authenticated per-user match result |
| `0x9a`/`0x9b`/`0x9c` | `0x9d` | begin/chunk/finish replay upload |
| `0x9e` | `0x9f` | list uploaded replays |
| `0xa0` | `0xa1`/`0xa2` | download replay chunks and completion status |

## Match records and replay transfer

Game types `0` (Top Vs Bottom) and `1` (Melee) update the normal-game
win/loss/draw record. Type `2` (Rank) updates the ranking record and ranking
points (`win * 3 + draw`). A result is accepted only for an authenticated
participant in a started two-or-more-user room. The first 16 bytes of the
room's random relay secret are the match token, and each account/token pair is
persistently idempotent.

Only the room host may upload a replay, and uploads are accepted only for
Melee or Rank. Replay bytes use ordered 32 KiB chunks plus an FNV-1a-64 final
digest. Completed files and a JSON catalog are stored below `data.replay_dir`.
The lobby browser pages that catalog and downloads the selected file in 32 KiB
server chunks.

## Lobby Mark Extension

The reconstructed lobby exposes five unique 42x18 mark frames. A client sends
opcode `0x96` with `u32 mark_index` (`0` through `4`). The server stores
the value in the authenticated account profile and answers with opcode `0x97`:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x0d` | 4 | status: `0` saved, `1` invalid index, `2` account unavailable |
| `0x11` | 4 | authoritative current mark index |

After a successful save the server rebroadcasts the normal opcode `0x07`
online-presence record. Its mark index is at packet offset `0x59`; opcode
`0x13` paged presence carries the same value at offset `0x5d`. This lets
already-open lobby lists update the existing nickname row without reconnecting.

## Lobby Activity Extension

Online-presence packets also carry the user's current WizardNet activity. Live
opcode `0x07` records use status offset `0x61` and a 32-byte room-name field at
`0x65`. Paged opcode `0x13` records use status offset `0x65` and the room name
at `0x69`.

| Status | Meaning |
|---:|---|
| `0` | in the lobby |
| `1` | hosting a game room |
| `2` | joined another user's game room |
| `3` | playing a game |

The server broadcasts a refreshed presence record when a room is created or
joined, when the host starts the game, and when each client returns to the
lobby. Older clients ignore the appended fields.

## WizardNet Relay Extension

The reconstructed server supports a game-room relay path so clients behind NAT
can create and join WizardNet rooms without a separate LAN VPN. This is a
game-specific relay, not a full OS virtual network adapter.

Relay packets use the same type-3 async TCP packet header.

| Opcode | Direction | Payload |
|---:|---|---|
| `0x90` | client -> server | `u32 game_id`, followed by an optional Link-lobby join payload |
| `0x91` | client -> server | optional `u32 game_id`; stale nonzero mismatches are ignored |
| `0x92` | client -> server | `u32 game_id`, `u32 target_member_id`, `u32 stream_id`, followed by relayed data |
| `0x93` | server -> client | `u32 status`, `u32 game_id`, `u32 local_member_id` |
| `0x94` | server -> client | `u32 game_id`, `u32 from_member_id`, `u32 stream_id`, followed by relayed data |
| `0x95` | server -> client | `u32 game_id`, `u32 member_id` that left |

Relay member `1` is always the host. Joiners receive the lowest free member ID
from `2` through `8`; a room accepts up to eight relay members including the
host. A `target_member_id` of `0` broadcasts to every other relay member in the
same room. Stream `0` carries Link-lobby traffic and stream `1` carries gameplay
Mode1 reliable payloads. Other stream IDs are invalid and are not forwarded.

The reconstructed client encrypts the relayed stream payload before sending
`0x90` join payloads or `0x92` relay frames. The encrypted payload starts with a
`WRL1` wrapper, followed by the plaintext length, nonce, SipHash tag, and a
ChaCha20-encrypted body. The server does not decrypt that body, but it requires
the wrapper magic, nonzero plaintext length, and declared/actual wire lengths to
match before routing by `game_id`, `target_member_id`, and `stream_id`.

The first non-host `0x90` join payload is encrypted with a deterministic
fallback key because the joiner has not received the room secret yet. Successful
host and join responses include the room secret, and normal Link and Mode1
frames are then encrypted with that room key. The client receive path rejects
plaintext relay payloads; once a room secret is available, gameplay Mode1
stream `1` also rejects fallback-key payloads. Link stream `0` still accepts the
fallback key so the host can process an initial join payload from a new member.

The server supports both targeted and broadcast fan-out. The reconstructed
client uses targeted member `1` for a non-host player's default Link-room send
path, and broadcast target `0` for the host's default room fan-out. That keeps
the relay behavior aligned with the original star-shaped TCP/IP Link-room
topology.

`0x93` status values:

| Status | Meaning |
|---:|---|
| `0` | OK |
| `1` | game not found |
| `2` | relay room full |
| `3` | sender is not a relay member |
| `4` | host session is missing |

Hosting a game with opcode `0x19` returns opcode `0x1a` with payload
`u32 status`, `u32 game_id`, `u32 local_member_id`. A successful host response
uses `status=1`, `local_member_id=1`.

Opcode `0x1b` removes the room from the public game browser. Opcode `0x28`,
sent by the Link lobby when the start countdown reaches zero, also retires the
host's public advertisement. In both cases the server keeps existing relay
membership alive so in-progress Link/gameplay traffic is not interrupted.

Hidden relay rooms reject new `0x90` joins unless the sender is already a
member. Host disconnect, host relay leave, or lobby reconnect tears down the
relay room and notifies joined members with `0x95`.

When a non-host member leaves, the server remembers that departed member ID for
the lifetime of the room. Later frames targeting that departed member, or empty
broadcasts after peers have left, are treated as stale shutdown traffic and are
not counted as `no_target` relay failures. Frames targeting a member ID that was
never part of the room are still counted as `no_target`.

When a relay room is removed, the server writes one `relay summary` log line.
Besides aggregate Link and Mode1 frame totals, current summaries include
`invalid_payload`, `member_peers`, and `member_frames`. `invalid_payload` counts
join or relay payloads rejected for a missing or malformed `WRL1` wrapper and
must remain zero for release evidence. `member_peers` records the last
server-observed TCP endpoint for each relay member. `link_bytes` and
`mode1_bytes` are encrypted wire-byte totals, including the relay crypto
wrapper. `member_frames` uses this compact format:

```text
member_frames=1:link_tx=4:link_rx=3:mode1_tx=120:mode1_rx=118;2:link_tx=3:link_rx=4:mode1_tx=118:mode1_rx=120
```

For a two-player NAT release gate, both member `1` and member `2` should have
distinct `member_peers` endpoints and nonzero `mode1_tx` and `mode1_rx`.
When `data.relay_evidence_dir` is configured, the server also writes the same
summary as per-room JSON evidence for `run_two_pc_release_gate.ps1`; that JSON
is revalidated against the requested room, game ID, member count, endpoint
count, and bidirectional Mode1 thresholds before it can pass the final gate.
