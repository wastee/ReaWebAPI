# ReaWebAPI

## English

- Add live title-bar switching with `window.setFrameless(boolean)`.
- Add `window.setCornerRadius(0..64)` with Dock/maximize suspension and floating restoration.
- Fix Windows frameless border remnants and resizing from all four edges and corners.
- Add worker-thread `fs.moveFile()` with explicit source/destination paths, overwrite control and cross-volume moves.
- Align page context menus with native window menus while retaining custom page menus and hiding browser defaults.

## 简体中文

- 新增 `window.setFrameless(boolean)`，支持在同一窗口动态切换原生标题栏。
- 新增 `window.setCornerRadius(0..64)`，停靠和最大化时暂停裁剪，恢复浮动后重新应用。
- 修复 Windows 无标题栏窗口的边框残留与四边四角缩放问题。
- 新增工作线程 `fs.moveFile()`，支持明确的源与目标路径、覆盖控制和跨卷移动。
- 统一网页与原生窗口右键菜单，保留页面自定义菜单并继续隐藏浏览器默认项。
