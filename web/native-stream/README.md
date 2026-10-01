# Native Stream Demo

Run `reawebapi-native-stream.lua` with ReaWebAPI v0.3.7.8 or later. Lua returns after opening the window.

Choose PCM, Spectrum, Meter/LUFS, realtime Waveform, MIDI, or a registered stream name. Master/input uses the active hardware device's first two channels. Selected track offers pre-FX media or post-FX channels 1–2, including folder sums and receives. Post-FX capture inserts a temporary pass-through JSFX at the end of the track FX chain, before the fader. Start playback or enable input monitoring for a signal. Detaching the last consumer removes the capture FX. MIDI inputs must be enabled in REAPER preferences. `reagba.video` requires the separate ReaGBA extension and a loaded ROM.

The page receives binary packets over the native transport and renders cached data through `requestAnimationFrame`. Detach affects only this page. Device/display queries and native folder watch are low-frequency controls. See [Native Streams](../../docs/native-streams.md).
