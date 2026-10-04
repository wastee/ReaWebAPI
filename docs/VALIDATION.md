# v0.3.8.4 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. Real WebView2 passed modules, fetch, HEAD/ranges, Canvas, IndexedDB, both Worker kinds, origin isolation and storage across host-process restarts. Virtual/HTTP navigation and Vite/openDev/HMR passed. |
| Linux x86_64 / GCC | Release extension and WebKit helper built. 23/23 CTest suites passed. Isolated REAPER passed manifest IDs, launcher fallback, duplicate-root rejection, restart persistence and Unicode/space/#/% directory moves. WebKitGTK Inspector split, float/dock, shortcuts, hide/show and page-state retention passed under Xvfb. |
| macOS ARM64 / Apple Clang | Release build on macOS 26.0.1. 24/24 CTest suites passed. REAPER 7.81 passed all nine App/phase cases across initial launch, restart and Unicode/space/#/% directory moves. Manifest IDs, Lua fallback, duplicate-root rejection, isolated localStorage/IndexedDB, modules, Workers, CSP, HEAD/ranges, Canvas, animation frames and source locations passed without skipped checks. Two active production Apps created no REAPER TCP listener. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. The ARM64 host cannot execute Intel binaries (`Bad CPU type in executable`), so Intel runtime acceptance remains unverified. |
| Linux ARM64 | Not built or executed locally for this revision. |
| SDK | TypeScript/Vite build, manifest validation, package contents and current-version-only ReaPack changelog checks passed. |

Resource tests cover manifest precedence, Lua filename normalization, conflicting roots, moved roots, metadata upgrade, MIME, conditional requests, byte ranges and path boundaries. Windows junctions and POSIX symlinks are exercised. Windows file-symlink cases require a privilege unavailable in this environment.

Windows WebView2 154.0.4258.53 Inspector layout checks fail for both virtual and original file-page controls: renderer bounds can be unavailable, and maximized-window restoration is not verified. No Inspector implementation was changed. Sources breakpoints and complete Network-pane acceptance remain unverified on Windows and Linux.

macOS Inspector passed Embedded/Floating, shortcuts, Console/Elements retention, resizing, hide/show, multi-window isolation and host migration. Network identified HTML/CSS/JS/module/fetch resources. Sources retrieved script contents and hit a breakpoint at its virtual URL. Source stacks and the Inspector connection remained valid after reload.

Native cookies are unavailable for the tested custom schemes. Shared browser profiles and cookie settings remain unchanged. HTTP-origin browser data is retained without automatic migration.

The local review bundle includes Windows x64, Linux x86_64 and macOS ARM64/Intel. Its ReaPack platform entries match those binaries. Linux ARM64 is omitted.
