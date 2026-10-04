# App trust model / 应用信任模型

ReaWebAPI runs trusted local Apps. An App can call the standard REAPER APIs and native file services available to the extension.

Apps share one browser profile while retaining their own local resource origins. localStorage and IndexedDB are isolated by origin. Cookies follow native domain and path rules and are shared across ports on the same host. Native API access uses the REAPER process permissions.

The manifest validator checks App metadata and entry paths. Review an App and its source before opening it with ReaWebAPI.

ReaWebAPI 运行受信任的本地应用，应用可调用扩展提供的标准 REAPER API 和原生文件服务。

App 共用浏览器 profile，使用独立的 `reaweb://<appId>` 来源。localStorage 和 IndexedDB 按 origin 隔离，Cookie 支持取决于浏览器对自定义协议的处理。原生 API 使用 REAPER 进程的访问权限。

Manifest 校验工具检查应用元数据和入口路径。使用 ReaWebAPI 打开应用前，应确认其来源和代码可信。
