# ReaWebAPI

## English

- Limit WebView context menus to docking and DevTools controls on Windows, macOS and Linux, hiding default browser entries including Inspect.
- Synchronize DevTools menu state with visibility and presentation, disable mode switching while hidden, and recover Windows embedding when the inspector frontend loads late.
- Add App-specific Docker tab menus for docking, reload, DevTools, opening the App folder and closing the App. Use the current App title in menu labels and show docking as a checked `Dock <App> in Docker` action.

## 简体中文

- Windows、macOS、Linux 的 WebView 右键菜单仅保留停靠与 DevTools 控制，隐藏包括检查在内的浏览器默认菜单项。
- DevTools 菜单与实际显示和模式状态同步，隐藏时禁用模式切换，并在 Windows 检查器内容延迟加载后恢复嵌入。
- 新增对应 App 的 Docker 页签菜单，提供停靠、刷新、DevTools、打开 App 文件夹和关闭操作。菜单名称跟随当前 App 标题，停靠操作统一为带勾选状态的 `Dock <App> in Docker`。
