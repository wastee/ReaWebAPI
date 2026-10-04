# v0.3.8.5 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. Isolated REAPER / WebView2 passed all 17 hardware-stream checks. |
| Linux x86_64 / GCC | Release extension and WebKit helper built. 23/23 CTest suites passed. Isolated REAPER / WebKitGTK with PulseAudio passed all 17 hardware-stream checks. |
| macOS ARM64 / Apple Clang | Release build on the designated macOS host. 24/24 CTest suites passed. Isolated REAPER / WKWebView with CoreAudio passed all 17 hardware-stream checks. |
| macOS Intel / Apple Clang | Release cross-build and ad-hoc signature verification passed. Intel runtime acceptance remains unverified. |
| Linux ARM64 | Not built or executed locally for this revision. |

Real-host checks cover concurrent Master Audio/Spectrum/Meter/Waveform delivery, stereo compatibility, meter history, payload layout, peak amplitude, RMS/LUFS, exact clip counters and reopening. All three hosts reported zero hardware-hook channel counts and successfully used the device fallback. Their devices were stereo. Mono and multichannel fallback are covered by simulated-host tests.

Meter regression cases cover missing callback channel counts with 1, 2, 6, 16, 32 and 40 device channels, independent input/output counts, positive callback-count precedence, the 32-channel cap, missing buffers and layout changes. The pre-fix producer fails the added fallback test by closing the stream. The corrected producer passes on all three platforms.

On Linux, 48 before/after cases produced byte-identical Audio/Spectrum/Waveform payloads, including mono, stereo, multichannel and missing-right-buffer cases. The existing allocation test confirms zero allocations during PCM capture. Public interfaces, Native Stream ABI 1 and the `13*C+14` Meter payload remain unchanged.

The local review bundle contains Windows x64, Linux x86_64 and macOS ARM64/Intel. ReaPack entries match these binaries and omit Linux ARM64. Release notes and `ReaWebAPI.ext` changelog contain only the v0.3.8.5 hardware-channel fallback fix.
