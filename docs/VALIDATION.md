# v0.3.8.8 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. Real REAPER 7.81/x64 with WebView2 154.0.4258.53 reproduced the Arrange View cursor failure with v0.3.8.7 and passed with v0.3.8.8. |
| macOS ARM64 / Apple Clang | Release build on the designated host. 24/24 CTest suites passed. REAPER 7.81/macOS-arm64 passed WebView/native API coexistence, events, services, binary streams and cleanup acceptance. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. Intel runtime acceptance remains unverified. |
| Linux x86_64 / GCC | Release extension and WebKit helper built. 23/23 CTest suites passed. REAPER 7.78 under WSL passed WebView/native API coexistence, events, services, binary streams and cleanup acceptance. |
| Linux ARM64 | Not built or executed locally for this revision. |

With Windows **Hide pointer while typing** enabled, the real REAPER comparison keeps the pointer in Arrange View while sending keyboard input to a ReaWebAPI text field. The old binary leaves the cursor handle null through repeated pointer movement. The new binary hides the cursor during typing and restores visibility and the native cursor handle on movement. Twenty consecutive typing/movement cycles passed with the native cursor shape and keyboard focus preserved. The system option and pointer position are restored after testing.

The Windows WebView2 integration test checks default/sRGB environments, repeated text input, native cursor shape, docking/undocking, reload, multiple windows, close/reopen, CSS `cursor: none`, native mouse capture and the system option on/off in fresh browser environments. macOS and Linux retain their existing native cursor handling. Interactive Arrange View pointer acceptance on those two platforms remains unverified.

The local review bundle includes Windows x64, Linux x86_64 and macOS ARM64/Intel builds. ReaPack lists only those available binaries and omits Linux ARM64. Release notes and `ReaWebAPI.ext` changelog contain only this cursor fix. No repository commit or GitHub release was created.
