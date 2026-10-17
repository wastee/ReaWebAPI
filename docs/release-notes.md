# ReaWebAPI

## English

- Prevent stale JSFX parameter replies and snapshots from overwriting newer host values.
- Reset JSFX initialization, presentation and timeout state on reload, and restore native controls after failed initialization.
- Extend JSFX Web UI to take, input and monitoring FX and nested containers with GUID validation and verified editor ownership.
- Fix JSFX Web UI sizing and presentation in containers and floating/chain transitions, including duplicate pages and macOS position drift. Retain the page when list selection clears and prevent native-control and small-window flashes when opening or floating FX.
- Preserve the native FX toolbar layout and input/output meters. Fit Web UI between the meters, preserve the native bottom inset and respect `options:no_meter`.
- Correct gesture rendering and successive-edit handling, and preserve native range pointer tracking on macOS.

## 简体中文

- 修复 JSFX 参数旧回复和旧快照覆盖较新宿主值的问题。
- 修复 JSFX 重载时的初始化、显示和超时状态重置，初始化失败后恢复原生控件。
- 将 JSFX Web UI 扩展至 Take、输入、监听 FX 和嵌套容器，校验 GUID 与编辑器归属。
- 修复容器及浮动、效果链切换中的 Web UI 显示和尺寸，包括重复页面与 macOS 位置漂移。取消列表选中时保留页面，修复打开及浮动 FX 时旧控件和原生小窗口闪现。
- 保留原生 FX 标准栏布局及输入、输出电平表，Web UI 位于两表之间，保留原生底部间隔并遵循 `options:no_meter`。
- 修正手势渲染和连续编辑处理，并保留 macOS 原生滑块的指针跟踪。
