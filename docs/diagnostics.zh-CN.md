# Runtime Diagnostics

[English](diagnostics.md) | **简体中文**

Windows、macOS、Linux 上的普通 App 与 JSFX WebUI 共用持久化诊断日志。在 **Preferences > Plug-ins > ReaWebAPI > Diagnostics** 配置。

| 设置 | 作用 |
| --- | --- |
| Enable App debug logging + Target App ID | 为精确匹配的 App ID 及其关联 Runtime 事件启用 INFO/DEBUG，覆盖该 App 的全部实例 |
| Enable Runtime debug logging | 为 Runtime 底层事件启用 INFO/DEBUG，包括尚无法归属 App 的事件，不开启其他 App 的 JavaScript debug/info 记录 |
| Open diagnostics log folder | 在系统文件管理器中打开日志目录 |

两个开关默认关闭，互不依赖。Apply/OK 将设置保存到 `ReaWebAPI.ini`，对已运行实例即时生效。可在 App 启动前填写目标，在现有 App ID 可识别后记录初始化过程，无需修改 App 配置。目标采用现有 `reaper.app.getId()` 或 `webRuntime.appId`，包含自动生成的 JSFX ID。空目标不能启用 App DEBUG。

日志位于 `<REAPER 资源目录>/ReaWebAPI/Diagnostics/`，格式为 UTF-8 JSON Lines，可直接用文本编辑器阅读。`runtime.log` 保存 Runtime 事件，`app-<appId>.log` 保存该 App 及其关联 Runtime 事件。单文件上限 2 MiB，保留三份编号备份。目录最多保留 64 个受管日志文件，达到上限时删除最旧文件。文件名保留现有小写 ASCII App ID，不截断，日志目标最长 240 个字符。诊断目标校验不改变 App 身份。

每条记录包含 UTC `time`、`level`（`ERROR`、`WARN`、`INFO`、`DEBUG`）、`scope`、`event`、诊断 `sessionId`、`appId`、`windowId`、`result` 和 `threadId`。诊断会话标识一次 Runtime 生命周期，与认证及文档令牌独立。同一 App 的多个实例通过窗口 ID 区分。可用的 JSFX 上下文包含 `fxGuid`、`fxIndex`、`fxLocation`、`fxSessionId` 和 `activation`。API 执行记录可包含 `method` 和 `durationMs`。早期事件的 App ID 为空，窗口 ID 为 0。

事件覆盖创建、Bridge 就绪与导航、关闭与释放、实例绑定、API 失败、JavaScript 日志等级、宿主消息通道活动及失败、生命周期超时，以及 JSFX 实例识别、暂存、关联、失效和原生通道会话变化。Track、输入 FX、Item/Take 和嵌套 Container FX 复用现有定位机制，FX 索引保留宿主的 Container 编码。

默认仅持久化 WARN/ERROR。自由文本、URL、调用参数、通信正文、认证令牌和音频样本不写入文件，包括 `reaper.debug.log/warn/error` 的文本。现有内存日志及控制台输出行为不变。相同 App、窗口和事件每秒最多记录 20 条，后续记录的 `suppressed` 表示被抑制数量。日志在 UI 线程写入并刷新，其他线程在 I/O 前直接返回。诊断 I/O 失败不影响 Runtime 运行。

REAPER 没有公开 JSFX 编译回调。原生通道会话变化、宿主 FX 修订通知、参数数量变化及绑定失效均为观测结果，不能据此断言发生了编译。Runtime 也无法直接观察任意 Lua 脚本启停。`lua.channel.begin/end` 表示现有宿主消息通道的可观测使用及释放，不代表 Lua 进程生命周期。Lua 轮询停止但窗口仍打开时，没有可靠的退出信号。已刷新的记录可在进程强制结束后读取，不保证断电持久性。

Demo 的 Diagnostics 面板及 `reaper.debug.getDiagnostics()` 继续提供状态快照。自动记录持久化日志无需保持任何查看窗口打开。
