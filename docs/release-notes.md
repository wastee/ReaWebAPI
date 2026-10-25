# ReaWebAPI

## English

- Add `reaper.window.setDawKeyboard(enabled)` for ordinary Apps on Windows, macOS and Linux. Disabled by default, it routes unmodified Space outside editable fields through REAPER's current shortcut binding while preserving text input.
- Fix the macOS plugin ABI test to account for the platform-specific keyboard accelerator registration in GitHub Actions.

## 简体中文

- Windows、macOS 和 Linux 普通 App 新增 `reaper.window.setDawKeyboard(enabled)`，默认关闭。启用后将非编辑区域的无修饰 Space 交给 REAPER 当前快捷键绑定处理，保留编辑区域的空格输入。
- 修复 GitHub Actions 中 macOS 插件 ABI 测试未计入平台专用键盘 accelerator 注册的问题。
