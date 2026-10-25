# ReaWebAPI

## English

- Fix unmodified Enter and keyboard focus restoration in captured macOS floating WebViews.
- Fix native keyboard focus after focusing or docking Linux App windows.
- Route unmodified Space from non-editable JSFX Web UI controls through REAPER's shortcut bindings on Windows, macOS and Linux. Add the association option `dawKeyboard: false` to opt out.

## 简体中文

- 修复 macOS 浮动 WebView 开启键盘捕获后的无修饰 Enter 与键盘焦点恢复。
- 修复 Linux App 聚焦及停靠切换后的原生键盘焦点同步。
- Windows、macOS 和 Linux 的 JSFX Web UI 默认将非编辑控件上的无修饰 Space 交给 REAPER 快捷键绑定处理，支持通过关联配置 `dawKeyboard: false` 关闭。
