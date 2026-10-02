# v0.3.8.0 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release extension built. All 23 CTest suites passed. Final audio, producer, aggregate-source and stream transport checks passed. Real REAPER 7.78 acceptance passed. |
| macOS ARM64 / AppleClang | Release extension built on the supplied Mac. All 23 CTest suites passed. Final aggregate-source and producer checks passed. Real REAPER 7.81 acceptance passed. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. All 22 CTest suites passed. Final audio, producer, aggregate-source and stream transport checks passed. Real REAPER 7.78 acceptance passed. |
| macOS Intel | Release extension compiled. Runtime acceptance was not performed. |
| Linux ARM64 | Uses the same platform-independent aggregate implementation. Not built or executed locally. |
| SDK and release metadata | TypeScript contract and 730 API definitions verified. Release tests verify the version and exact current-release `@changelog` inside the generated ReaPack ZIP. |

The isolated REAPER acceptance covers `audio`, `spectrum`, `meter` and `waveform`, single-track equivalence with omitted/false `aggregate`, nested folders, receives, track/folder mute, solo and solo defeat, muted items, sums above unity, phase cancellation, duplicate/cyclic routes, live routing changes, playback time, upstream/root deletion, one public stream and last-consumer cleanup. Stream operations preserve project state and Undo. Track/send gain, pan, phase and channel mapping remain outside aggregate-source semantics.

Native tests additionally verify synchronized read parameters, ordinary solo versus solo-in-place routing, reused track identities, project changes, accessor errors and accessor reuse/destruction. Existing hardware capture remains allocation-free in its producer regression test.

Review packages are available for Windows x64, macOS ARM64/Intel and Linux x86_64. Complete five-target ReaPack assembly requires the Linux ARM64 build. Release tests use synthetic binary fixtures that are never distributed. GitHub release notes and `ReaWebAPI.ext` are generated from [this version’s notes](release-notes.md). No release was published.
