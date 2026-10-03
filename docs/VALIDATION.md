# v0.3.8.2 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release extension built. All 23 CTest suites passed. Real REAPER 7.81 passed 185 nested-folder stress checks and an additional 200 source switches. |
| macOS ARM64 / AppleClang | Built on the supplied Mac. All 23 CTest suites passed. Real REAPER 7.81 passed 185 stress checks using the configured CoreAudio device. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. All 22 CTest suites passed. Real REAPER 7.78 passed 185 stress checks. |
| macOS Intel | Release extension compiled. Runtime acceptance was not performed. |
| Linux ARM64 | Shares the platform-independent implementation. Not built or executed locally. |
| TrackAxis / Windows | 17 audio frontend tests, 35 real-REAPER acceptance checks and four injected handshake/disconnect recovery checks passed. |
| Release metadata | Version and release tests validate the ReaPack ZIP and its current-release-only `@changelog`. |

All three runtime platforms also passed 125 compatibility checks covering ordinary/aggregate PCM, analysis formats, mute/solo and routing changes, phase cancellation, deletion, project state and Undo preservation.

The stress project has 81 tracks, including 16 nested folder branches and 48 audio children. The parent remains selected during continuous playback, followed by 60 parent/child source switches with three simultaneous analyses. Windows and macOS completed 20 playback loops, Linux completed eight. Each run ended with zero consumers, no handshake rejections and no expired tickets.

Native regressions cover recovery of three shared analyses after an isolated invalid PCM block, ordinary and aggregate sources, bounded failure for persistent invalid PCM, and connection/detach progress under slow sampling. TrackAxis retries transient connection failures at most three times and cancels retries on source changes or closure.

Review packages contain Windows x64, macOS ARM64/Intel and Linux x86_64, with a matching four-target ReaPack descriptor. The five-target release configuration still requires Linux ARM64. No release was published. Single REAPER accessor calls remain non-interruptible.
