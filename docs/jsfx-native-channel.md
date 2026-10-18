# JSFX native channel

[简体中文](jsfx-native-channel.zh-CN.md) · [Web UI integration](jsfx-webui.md)

The optional native channel exposes JSFX telemetry, structured state, commands and binary resources through the existing `jsfx` service. It uses the loaded REAPER JSFX engine and requires `reaweb_jsfx.inc` in participating effects. Parameter-only Web UIs do not require DSP changes.

## Transport

```text
JSFX @block / @sample
  ↔ named gmem slots
  ↔ private helper in the same JSFX engine
  ↔ extension-owned staging buffer
  ↔ existing Service / Stream transport
  ↔ Hosted WebView
```

The bridge validates `JesusonicAPI` version `0xf0f01091` and the required host EEL exports. Its private helper has no project track, FX slot or window. Host EEL accesses staging only after the helper returns. It never accesses the producer's live payload directly. Unsupported engines retain parameter mode and reject native capabilities with `UNSUPPORTED`.

`JesusonicAPI` is an exported REAPER-facing interface, not a stable third-party Extension SDK contract. Host upgrades require compatibility validation. The bridge does not replace the JSFX engine, patch callbacks or access private instance layouts.

## Instance binding

The host validates project lifetime, owner GUIDs, FX GUID and source identity. An explicitly reserved transport slider supplies the EEL generation.

1. The host reads the target slider and posts a challenge in an allocated gmem slot.
2. The live EEL instance matches its generation, claims the slot and allocates a fresh generation.
3. JSFX publishes that response in its transport slider. The host accepts the lease only when the target's readback matches the slot response.

A copied or restored slider value alone cannot establish a binding. Initial connection and reconnection never call the host parameter setter, create Undo entries or mark the project dirty. If DSP is not scheduled, the handshake waits. The bridge does not start playback or change the audio device.

Serialization restore in the same VM renews its owned slot. Recreated VMs establish a new binding after a bounded renewal interval. New generations invalidate stale operations, snapshots and transfer tickets. Successful durable state/resource changes still notify REAPER and participate in Undo. That notification is separate from connection setup.

Call `sliderN=reaweb_jsfx_handshake(sliderN)` from `@slider`, before servicing each `@block`, and after SDK serialization. Do not automate, repurpose or send UI edits to this reserved slider.

## Synchronization and bounds

The [official JSFX implementation](https://github.com/justinfrankel/jsfx/blob/main/jsfx/effectproc.cpp) uses a module-wide mutex for `atomic_*`. Both bridge endpoints execute in that engine, so ownership transitions share the same synchronization primitive. Payload copies occur outside the mutex while the slot is exclusively owned. This is not a lock-free transport.

- Audio-side slot acquisition makes one attempt. Busy/full queues drop telemetry or defer control work without waiting for the browser.
- Buffers are preallocated and touched. Each telemetry packet holds at most 4096 values, with eight packets per instance. Control payloads are bounded to 8192 values.
- The private helper initializes its namespace once. Unsubscribed native queues are polled with the 20 ms status interval. DSP service skips command acquisition when the block snapshot contains no pending command.
- State serialization owns a separate gate. Audio processing skips SDK state work while serialization holds it. Saving may wait for an in-progress bounded state copy.

The declared gmem range is `[base, base + 2170944)`, containing 32 instance slots and private staging. The SDK also reserves 32768 local words. State/resource banks must be disjoint from that local range and from each other.

A shared gmem name intentionally selects shared memory. It does not identify an FX instance or isolate arbitrary other JSFX code. Preserve an existing effect's namespace and declare an unused range. See the [official shared-memory reference](https://www.reaper.fm/sdk/js/basiccode.php).

## Execution contract

Streams publish explicitly selected data. State writes use schema validation and revision checks. Commands complete only after DSP acknowledgement. Resource uploads stage and validate data before a block-boundary swap. Persistent data uses the effect's `@serialize` section.

Bypass, offline state, stopped devices and unscheduled take/input FX may suspend execution. Deadlines do not imply that a running command has stopped. Continuous actions and owned MIDI notes are released when DSP resumes. Page visibility and DSP scheduling are separate concerns.

Audio performance depends on the host, device and workload. Callback processing time, publication-to-consumer latency and device-reported latency measure different paths. `GetUnderrunTime` exposes the latest host-reported underrun timestamps when supported, not an exact event count. See the [official API reference](https://www.reaper.fm/sdk/reascript/reascripthelp.html#GetUnderrunTime).
