# v0.3.8.11 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build and 25/25 CTest suites passed. WebView2 passed 80 cold starts floating and 80 docked before the initial frame, with 1,600 native frame samples per mode. Final incremental checks passed. |
| macOS ARM64 / Apple Clang | Release build on the designated host and 25/25 CTest suites passed. The final review binary passed 80 cold starts, 1,600 native frame samples, 213 resize captures and embedded/floating Docker acceptance on an unlocked desktop. Its SHA-256 matches the packaged binary. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. Intel runtime acceptance remains unverified. |
| Linux x86_64 / GCC | Release extension/helper and 24/24 CTest suites passed. REAPER under WSL on an isolated X11 display passed 80 cold starts and 1,600 native frame samples. Four-direction Docker divider dragging passed 480 captures with no white frames. |
| Linux ARM64 | Not built or executed locally for this revision. |

Cold-start cases use fresh browser profiles with dark, light, gradient and delayed external CSS backgrounds. Each case runs 20 times and samples the first 20 visible native frames. The checks require rendered page content, so replacing a white blank surface with a dark blank surface does not pass. On macOS, the same native capture fixture reproduced a blank first frame with v0.3.8.10.

Windows native resize/background, viewport/state, shared Docker and cursor regressions passed. Real REAPER embedded/floating Docker tests passed startup hiding, icons, inactive tabs, two WebViews, close/reopen, focus and bounds. Linux WebKitGTK tests passed background/theme/transparency, JS round-trips, host focus/blur, reparenting, state preservation, reload and independent close. Linux embedded/floating REAPER Docker tests passed saved docking, titles, icons, visibility, two WebViews, close/reopen, focus and bounds.

Runtime tests cover pending presentation, handshake readiness versus visibility, size changes, deferred Docker activation, hide/show and closing during reload cleanup. Startup tests cover obsolete navigation captures and cancellation. The modern SDK build, strict TypeScript contract and generated API verification passed. GitHub Actions was not run because this revision remains uncommitted.

The v0.3.8.10 Linux parent ConfigureNotify sizing, 200 ms stale-size guard, display scaling, X11 sync-counter acknowledgement and socket-readability pump remain unchanged. This revision changes first presentation, preserves visibility intent while pending, defers maximization/activation and briefly paints a page snapshot across SWELL/GTK mapping. Initial X11 background pixmaps are released after the GTK handoff. Normal resize and docking style handling retain their existing paths.

The local review bundle includes Windows x64, Linux x86_64 and macOS ARM64/Intel. ReaPack lists only those binaries. Its changelog and release notes contain only the startup flash fix. No commit, tag or GitHub release was created. Linux native desktop/GPU compositor acceptance, Linux ARM64 and Intel macOS runtime remain outside the verified results above.
