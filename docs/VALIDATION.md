# v0.3.7.7 validation

| Check | Result |
| --- | --- |
| Windows x64 / MSVC | Release extension built. All 22 CTest suites passed. Final plugin ABI check passed. |
| Windows resize / WebView2 | Native background pixels, dark/light themes, transparent and translucent fallback, child paint preservation, ancestor redraw batching, shared/hidden Docker hosts, restoration after undocking/destruction, viewport dimensions and page state passed in `windows_resize`. |
| Windows Docker divider / real REAPER | REAPER 7.78 with the Default 7 theme passed continuous mouse dragging of top, bottom, left and right Docker dividers. No bright page-interior flashes were detected across 360 screen captures. Input values and page animation progress were preserved. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. All 21 available CTest suites passed. |
| Linux resize / WebKitGTK | Background changes, translucent fallback, repeated viewport resizing, state retention, native mapping when shown/hidden/parked/restored, X11 reparenting, host focus, reload and independent close passed. WSLg required `WEBKIT_DISABLE_DMABUF_RENDERER=1` for animation frames. No renderer override is included in the extension. |
| Linux Docker divider / real REAPER | REAPER 7.78 passed XTest dragging of top, bottom, left and right Docker dividers on an isolated Xvfb display in WSL Ubuntu. All 480 captures retained visible page content without sampled white pixels. Actual size changes, viewport dimensions, input retention and animation progress passed. Native mapping, GDK-relative placement and resize pixel preservation fixes are included. The virtual display uses Mesa software rendering with `WEBKIT_DISABLE_DMABUF_RENDERER=1`. |
| macOS ARM64 / AppleClang 17 | Release extension built on macOS 26.0.1 (Apple M4). All 22 CTest suites passed after the final painting fix. |
| macOS resize / WKWebView | Real REAPER 7.74 passed native mouse dragging of top, bottom, left and right Docker dividers, with measured size changes over 100 points in each direction. Across 1,035 native-window captures, no white pixels appeared in the sampled dark-page area. Visible page content, animation progress, viewport dimensions, input state, theme changes and translucent fallback passed. The test also covers a floating Docker and floating WebView resizing before and after each docking cycle. |
| Real REAPER / Windows, macOS and Linux | Existing Docker integration tests passed in embedded and floating modes, including hidden tabs, docking transitions, multiple WebViews, close/reopen, icons, titles and host state. |
| Windows DevTools | Embedded resizing and mode transitions passed up to the existing maximized-window restoration assertion. The same `IsZoomed(inspector)` assertion fails with the pre-change window implementation on WebView2 154.0.4258.37. The full DevTools suite is not a pass. |
| macOS Intel | Final Release extension compiled successfully. Runtime acceptance was not performed because Rosetta is unavailable on the ARM64 test machine. |
| Linux ARM64 | Platform implementation updated. Not built or executed locally for this version. |
| Release metadata | 20 release tests and 4 version tests passed. Generated `ReaWebAPI.ext` uses v0.3.7.7 and only this version's resize changelog. ReaPack ZIP contract tests use synthetic binary fixtures that are never distributed. |

The complete five-target ReaPack archive requires the platform builds. GitHub release notes and the packaged `@changelog` are generated from [release notes](release-notes.md). No GitHub release has been published by this validation.

Native resize automation and sampled captures do not cover every display scale, graphics backend or frame during manual dragging. See the [host checklist](SMOKE_TEST.md).
