# IPC and connection lifecycle

The current applications use `sony-transport` for platform Bluetooth,
`sony-protocol` for framing/session/commands, and `sony-core` for device state,
connection policy, and IPC. Qt Quick is the only desktop UI.

## Ownership and recovery

`sonyd` acquires its IPC endpoint before making Bluetooth connections. A socket
polling thread handles at most 16 clients; a separate executor serializes commands
and maintenance. Commands waiting for execution are bounded to 16. Completed
requests close their connection, matching the existing CLI client. Pipelined
requests are rejected. Individual hardware operations can delay later commands;
they do not block socket acceptance or the GUI thread.

`DeviceService::startAutoConnect(address)` enables discovery and retries only
for a nonempty, valid Bluetooth address. Empty addresses disable automatic
connection. Its `tick()`
method runs on the owning executor, using an injectable steady clock. Discovery
sorts system-connected devices first for presentation, then uses stable address
ordering. Recovery always filters to the selected address; unavailable targets
never fall through to another headset. Failures retry after 1, 2, 4, 8, 16, then 30
seconds. Success resets the delay. Manual disconnect suspends retries; connect
resumes them. EOF/fatal session errors mark the session disconnected; an ordinary
receive timeout does not. The owner joins the reader before reconnecting.

The Windows GUI enables `RequireSystemConnection`. Software disconnects persist
`PausedByUser`; a lost system/control connection pauses further attempts. Their
normalized addresses are saved in `connection/pausedAddresses`. A system pause
resumes only after an affirmative Windows connection for the same selected
headset. A manual software pause on an unchanged Windows link remains effective,
including after restart; after observing a real system disconnect it can follow
the headset's next Windows connection. This authorization is persisted in
`connection/resumeOnSystemConnectionAddresses`. Explicit Connect clears only that
target's pause. Legacy records lacking the new key migrate once to passive
observation, so old permanent pauses cannot mask an already connected headset.
Unknown metadata cannot authorize a new connection or invalidate a healthy control
session. Startup without a system link waits without manufacturing a disconnect.
Other owners retain the legacy policy. Audio connections and source selection
remain under Windows/headset control.

An ACK wait timeout invalidates the control session immediately. Two consecutive
query response timeouts also invalidate it, even if Windows still reports the
Bluetooth transport connected after a multipoint phone-call handoff. ACKs,
successful ACK-only writes, and unrelated notifications do not reset query
failures; a matching completed query does. An isolated optional query timeout
therefore keeps the session usable. Recovery closes the old transport and opens
a fresh session for the same selected address. It does not replay a failed
power-off command. Detection follows maintenance/request deadlines; it does not
claim to observe a phone call or guarantee immediate recovery at hang-up.

The GUI remembers the last accepted explicit selection, including failed
initial connection attempts and later successful recovery, in
`connection/selectedAddress`. First use waits for a device-list selection.
`sonyd --device <address>` supplies its startup target; no address means idle.
Changing the automatic target immediately closes the previous control session.
JSON and legacy command handlers validate the active selection before device
operations. The GUI attaches `targetAddress` to queued writes, rejecting a request
created for a previous headset after a selection change. Name/OUI filtering
identifies discovery candidates only; a Sony OUI is not headphone identity.

Power-off is a separate service operation from manual disconnect. After a
successful power-off command, the service closes the control session and enters
`waiting_for_power_on`. It waits five seconds for shutdown, then checks discovery
once per second for the same headset. A known disconnected system Bluetooth link
suppresses RFCOMM attempts. When the system link returns, the service reopens the
control session; failed attempts retain the usual retry backoff. Platforms that
do not expose the system connection hint fall back to reconnect attempts after
the shutdown delay under the legacy policy. The Windows GUI requires an
affirmative system connection result, and persists the waiting target in
`connection/waitingForPowerOnAddress`. Unknown/enumeration failure never
authorizes a Windows automatic connection. Manual disconnect cancels this
waiting state; normal program exit does not create a pause.

Known connected sessions rotate settings reads in separate maintenance steps,
one step every five seconds and roughly 45 seconds per rotation when the device
answers promptly. Firmware is cached per connection. Battery refreshes are due
about 30 seconds after a valid read/notification, in both foreground and tray.
They are serviced before queued writes and between protocol requests.
Unsupported profile features are not probed. Timeouts extend
these intervals rather than accumulating overlapping requests. V1 does not send
the V2 battery opcode; it reads battery, noise control and equalizer through its
own legacy opcodes. Protocol similarity is not hardware verification.

`sonyctl --direct` refuses to run while a daemon is reachable at the selected socket.
This also applies to a disconnected daemon that may be reconnecting. Stop the daemon
before starting a direct session. Separate custom endpoints and third-party Bluetooth
clients are not coordinated by this check.

## Endpoint protection

The default Unix socket is `$XDG_RUNTIME_DIR/sony-device-center.sock`. If that variable
is unset, Linux uses `/tmp/sony-device-center-<uid>/sony-device-center.sock`; macOS uses
`/private/tmp/sony-device-center-<uid>/sony-device-center.sock`.

The immediate parent must be a real, user-owned directory with mode `0700`.
Symlink components, foreign endpoints, non-socket files, and overlong paths are
rejected. A missing final directory can be created by the server. The socket is
`0600`, and both peers verify their effective user IDs. Custom `--socket` paths
follow the same rules. An invalid configured runtime directory fails explicitly
rather than selecting a less protected location.

A persistent sibling `.lock` file prevents duplicate daemons and concurrent stale
socket reclamation. It is deliberately retained after shutdown. Startup also
checks for older listeners that do not take the lock. Shutdown only removes the
socket inode created by that server.

Limits: 16 KiB request lines; 256 KiB responses; five-second absolute read/write
deadlines; 30-second queued-command deadline. JSON nesting is limited to 32.
Broken pipes and partial writes are handled without process-wide signal changes.
Windows IPC remains unsupported and Windows applications use direct Bluetooth.

## Structured protocol, version 1

Requests and responses are one UTF-8 JSON object per line. JSON is handled by the
vendored MIT-licensed nlohmann/json v3.12.0 header, without a Qt dependency in core.
The existing textual CLI commands and response encoding remain supported.

```json
{"version":1,"id":42,"method":"ambient","params":{"level":12,"focusOnVoice":true,"targetAddress":"AA:BB:CC:DD:EE:FF"}}
```

Responses echo the ID and version. Success returns `{"ok":true,"data":...}`;
failure returns `{"ok":false,"error":{"code":"...","message":"..."}}` alongside
those fields. IDs are integers or strings. Unsupported versions return
`VersionMismatch`. Syntax/type/range failures return `InvalidRequest`; device
errors preserve `SonyErrorCode` names. No automatic retry of a failed write occurs.

| Method | Parameters | Result |
| --- | --- | --- |
| `snapshot` | none | Complete cached state |
| `devices` | none | Discovery list with nullable paired/systemConnected hints |
| `connect` | address, optional name | State after connection |
| `disconnect` | none | State after explicit disconnect |
| `anc`, `dsee`, `speakToChat`, `adaptiveVolume` | enabled: boolean | State after targeted readback |
| `ambient` | level: 1–20, optional focusOnVoice | State after targeted readback |
| `eqPreset` | preset: supported numeric code | State after targeted readback |
| `eqCustom` | clearBass: −10–10, bands: five integers −10–10 | State after targeted readback |
| `autoPowerOff` | index: 0–5 | State after targeted readback |

Snapshots include connection state, selected address/model, profile capabilities,
battery main/left/right/case and charging, noise mode/level/voice focus, EQ preset and
bands, audio settings, codec, and firmware. Per-feature metadata records
`availability` (`unknown`, `unsupported`, `valid`, `stale`), `lastSuccessMs` (Unix
milliseconds, zero if never confirmed), and `error`. Missing battery values are
JSON null. A transport ACK confirms acceptance, not an independently verified
audio effect; model-specific protocol issues must still be tested on hardware.

Live SonyDevice sessions discard unmatched replies rather than retaining them to
confirm a later request. Notifications still update confirmed state immediately.
The protocol provides no general transaction identifier: a delayed frame arriving
during another pending request with the same opcode and subtype cannot always be
distinguished. Buffered reply replay remains available only for compatibility
callers that use the session's default compatibility policy.

## Qt worker and presentation

`DeviceBackend` coordinates a bounded 32-command queue on a dedicated QThread;
a persistent second Qt thread performs serial IPC/direct-service I/O.
`DeviceCenterController` contains GUI-thread caches only. Startup renders
immediately. Direct notifications publish immutable snapshots without waiting
for I/O completion; identical snapshots are suppressed. Daemon
snapshots are polled once per second. State locks are released before event
listeners run. Shutdown cancels I/O, removes subscriptions, and joins the reader
and Qt workers. Windows connect/send/receive and protocol busy waits are cancellable.

Device actions show pending state and keep their previous confirmed value until
success from a targeted readback. Adjacent unsent slider inputs are coalesced;
discrete actions retain order and queue overflow has explicit feedback.
Errors remain visible until another action or explicit dismissal.
Generation/session/revision checks discard obsolete or out-of-order snapshots.
Transient read failure preserves confirmed values with stale quality; pause or
real disconnect does not present old battery data as live. Unknown battery is shown
as a dash/Unknown; unknown noise mode is not rendered as Off; codec is never guessed.
An unavailable or incompatible daemon does not trigger a competing direct session.

## Validation

Catch2 tests cover endpoint protection, malformed/idle clients, concurrent client
service, reconnect timing/selection, JSON escaping and versioning, and V1 safety.
Qt tests inject services to cover slow I/O, failed actions, notifications, and
shutdown. A Linux offscreen smoke test starts the simulated daemon, exercises the
actual CLI and structured socket, checks the direct-mode guard, and loads Main.qml.
CI requires Qt on Linux and Windows; Unix socket tests explicitly skip Windows.

Run `cmake --build build --parallel` followed by
`ctest --test-dir build --output-on-failure`. Unix integration tests need permission
to bind local sockets. Physical-headset, native Windows, and native macOS validation
must be recorded separately; the simulator does not establish model compatibility.
