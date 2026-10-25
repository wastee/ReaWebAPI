# ReaWebAPI

## English

- Add `app.json.dawKeyboard` for ordinary App initial Space routing, defaulting to `false`.
- Allow JSFX Web UI to use `window.setDawKeyboard`. Both page types restore their JSON settings on reload without writing configuration. The JSFX default remains `true`.

## 简体中文

- 普通 App 新增 `app.json.dawKeyboard` 初始 Space 路由配置，默认 `false`。
- JSFX Web UI 支持 `window.setDawKeyboard` 动态设置。两类页面重载后均恢复各自 JSON 配置，不回写文件，JSFX 默认仍为 `true`。
