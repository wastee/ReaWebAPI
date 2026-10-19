# JSFX Web UI

[English](jsfx-webui.md)

具有有效 Web UI 关联的原生 JSFX 编辑器自动使用网页界面，无需全局首选项或 INI 设置。关联配置的 `enabled` 默认为 `true`。

模块复用现有 WebView2、WKWebView 或 WebKitGTK Runtime、浏览器配置、`reaweb://<appId>` 来源及 Service 桥。JSFX 的 DSP 与参数状态仍由 REAPER 管理，无需 Lua 后端或额外扩展。

## 页面关联

在 JSFX 旁放置同名 JSON 文件。实例识别使用 REAPER 的 `fx_type=JS` 和 `fx_ident`，不使用显示名称。

```text
Effects/Author/Utility/
  Utility.jsfx
  Utility.json
  index.html
  app.js
  jsfx-webui.mjs
```

```json
{"schema":1,"JSFX_WebUI":{"entry":"index.html","enabled":true,"width":640,"height":420}}
```

`entry` 必须指向 JSFX 所在目录内的 HTML 文件，JSFX 必须位于 `Effects` 下。关联缺失、禁用或无效时保留原生编辑器。`width`、`height` 指定独立 FX 窗口的初始内容尺寸，范围为 100..2048，默认 640×420。停靠窗口保留宿主尺寸。HTML 目录内可提供遵循普通 App 身份契约的 `app.json`，未提供时按 `fx_ident` 生成稳定的 App ID。使用下方 helper 时，将 `SDK/jsfx-webui.mjs` 复制到页面旁。

## 参数服务

等待 `reaper.lifecycle.ready` 后使用 `reaper.host.service('jsfx')`。请求绑定到调用方的 Hosted WebView，普通 App 与 External Client 无法选择或冒充绑定实例。

helper 要求 `runtime.jsfx.protocolVersion === 1`。入口为 `connectJSFX()`，参数接口位于 `jsfx.params`。所有扩展能力位于同一 `jsfx` 服务。

JSFX 回复统一为 `{ok:true,sessionId,data}` 或 `{ok:false,sessionId?,error:{code,message,details?}}`。helper 解包结果，将业务错误转为带 `code`、`details` 的异常。`getCapabilities` 获取实例会话，后续请求携带 `sessionId`，活动编辑器操作还携带 `activation`。修订号使用十进制字符串，按 `BigInt` 比较。

| SDK / 服务 | 输入 → data |
| --- | --- |
| `getCapabilities` | 协议版本、实例会话、能力修订号、能力和限制 |
| `params.getState` / `getParamState` | `{sessionId,fxIdent,count,parameters,revision,activation,active}` |
| `params.beginEdit` / `beginParamEdit` | `{index}` → `{gestureId}`，开始一次受管理的撤销手势 |
| `params.set` / `setParam` | `{index,normalized,gestureId?}` → `{parameter,revision}`，仅接受 [0,1] 内的有限归一化值 |
| `params.endEdit` / `endParamEdit` | `{gestureId}` → `{ended}`，随后由 helper 刷新快照 |
| `params.beginEditGroup` / `beginParamEditGroup` | `{indices}` → `{gestureId}`，与单参数手势互斥 |
| `params.setMany` / `setParams` | `{gestureId,values:[{index,normalized}]}` → `{applied,revision}`，写入前校验全部值 |
| `params.endEditGroup` / `endParamEditGroup` | `{gestureId}` → `{ended}`，整组共用一次撤销 |
| `present` | `{activation}` → `{presented}`，在当前宿主状态完成渲染后确认 |
| `paramsChanged` | `{sessionId,parameters,revision,activation}`，仅针对当前实例 |
| `visibilityChanged` | `{sessionId,active,activation,paramState?}`，重新激活时附带新快照 |
| `getDiagnostics` | 实例状态、原生通道可用性、当前手势及呈现时间 |

`params.snapshot` 为不可变缓存，在连接完成前初始化。订阅返回 `Promise<Dispose>`，Dispose 是幂等的异步函数。helper 管理会话、激活代数和手势 ID。直接调用 Service 的页面需自行管理。JSFX 方法使用 `invoke`，不使用 `send`。

未声明 EEL 原生通道时，能力返回 `status:'parameterOnly'`、参数组可用及 `nativeChannel:false`，扩展方法返回 `UNSUPPORTED`。接入 SDK 的插件可通过同一服务暴露已声明的流、经过 schema 校验的复杂状态、带执行确认的命令和可靠二进制资源。传输及同步契约见[原生通道](jsfx-native-channel.zh-CN.md)。

参数元数据包括 `index`、`name`、`ident`、`min`、`max`、`mid`、`defaultNormalized`、`step`、`smallStep`、`largeStep`、`toggle`、`section`、`enum`、`value`、`normalized` 和 `formatted`。不支持的字段为 `null`。`defaultNormalized` 来自宿主归一化默认值，`mid` 是宿主中点，不是默认值。服务不提供分节及枚举标签。归一化写入保留 REAPER 的参数映射。

```js
import { connectJSFX } from './jsfx-webui.mjs';

await reaper.lifecycle.ready;
const jsfx = await connectJSFX(reaper, error => showError(error.message));
render(jsfx.params.snapshot);
const dispose = await jsfx.params.onChange(render);
```

拖动中的控件保持本地指针位置，继续接收宿主状态，在 `endEdit()` 后显示校准值。连续手势应串行开始，完成回调限定到所属手势，避免延迟释放覆盖新拖动。相对拖动从当前显示值开始。原生 range 输入使用自身指针处理，自定义旋钮等控件才需要显式指针捕获。参数元数据保留宿主原始浮点值，显示时优先使用 `formatted`。

单参数手势最多保留一个在途写入与一个待发送值。指针释放、取消及键盘操作结束时调用 `endEdit()`。手势外的每次 `params.set` 形成独立撤销，其 Promise 明确返回已应用或已被替代。离散写入与参数组批次使用有界串行队列。`setMany` 不保证样本级原子性。部分写入后宿主 setter 失败时返回 `PARTIAL_WRITE` 及已应用索引，结束手势并刷新快照。导航、失效及手势闲置 30 秒会结束编辑。初始化与重载不写入默认参数。

宿主参数访问在主线程执行，上限为 256 个 JSFX 滑块及 REAPER 的 Bypass、Wet、Delta。FX 重排后在原效果链内按 GUID 解析。Take 移到其他轨道时，原绑定失效。

## 页面与编辑器

页面按需创建，关闭编辑器或切换 FX 后最多缓存四个隐藏页面，60 秒空闲后释放。FX 删除或工程失效时立即释放。重新激活会刷新宿主快照，页面完成渲染并确认 `present` 后显示。helper 管理激活标记及呈现确认。

使用 `jsfx.active` 和 `onVisibilityChange` 暂停页面定时器、流订阅和动画。失活会结束参数手势并丢弃待提交拖动值。直接使用 Service 的页面需渲染 `visibilityChanged.paramState`，再确认对应的 `activation`。初始化失败或超过 15 秒未确认时恢复原生内容。

支持轨道、主轨、Take、输入、监听 FX 及其嵌套容器，包含效果链和浮动编辑器。浮动与非浮动之间复用同一页面，重新应用关联尺寸，停靠行为由 REAPER 管理。绑定校验工程、归属者及 FX GUID，不依赖窗口标题或最后触摸的 FX。

网页仅替代关联 JSFX 的内容区。预设、Param、路由、UI 切换、干湿及 bypass 控件保留原生行为，不另建标准栏。两侧原生电平表保留，`options:no_meter` 可移除其占位。两种模式均保留原生底部间隔。

Windows 使用 WebView2，macOS 使用 WKWebView，Linux 使用 SWELL/X11 WebKit 嵌入路径。适配目标为 REAPER 7.82 及 REAPER 7 窗口布局。窗口结构识别涉及宿主内部控件，未识别结构保留原生编辑器。失败后可重新打开重试。模块不自动转换 `@gfx` 代码。

Windows 请使用 v0.3.8.28 或更高版本，使 FX 链中的 WebView 正确参与原生对话框焦点导航。关联的 JSFX 和页面代码无需修改。

## 可选 DSP 能力

将配套 `SDK/jsfx/reaweb_jsfx.inc` 放在插件旁。SDK 的 `SDK/jsfx/examples` 提供三个独立示例：`analyzer`、`curve`、`resource`。将示例目录复制到 REAPER 的 `Effects` 目录即可加载。分析器展示原始 PCM、min/max 波形和 Hann 窗频谱，测试信号仅用于遥测，不改变音频输出。曲线示例包含 CAS 与原生撤销，资源示例包含内嵌字节资源导入导出和 DSP 命令。

在 `JSFX_WebUI` 内添加 `native`。`transportParam` 是专用滑块对应的宿主参数索引，从零开始，不能占用现有自动化参数。EEL 的 `options:gmem` 名称和 `reaweb_jsfx_init(base,memory)` 必须匹配声明。接入已有插件时保留其 gmem 名称，选择未使用的保留区间。

```json
{
  "protocolVersion": 1,
  "gmem": "Author_Effect_v1",
  "base": 0,
  "transportParam": 1,
  "streams": [{"id":"output","kind":"spectrum","source":"postDSP","channels":2,"fftSize":2048}],
  "dataStates": [{"id":"curve","schemaVersion":1,"schema":{"type":"array","maxItems":4,"items":{"type":"number","minimum":0,"maximum":1}},"persistent":true,"undo":true}],
  "commands": [{"id":"sum","args":{"type":"array","maxItems":8,"items":{"type":"number","minimum":0,"maximum":1}},"result":{"type":"number","minimum":0,"maximum":8}}],
  "resources": [{"id":"asset","format":"bytes","maxBytes":32768,"persistent":true,"undo":true}]
}
```

ID 满足 `[A-Za-z][A-Za-z0-9_.-]{0,127}`，在所属类别内唯一。EEL 数字 ID 对应声明数组的零起始索引。对象按字段名的字典顺序编码，所有字段必填。数组和 UTF-8 字符串以元素数或字节数开头，布尔值使用 0/1。数值边界必须有限。schema 最多嵌套八层，编码最多 4096 个 double，JSON 值最多 64 KiB。修订号为无符号 64 位十进制字符串，EEL 内使用两个 32 位分量。

| SDK | 服务方法 | 契约 |
| --- | --- | --- |
| `streams.open(id, {updateRate}?)` | `openStream` 与现有 Stream 传输 | 返回标准 `ReaWebStream`，验证实例及文档所有权。 |
| `dataState.get(id)` | `getDataState` | 返回 `{id,revision,schemaVersion,value,available}`，DSP 暂停时可返回缓存并标记 `available:false`。 |
| `dataState.set(id,value,{expectedRevision,requestId?})` | `setDataState` | 返回操作记录，旧修订号产生 `CONFLICT` 并附当前快照。 |
| `commands.release()` | `releaseCommands` | 释放当前文档持有的持续操作和音符。 |
| `commands.invoke(id,args,{requestId?})` | `invokeCommand` | 经校验的有界请求，由接入插件执行并回复。 |
| `operations.get/find/cancel/onChange` | `getOperation/findOperation/cancelOperation`、`operationChanged` | 查询执行状态，恢复丢失回复，避免重复执行。 |
| `resources.beginUpload/writeChunk/commitUpload/abortUpload` | `beginResourceUpload/commitResourceUpload/abortResourceUpload` 与二进制通道 | 暂存后替换。`writeChunk` 使用 `Uint8Array` 与连续字节偏移。 |
| `resources.openDownload/readChunk/closeDownload` | `openResourceDownload/closeResourceDownload` 与二进制通道 | 固定修订快照，按字节区间读取并显式关闭。 |

同一实例会话内，`requestId` 对相同操作和负载去重。换用其他负载会报错。最多 16 个未完成操作，有界缓存最多保存 256 条记录，完成后保留 60 秒。DSP 未调度时操作保持 `queued`，附 `blockedReason:'DSP_UNAVAILABLE'`。只有 DSP 回复后才进入 `succeeded` 或 `cancelled`。超过 `maxExecutionMs` 的命令标记 `deadlineExceeded:true`，可取消处理器收到取消请求。操作及撤销记录保留到 DSP 回复或实例失效，不能仅凭期限推断没有产生副作用。运行中的不可取消命令拒绝取消。

流支持 `audio`、`spectrum`、`waveform`、`meter`、`binary` 和 `midi`。前四类接收插件显式发布的交错 PCM。频谱为 Hann 窗、按通道交错的线性幅值。波形按采样点及通道排列 min/max 对。Audio 保留原始样本，适合示波器和相位图。Meter 复用 `reaper.audio.decodeMeter` 布局。Binary/MIDI 承载插件定义的字节记录，应用需约定布局，不代表硬件 MIDI 输入。音符输入通过下述命令辅助函数管理所有权。

JSFX 流协商布局版本 2，不改变既有 Stream 数据包。`packet.timing` 包含 `clockEpoch`、`firstSample`、`sourceDropped`、`position`、`playState`、`discontinuity`。遇到不连续应清空分析及显示历史。描述符提供采样点、通道标签、采样率和 FFT 大小。分析按包边界及协商的 1–120 Hz 频率发布，默认 30 Hz，不保证固定样本 hop。背压下允许丢弃遥测，不能等待网页。触发检测、预计算频谱和专用 MIDI 记录可由插件结合 audio/binary 流实现。

资源格式为原始字节和有限值的 IEEE float32 小端数据。声明上限还受 EEL 缓冲容量约束。提交必须提供小写 SHA-256 校验和及预期修订号。二进制块包含 CRC32、序号、偏移确认和有界重试。完整暂存区通过 CAS 后才在块边界切换，失败保留旧数据。下载提供固定快照、校验和及元数据。元数据为最多 768 个 UTF-8 字节的 JSON 对象。文件选择复用已有文件 API 或浏览器文件输入。格式解析与重采样放在 `@block` 之外，并显式提供采样率及通道元数据。本版通过 `@serialize` 内嵌注册资源，不新增文件引用重定位机制或音频文件解码器。

现有 Stream 服务承载私有 `RWR1` 资源模式，控制 JSON 不携带资源字节。每个传输最多一个 256 KiB 在途块，30 秒无进展后过期。每实例最多两个传输，暂存预算 32 MiB，Runtime 共用 128 MiB。预算包含缓冲及在途开销，因此最大负载可能小于 32 MiB。DSP 缓冲由插件另外预分配。页面失去所有权时关闭流和未提交传输，已接受的状态及资源操作仍可在同一 DSP 会话查询。新 DSP 代数使旧凭据、操作和快照失效。没有缓存页面、保留操作或传输的实例在 60 秒后释放，再次打开会建立新会话。

### EEL 接入

为 SDK 保留 32768 个本地内存单元，与音频缓冲及所有状态、资源的双缓冲分离。gmem 协议保留 `[base, base + 2170944)`，包含 32 个活动实例槽位。命名空间与范围冲突时拒绝接入，网页不能提交内存地址。

```eel
options:gmem=Author_Effect_v1
import reaweb_jsfx.inc
slider1:1<0,2,0.001>Gain
slider2:0<-70368744177664,70368744177664,1>-ReaWebAPI transport v1
@init
reaweb_jsfx_init(0,65536);
// Register state/resource banks here, initializing defaults only on return value 1.
@slider
slider2=reaweb_jsfx_handshake(slider2);
@block
slider2=reaweb_jsfx_handshake(slider2);
kind=reaweb_jsfx_service();
// Handle kind 1 using reaweb_jsfx_id, reaweb_jsfx_length and reaweb_jsfx_value(i).
// Reply with reaweb_jsfx_reply(code, data, length). Zero means success.
@serialize
reaweb_jsfx_serialize(0,state_mask,resource_mask);
slider2=reaweb_jsfx_handshake(slider2);
```

`reaweb_jsfx_state(id,bankA,bankB,length,capacity,schemaVersion,readOnly)` 与 `reaweb_jsfx_resource(id,bankA,bankB,length,capacity,format)` 返回 1 表示新建缓冲，2 表示保留缓冲，0 表示声明无效。资源格式 1 为字节，2 为 float32。每次 `reaweb_jsfx_service()` 后通过 `reaweb_jsfx_state_data`、`reaweb_jsfx_resource_data` 和 `reaweb_jsfx_resource_length` 获取当前数据，不能跨块缓存缓冲指针。DSP 主动更新状态使用 `reaweb_jsfx_state_update`，该函数暂存有界数据并递增修订号。

`reaweb_jsfx_stream_active(id)` 表示当前原生订阅，可用于跳过可选采集工作。未订阅的发布不产生数据。`reaweb_jsfx_publish(id,data,length,channels,firstSample)` 每次最多发布 4096 个值，缓冲及复制必须有界。异步命令每块只执行有限工作，检查 `reaweb_jsfx_cancelled()`，取消完成后以代码 6 回复。`reaweb_jsfx_session_changed` 为真时清除插件自己的在途任务，再清除此标记。音频线程不得跨 FX 路由 UI 命令、解析 JSON、访问文件或等待。

`reaweb_jsfx_serialize` 在插件原有序列化流中追加独立版本记录，掩码按数字 ID 选择持久状态和资源。项目、预设和 Undo 存储可往返精确 double、float32、字节及元数据。读取先校验记录，再切换活动缓冲。插件升级时显式管理 schema 版本。保留的握手值为临时凭据，不能单独授权恢复或复制后的实例。

连接及重新连接仅通过共享内存挑战和目标参数读回确认实例，不写宿主参数，不产生撤销记录或工程修改。DSP 未调度时等待回复。实际参数编辑和持久状态提交仍按正常规则参与撤销。REAPER 仍可能记录关闭编辑器或窗口布局变化，与原生编辑器一致。

### 持续命令与 MIDI

按住类操作声明 `continuous:true,cancellable:true`，不能同时声明 `undo` 或 `persistent`。`commands.release()` 请求清理当前文档持有的操作。SDK 在失焦、失活和销毁时释放，失焦后拒绝新的持续命令，直到重新取得焦点。原生文档销毁也会释放所有权。自定义 DSP 处理器在 `reaweb_jsfx_service()` 后读取 `reaweb_jsfx_released_owner`，-1 表示没有释放，0 表示旧会话的所有持有者，正值标识单个持有者。将其与操作开始时保存的 `reaweb_jsfx_owner` 比较。

持续命令处理器使用 `reaweb_jsfx_note_on(channel,note,velocity)` 与 `reaweb_jsfx_note_off(channel,note)`，成功返回 1，拒绝或发送失败返回 0。最多记录 128 个 UI 音符，保留当前 MIDI 总线，只释放自有音符。其他 UI 持有者占用相同总线、通道和音高时拒绝新请求。note-off 发送失败会在后续块重试，不发送全局清音或改变路由。MIDI 1.0 无法区分外部来源使用的相同通道和音高，需要独立所有权时应预留通道。FX 被旁通或设备停止时，清理等待 DSP 恢复调度。
