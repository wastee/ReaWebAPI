# v0.3.7.3 validation — 2026-09-29

| Check | Result |
| --- | --- |
| Windows x64 / MSVC | Release extension built. All 22 CTest suites passed. Native Preferences integration target compiled. Real REAPER 7.78 acceptance passed. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. All 21 available CTest suites passed. Real REAPER 7.78 acceptance passed. Lua CLI is unavailable. |
| macOS ARM64 / AppleClang 17 | Release extension built on Apple M4. All 22 CTest suites passed. Real REAPER 7.74 acceptance passed. |
| macOS Intel / AppleClang 17 | Release extension cross-compiled for x86_64. Runtime acceptance was not executed. |
| Linux ARM64 | Existing CI target retained. Not built or executed locally. |
| External protocol | Authentication, allowlist, argument validation, Batch references/errors, binary and Float64 values, session handles, services/events/streams, disconnect cleanup, token rotation, enable/disable, queue limits and bind failure passed over real loopback sockets. |
| Existing interfaces | Core, Runtime, native ABI, services, monitors, streams, producers, tasks, JavaScript bridge, Demo and resource contracts passed. All 730 API mappings verified. |
| Real REAPER / Windows x64, Linux x86_64 and macOS ARM64 | External calls worked before creating a WebView. External Client and legacy WebView then shared API state and native events. Authentication, Batch references, service invocation, binary delivery, disconnect cleanup and cross-session handle rejection passed in isolated profiles. |
| SDK / release contracts | 41 Python contracts passed, including SDK content and links, platform metadata, ReaPack layout and generated `ReaWebAPI.ext` changelog matching only this version's release notes. Release tests use synthetic binary fixtures that are never distributed. |

The full five-target ReaPack archive requires the platform CI builds. GitHub release notes and the packaged `@changelog` are generated from [release notes](release-notes.md). No GitHub release has been published by this validation.

See the [host acceptance checklist](SMOKE_TEST.md#external-clients) for the opt-in real REAPER test and remaining UI checks.
