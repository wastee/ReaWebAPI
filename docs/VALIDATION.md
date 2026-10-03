# v0.3.8.3 validation

| Target | Result |
| --- | --- |
| Windows x64 / MSVC | Release build. 24/24 CTest suites passed. REAPER 7.81 passed 138 Cockos comparison/history checks at each of 44.1/96 kHz in stereo, 146 checks at 48 kHz with six channels and 198 with 32 channels. Aggregate and loop/switch stress fixtures passed 125 and 185 checks. |
| Linux x86_64 / GCC, WSL Ubuntu | Release extension and WebKit helper built. 23/23 CTest suites passed. REAPER 7.78 passed 146 Cockos comparison/history checks at 48 kHz with six channels, 125 aggregate checks and 185 loop/switch stress checks. |
| macOS ARM64 / Apple Clang | Release build. 24/24 CTest suites passed. REAPER 7.81 passed 146 Cockos comparison/history checks at 48 kHz with six channels, 125 aggregate checks and 185 loop/switch stress checks. |
| macOS Intel / Apple Clang | Release cross-build succeeded. Native tests cannot launch on the ARM64 host: `Bad CPU type in executable` / error -86. Intel runtime acceptance remains unverified. |
| Linux ARM64 | Shared implementation. Not built or executed locally. |
| SDK | Strict TypeScript contract, bridge, Demo, API inventory, package and release metadata checks passed. |

The Cockos reference fixture processes exactly six seconds of identical Float32 PCM in the official JSFX and the compiled native analyzer. Its test-only JSFX copy exposes frame counts, channel True Peak maxima/clip counts, LUFS-M/S maxima and LRA bounds without changing the DSP. Signals cover silence, sine, near-full-scale sine, impulse, inter-sample peaks, sample clipping, pink noise and a level transition. RMS-M/I, LUFS-M/S/I, LUFS maxima and LRA/bounds match within 0.0005 dB/LU. Channel True Peak matches within 0.000002 linear amplitude and its clip counts match exactly. Live streams verify payloads, peak consistency, stopped history, reset, heterogeneous aggregate channel widths and closure after channel changes. Spectrum remains stereo. Track preroll is excluded from the exact six-second numerical reference.

Native tests cover mono/stereo/6/32-channel Track, Aggregate, Master and Input sources, both integrated modes with default playback-only, 44.1/48/96/192 kHz, 10/30/60 Hz publication and irregular blocks. A real accumulation exceeds 2^24 clipped samples. SDK reconstruction checks 2^24+1, 2^53+1 and 2^64-1 without integer loss. Per-channel and global histories share reset behavior. Binary tests cover stopped history, seek, joining a paused source, reset, accessor refresh, capture gaps, channel/sample-rate changes and reopen. Audio-callback allocation checks pass. Multi-channel hardware paths use a synthetic capture callback because the acceptance hosts do not provide 32-channel hardware.

A Linux differential check against the implementation before this optimization passed 3,145,504 exact comparisons of retained Meter fields, spectrum and waveform across 1/2/6/32 channels and 44.1/48/96/192 kHz. Changed LUFS-M/S/maxima and LRA/bounds are excluded and covered by the official JSFX fixture. Existing audio, MIDI, Native Stream ABI 1, third-party producer, Runtime and cleanup regressions pass.

Local review packages contain Windows x64, Linux x86_64 and macOS ARM64/Intel, with matching ReaPack platform entries and current-version-only `@changelog`. Intel is built but not runtime-validated. Linux ARM64 is omitted.
