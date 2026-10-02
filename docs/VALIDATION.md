# v0.3.8.1 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release extension built. All 23 CTest suites passed. Real REAPER 7.81 passed 125 native-stream checks. |
| macOS ARM64 / AppleClang | Built on the supplied Mac. All 23 CTest suites passed. Real REAPER 7.81 passed 125 native-stream checks using the configured CoreAudio device. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. All 22 CTest suites passed. Real REAPER 7.78 passed 125 native-stream checks. |
| macOS Intel | Release extension compiled. Runtime acceptance was not performed. |
| Linux ARM64 | Uses the same platform-independent implementation. Not built or executed locally. |
| TrackAxis / Windows | All 35 real-REAPER acceptance checks passed. All 14 audio frontend tests passed, including an already-closed stream retaining its error status. |
| Release metadata | Version and release tests verify the generated ReaPack ZIP and its current-release-only `@changelog`. |

Native regressions cover invalid PCM isolation, continued delivery to an existing healthy stream, opening streams after an analysis failure, one shared accessor/read for parallel analyses, bounded aggregate continuation, project-change invalidation, and accessor cleanup on the host thread. Hardware capture remains allocation-free.

Each platform's real-REAPER run includes 20 consecutive three-stream source switches, ordinary and aggregate PCM, spectrum, meter and waveform, routing and mute/solo changes, phase cancellation, playback position, root/upstream deletion, and last-consumer cleanup. Streams preserve project state and Undo. Aggregate work yields between source tracks. A single REAPER accessor call cannot be interrupted.

Review packages cover Windows x64, macOS ARM64/Intel and Linux x86_64. The review ReaPack ZIP includes these four targets and matching metadata. The five-target release configuration is unchanged and additionally requires Linux ARM64. Release test binaries are synthetic fixtures and are never distributed. No release was published.
