# Runtime Diagnostics

**English** | [简体中文](diagnostics.zh-CN.md)

Windows, macOS and Linux share a persistent diagnostic logger for Apps and JSFX WebUI. Configure it in **Preferences > Plug-ins > ReaWebAPI > Diagnostics**.

| Setting | Effect |
| --- | --- |
| Enable App debug logging + Target App ID | Enables INFO/DEBUG for the exact App ID and its associated Runtime events, across all its instances |
| Enable Runtime debug logging | Enables INFO/DEBUG for Runtime infrastructure, including events with no known App. Does not enable other Apps' JavaScript debug/info records |
| Open diagnostics log folder | Opens the log directory in the system file manager |

Both switches default to off and work independently. Apply/OK persists settings in `ReaWebAPI.ini` and updates active instances immediately. Configure the target before launching an App to capture initialization after its existing App ID becomes available. No App configuration changes are needed. The ID is the existing `reaper.app.getId()` / `webRuntime.appId`, including generated JSFX IDs. An empty target cannot enable App DEBUG.

Logs are UTF-8 JSON Lines under `<REAPER resource path>/ReaWebAPI/Diagnostics/`. Read them with any text editor. `runtime.log` contains Runtime events. `app-<appId>.log` contains the App's events, including associated Runtime events. Each file is limited to 2 MiB, with three numbered backups. The directory retains at most 64 managed log files, deleting the oldest when needed. Identifiers are preserved without filename truncation, restricted to existing lowercase ASCII App ID characters and a maximum of 240 characters for logging. Invalid diagnostic targets do not change App identity.

Every record has UTC `time`, `level` (`ERROR`, `WARN`, `INFO`, `DEBUG`), `scope`, `event`, diagnostic `sessionId`, `appId`, `windowId`, `result` and `threadId`. The diagnostic session identifies one Runtime lifetime and is independent of authentication and document tokens. Window IDs distinguish simultaneous instances. Known JSFX context adds `fxGuid`, `fxIndex`, `fxLocation`, `fxSessionId` and `activation`. API execution may include `method` and `durationMs`. Early events use an empty App ID and window ID 0.

Events cover creation, bridge readiness/navigation, closing and release, instance binding, API failures, JavaScript log severity, host-message channel activity and failures, lifecycle timeout, and JSFX identification, parking, attachment, invalidation and observed native-session changes. Track, input FX, Item/Take and nested Container FX use the existing resolver. Encoded FX indices retain the host's Container addressing.

Only WARN/ERROR are persisted by default. Free-form messages, URLs, arguments, message bodies, authentication tokens and audio samples are excluded, including text supplied to `reaper.debug.log/warn/error`. Existing in-memory logs and console output retain their current behavior. Repeated events are limited to 20 per second per App/window/event, with a `suppressed` count on the next emitted record. Writes flush on the UI thread. Calls from other threads are rejected before I/O. Diagnostic I/O failure is ignored by the Runtime.

REAPER does not expose a JSFX compilation callback. Native-channel session changes, host FX revision notifications, parameter-count changes and binding invalidation are observations, not proof of compilation. The Runtime also cannot observe an arbitrary Lua script's start or exit. `lua.channel.begin/end` describe observed use and release of the existing host-message channel, not Lua process lifetime. A stopped Lua polling loop without a window close has no definitive exit signal. A flushed record survives process termination, but this is not a power-loss durability guarantee.

The Demo's Diagnostics panel and `reaper.debug.getDiagnostics()` remain snapshots. Persistent logs require no viewer to stay open.
