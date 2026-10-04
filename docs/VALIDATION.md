# v0.3.8.7 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. REAPER 7.81/x64 passed all 36 aggregate-meter acceptance checks. |
| macOS ARM64 / Apple Clang | Release build on the designated host. 24/24 CTest suites passed. REAPER 7.81/macOS-arm64 passed all 36 aggregate-meter acceptance checks. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. Intel runtime acceptance remains unverified. |
| Linux x86_64 / GCC | Release extension and WebKit helper built. 23/23 CTest suites passed. REAPER 7.78 under WSL completed the initial five-second empty-source RMS/LUFS checks. Full runtime acceptance is limited by playback-clock discontinuities affecting both aggregate and non-aggregate meters. |
| Linux ARM64 | Not built or executed locally for this revision. |

The aggregate regression covers empty buses, folders and upstream tracks with an unchanged project revision, 6.4 seconds of continuous PCM, RMS-M/I and LUFS-M/S/I. It also checks media insertion/removal, edits in a nonempty accessor that returns no audio for the current block, and routing changes.

Real REAPER acceptance checks continuous duration and RMS/LUFS against a non-aggregate reference, then verifies media insertion/removal, media edits, route mute/unmute, seek, stopped history, playback restart and stream cleanup. Windows and macOS passed with the final harness. On Linux, a captured playback position advanced approximately 189 ms in 67 ms of wall time while the project revision remained unchanged, correctly resetting all four meters.

The local review bundle contains Windows x64, Linux x86_64 and macOS ARM64/Intel binaries. ReaPack entries match these binaries and omit Linux ARM64. Release notes and the ReaPack changelog contain only the aggregate RMS/LUFS empty-accessor fix.
