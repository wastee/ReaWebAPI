# ReaWebAPI

## English

- Replace per-App HTTP listeners with native `reaweb://<appId>` resources on Windows, macOS and Linux, preserving modules, fetch, Workers, browser storage and DevTools.
- Add authoritative `app.json.id`, Lua-launcher filename fallback and duplicate-root detection. Preserve App identity across directory moves and store port-free origin metadata.
- Update SDK examples and diagnostics for virtual App origins. Existing HTTP-origin browser data is retained without automatic migration.

## 简体中文

- Windows、macOS 和 Linux 使用原生 `reaweb://<appId>` 资源替代每 App HTTP listener，保留模块、fetch、Worker、浏览器存储和 DevTools 支持。
- 新增权威 `app.json.id`、Lua 启动脚本文件名回退及重复目录检测，目录移动后保留 App 身份，来源元数据不再保存端口。
- 同步 SDK 示例与虚拟 App 来源诊断，保留已有 HTTP origin 浏览器数据，不自动迁移。
