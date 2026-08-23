# Ranker Reconstructed WizardNet Server

This folder contains the reconstructed WizardNet control server used by
`ranker_rebuild.exe`.

## Relay Status

The server includes a room-scoped WizardNet relay for the reconstructed client.
Clients make outbound TCP connections to the server, and Link-lobby plus
gameplay Mode1 payloads are forwarded as:

```text
client -> server -> peer
```

This is intended to let users behind routers, CGNAT, or restrictive NAT create
and join WizardNet rooms without installing Radmin VPN.

The relay is game-specific. It does not create a Windows virtual NIC and does
not expose a general-purpose LAN to other applications.
Relayed Link and Mode1 payload bodies are encrypted by the reconstructed client
with a `WRL1` wrapper before they enter the server TCP stream. The server routes
the opaque encrypted payload by room/member/stream metadata and does not need
to parse the game payload itself. The initial join payload uses a fallback key
until the joiner receives the room secret; room-secret gameplay Mode1 traffic is
then enforced by the client receive path.

## Relay Topology

Relay member `1` is always the host. Joiners receive member IDs from `2`
through `8`. Target member `0` broadcasts to every other member in the same
room.

Stream `0` carries Link-lobby traffic. Stream `1` carries gameplay Mode1
reliable payloads.

The client mirrors the original Link-room TCP/IP topology while using the
relay: the host can fan out to all relay members, while a joining client's
default Link-room send path targets the host first. This keeps the relay aligned
with the original star-shaped room transport.

Room advertisements and relay sessions are intentionally separate. Once a relay
room starts, is removed, or expires from the public game browser, it is hidden
and rejects late joins. Existing members keep their relay route until the host
disconnects, leaves, or returns to the lobby.

## Running

Python 3.11 or newer is sufficient; no third-party Python package is required.

```powershell
cd C:\Users\eno\Desktop\jw2_reconstruction\ranker_reconstructed_server
.\run_server.ps1
```

The default listen address is `0.0.0.0:19777`. To use another port:

```powershell
.\run_server.ps1 -ListenAddress 127.0.0.1 -Port 20000
```

The JSON config also sets TCP keepalive and `send_timeout_seconds`. A client
whose outbound relay writes cannot drain before that timeout is closed so one
stalled peer cannot hold up the room's relay fan-out.

The deployed client reads display, remembered-account, and relay endpoint
settings from `ranker_client.ini` next to `ranker_rebuild.exe`. The current
public WizardNet section uses:

```ini
[WizardNet]
Address=115.22.136.89
Port=19777
```

Environment variables override the INI and are useful for automation:

```powershell
$env:RANKER_RECONSTRUCTED_SERVER_ADDRESS = "server.example.net"
$env:RANKER_RECONSTRUCTED_SERVER_PORT = "19777"
.\ranker_rebuild.exe
```

## Implemented Scope

- Legacy type-3 async TCP packet framing, checksums, fragmentation, and merging.
- Login, automatic account registration, account creation, and minimal profile
  replies.
- Lobby list, lobby movement, online presence paging, lobby chat, and persistent
  per-account selection of the five unique lobby nickname marks.
- Separate persistent normal-game (Top Vs Bottom/Melee) and ranking-game
  win/loss/draw records, including the existing rank list and player-profile
  replies.
- Match-token-validated replay uploads from every Rank and Melee participant,
  match-token/content-SHA-256 duplicate suppression, a persistent replay
  catalog, and authenticated replay listing/download. The client saves
  downloaded files under `Replays\download`.
- Game advertisement, duplicate-name handling, game browser paging, and removal
  notices.
- Room-scoped relay join, leave, member-left notification, targeted frame
  delivery, broadcast fan-out, room capacity, and hidden-room behavior.
- Active relay session preservation after game start, advertisement removal, and
  advertisement expiry.
- Relay-only reconstructed client path for WizardNet Link and gameplay Mode1
  traffic.
- Per-room relay summary logging when a room is removed, including Link frames,
  Mode1 frames, deliveries, missing targets, invalid streams, and malformed or
  unwrapped relay payloads.

## Verification

After deploying or restarting the server, verify the central relay protocol:

```powershell
python tools\relay_smoke.py --host 115.22.136.89 --port 19777
```

After deploying `RankerOCPV_Win\ranker_rebuild.exe`, verify the reconstructed
Win32 client path from the repository root:

```powershell
python ranker_reconstructed_server\tools\gui_relay_smoke.py --server-host 115.22.136.89 --server-port 19777 --timeout 75 --start-game --post-wait 12 --assert-relay-only
```

This drives two local client processes through WizardNet login, room creation,
game-browser join, Link-room start, countdown completion, and gameplay Mode1
relay traffic. With `--assert-relay-only`, it also fails if either client has an
established TCP connection to anything other than the configured WizardNet relay
server, if either process opens a TCP listener, or if either process keeps a UDP
endpoint open during the relayed room.
When the server log is local, add
`--server-log debug_artifacts\server_logs\SERVER_LOG.err.log` to require the
matching `relay summary` line after the smoke closes the clients.

To exercise room fan-out with more than one joiner, add `--joiner-count N`.
The smoke tool will choose a `.trk` map whose leading player-count prefix can
hold the host plus all requested joiners when one is available:

```powershell
python ranker_reconstructed_server\tools\gui_relay_smoke.py --server-host 115.22.136.89 --server-port 19777 --timeout 90 --start-game --post-wait 12 --assert-relay-only --joiner-count 2
```

When `--server-log` is supplied, the same fan-out smoke requires the server
summary to prove host plus every joiner has a distinct relay TCP endpoint and
bidirectional Mode1 traffic.

Run the unit tests with:

```powershell
python -m unittest tests.test_server tests.test_accounts tests.test_config tests.test_replays
```

Replay files and their catalog index are stored in the configured
`data.replay_dir` (by default `data/replays`). `server.max_replay_bytes` limits
each upload and defaults to 64 MiB. Stored display names use
`[map]_player1_vs_player2.ply`. If that filename already exists, only the
server-side stored filename receives a numeric collision suffix.

## Two-PC NAT Release Gate

Same-machine smoke tests prove the server protocol and reconstructed Win32 state
machine, but they are not a substitute for a real NAT test. Before treating the
feature as complete, use two machines on different routers with Radmin disabled
or uninstalled on both:

1. Start `ranker_rebuild.exe` on both machines and log in to the same WizardNet
   server.
2. On PC A, create a WizardNet room and wait in the Link lobby.
3. On PC B, refresh the game browser, join PC A's room, and verify both clients
   remain in the same Link lobby without any router port forwarding.
4. Start the game from PC A and play long enough for Mode1 gameplay packets to
   flow in both directions.
5. Inspect both `Jw2.log` files. Passing logs must show relay configuration,
   Link stream queued frames with `crypto=yes` and `wire_bytes=...`,
   `wizardnet relay link received` lines from the peer member, Mode1 stream
   frames with `crypto=yes`, and no relay `blocked`, `rejected`,
   `dropped`, or `ignored` lines after the join is accepted.
6. Inspect the server log for the removed room. The `relay summary` line should
   show nonzero `link_frames` and `mode1_frames`, `no_target=0`, and
   `invalid_stream=0` and `invalid_payload=0`. Current server logs also include
   `member_frames`, which records per-member Link and Mode1 transmit/receive
   counts. The summary byte counters are encrypted wire-byte totals.

The per-client log check can be automated while the match is still running:

```powershell
python ranker_reconstructed_server\tools\relay_log_check.py C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win\Jw2.log --role host --require-mode1 --forbid-direct-transport
python ranker_reconstructed_server\tools\relay_log_check.py C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win\Jw2.log --role joiner --room ROOM_NAME --require-mode1 --forbid-direct-transport
```

While a live match is running the server summary may not exist yet, so the live
check can use the room name. After the room ends and the server writes its
`relay summary`, pass `--game-id GAME_ID` to scope client-log checks to that
exact relay session. This avoids stale evidence from earlier rooms with the
same name.

For the release gate, prefer `relay_client_check.py` while the game is still
running. It checks `Jw2.log`, the live process network table, the deployed
binary/config hash, the configured relay server in `ranker_client.ini`, and
optional Radmin/Famatech VPN state:

To package the exact current deployment executable/config plus all release-gate
tools for a remote tester, create a two-PC test kit from the repository root:

```powershell
.\ranker_reconstructed_server\tools\prepare_two_pc_relay_kit.ps1
```

The kit is written under `debug_artifacts\relay_kit` and includes
`deployment_payload\ranker_rebuild.exe`, `deployment_payload\ranker_client.ini`,
`manifest.json`, `collect_live_report.ps1`, and `run_final_gate.ps1`.

```powershell
.\ranker_reconstructed_server\tools\collect_relay_client_report.ps1 -Role host -Room ROOM_NAME
.\ranker_reconstructed_server\tools\collect_relay_client_report.ps1 -Role joiner -Room ROOM_NAME
```

Run that command on each PC while the game is still running. It writes a JSON
report under `debug_artifacts\relay_evidence` by default. If Python or the game
folder is elsewhere, pass `-Python` or `-InstallDir`.

After the room closes and the server writes its summary, run the final gate.
When `data.relay_evidence_dir` is configured, the server automatically writes a
per-room evidence JSON file there; the public config writes these under
`debug_artifacts\relay_server_evidence`.

```powershell
python ranker_reconstructed_server\tools\relay_server_summary_check.py debug_artifacts\server_logs\SERVER_LOG.err.log --room ROOM_NAME --min-link-frames 1 --min-mode1-frames 1 --min-members 2 --min-distinct-peer-endpoints 2 --min-bidirectional-mode1-members 2 --max-no-target 0 --max-invalid-stream 0
.\ranker_reconstructed_server\tools\run_two_pc_release_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerLog debug_artifacts\server_logs\SERVER_LOG.err.log -HostReport HOST.json -JoinerReport JOINER.json
```

With automatic server evidence:

```powershell
Get-ChildItem debug_artifacts\relay_server_evidence\relay_*_game*_ROOM_NAME.json
.\ranker_reconstructed_server\tools\run_two_pc_release_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidence debug_artifacts\relay_server_evidence\relay_YYYYMMDDTHHMMSS_gameN_ROOM_NAME.json -HostReport HOST.json -JoinerReport JOINER.json
```

Or let the gate select the newest automatic evidence JSON matching the room and
optional game ID:

```powershell
.\ranker_reconstructed_server\tools\run_two_pc_release_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidenceDir debug_artifacts\relay_server_evidence -HostReport HOST.json -JoinerReport JOINER.json
```

Add `-Output RESULT.json -SummaryOutput SUMMARY.txt` to keep both the full JSON
result and a compact human-readable pass/fail summary. The summary lists failing
sections, missing checks, selected server evidence, member/endpoint counts,
machine fingerprints, and LAN fingerprints.

If automatic evidence was not enabled, the server operator can export just the
validated room evidence from the full server log:

```powershell
python ranker_reconstructed_server\tools\export_relay_server_evidence.py debug_artifacts\server_logs\SERVER_LOG.err.log --room ROOM_NAME --output ROOM_server_evidence.json
.\ranker_reconstructed_server\tools\run_two_pc_release_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidence ROOM_server_evidence.json -HostReport HOST.json -JoinerReport JOINER.json
```

The final gate revalidates exported server evidence against its own room,
game-id, member, endpoint, and bidirectional Mode1 thresholds; a JSON file
exported for a different room or with weaker criteria must not pass.
`relay_release_gate_check.py` defaults to the same two-PC release criteria:
client reports are required, Radmin/Famatech must be inactive, hashed machine
identity fingerprints must differ, network evidence must differ, neither
client may route its IPv4 default gateway through a VPN/tunnel/virtual-marked
interface, the server summary must contain at least two relay
members/endpoints, and at least two members must have bidirectional Mode1
traffic. Client logs must also contain
encrypted relay evidence (`crypto=yes` plus `wire_bytes=` on queued relay
frames). Development-only lab checks can be relaxed explicitly with
`--allow-missing-client-reports`,
`--allow-radmin`, `--allow-same-machine`, or `--allow-same-network`; do not use
those relaxations for final NAT proof.

If a client log contains multiple sessions, add `--game-id GAME_ID` to
`relay_client_check.py` using the ID from the matching server summary.
`relay_client_check.py` records the local `ranker_rebuild.exe` SHA256,
the `ranker_client.ini` hash and parsed WizardNet endpoint, hostname, hashed
machine identity fingerprint,
local IPv4 addresses, matching Radmin/Famatech processes, matching network
adapters, matching services, and the live network table snapshot. The raw
MachineGuid/UUID/serial values are not written to the JSON report. The
collection script passes
`--require-deployment`, so passing output must show an existing
`ranker_rebuild.exe`, no alternate deployment executable name, and a
`ranker_client.ini` whose `[WizardNet] Address` and `Port` match the tested
server. With `--require-no-radmin`, passing output must show no matching
process, no active matching adapter, no running matching service, and no
VPN/tunnel/virtual-marked interface providing the IPv4 default gateway.

Passing output must report `ok: true`, exactly one live `ranker_rebuild.exe`
process on that PC, one server TCP connection for that process, no unexpected
established TCP connections, no TCP listeners, and no UDP endpoints owned by the
client process.
The server summary check and combined release gate check must also report
`ok: true`. The combined release gate reads the server summary first and uses
its `game_id` as the client-log session scope unless `--game-id` is supplied
explicitly. When client JSON reports are supplied, the gate also requires their
live network checks and, with `--require-no-radmin`, their Radmin-off plus
forbidden default-route checks.
By default `run_two_pc_release_gate.ps1` also requires both client reports to
target the same relay server, both reports to contain the same
`ranker_rebuild.exe` SHA256, different hashed machine identity fingerprints,
and different network evidence. It also requires at least two distinct
server-observed relay TCP endpoints and at least two relay members in the server
summary to have both `mode1_tx` and `mode1_rx` counts, proving gameplay packets
flowed in both directions. The network check passes if the server saw at least
two distinct remote client IPs, or if the two client JSON reports contain
distinct LAN fingerprints from their IPv4 gateway, gateway MAC, or prefix data.
The report collector pings the default gateway and queries the neighbor table by
interface index before recording `GatewayMac`, so two CGNAT clients that both
use a private gateway like `192.168.0.1` can still prove different routers when
their gateway MACs differ. This keeps the gate useful for CGNAT cases where
different routers can still share one server-observed public IP.

For a stricter public-IP proof, add `-MinDistinctPeerHosts 2` to
`run_two_pc_release_gate.ps1` or `--min-distinct-peer-hosts 2` to
`relay_server_summary_check.py`. For a same-LAN lab smoke only, use
`-AllowSameNetwork` or `--allow-same-network`; do not use that for final NAT
proof.

`no_target` is reserved for frames sent to member IDs that were never present in
the room. Stale frames aimed at a member that just left are ignored so ordinary
shutdown order does not fail the release gate.

For a same-machine two-client smoke log, use `--role combined --room ROOM_NAME`.
