# ReaWebAPI

## English

- Add timer-driven frame events and `ReaWeb_SetFrame` for native transport samples and replaceable Lua content on Windows, macOS and Linux.
- Reduce Lua message latency by dispatching pending WebView requests during receive polling.
- Coalesce Demo volume and pan writes and render the latest frame with `requestAnimationFrame`.
- Validate JavaScript event names against native capabilities, including `devicesChanged` and `frame`.

## 简体中文

- 在 Windows、macOS、Linux 上新增定时器驱动的帧事件和 `ReaWeb_SetFrame`，支持原生播放状态采样及可替换的 Lua 内容。
- 在接收轮询时分派待处理的 WebView 请求，降低 Lua 消息延迟。
- Demo 合并音量和声像写入，通过 `requestAnimationFrame` 绘制最新帧。
- JavaScript 依据原生能力列表校验事件名称，支持 `devicesChanged` 和 `frame`。
