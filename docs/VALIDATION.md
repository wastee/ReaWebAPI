# v0.3.8.6 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. WebView2 with a mock host passed 11 native icon checks, including the Demo SVG, replacement/removal, docking, visibility and explicit override persistence after reload. |
| Linux x86_64 / GCC | Release extension and WebKit helper built. 23/23 CTest suites passed. Isolated REAPER / WebKitGTK passed native icon checks with embedded and floating Dockers. |
| macOS ARM64 / Apple Clang | Release build on the designated macOS host. 24/24 CTest suites passed. Isolated REAPER / WKWebView passed native icon checks with embedded and floating Dockers. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. Intel runtime acceptance remains unverified. |
| Linux ARM64 | Not built or executed locally for this revision. |

All native icon checks load local resources through `reaweb:`. macOS and Linux checks inspect native icon pixels and cover visibility, replacement while hidden, two WebViews, close/reopen, container restoration, main-window icons, focus and bounds.

The 10 favicon regression cases cover Windows and WebKit bridges with production `reaweb:` and development HTTP origins, relative and Unicode paths, declaration replacement/removal, media changes, data/blob URLs, failed loads and explicit overrides. The two production-origin cases fail before the fix and pass after it.

The local review bundle contains Windows x64, Linux x86_64 and macOS ARM64/Intel. ReaPack entries match these binaries and omit Linux ARM64. Release notes and `ReaWebAPI.ext` changelog contain only the v0.3.8.6 favicon fix.
