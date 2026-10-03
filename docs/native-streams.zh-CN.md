# Native Stream

[External Client](external-clients.zh-CN.md) 通过绑定 Session 的 Ticket 接入同一 Stream Hub。二进制 packet、确认、背压和原有 WebView Origin 校验保持不变。

`reaper.host` 对应 Lua Backend，`reaper.host.service(name)` 对应原生服务，`reaper.events` 对应状态通知，`reaper.stream` 对应连续二进制数据。现有接口保持原有契约。

```js
const stream = await reaper.audio.openStream('spectrum', {
  source: 'master', fftSize: 2048, updateRate: 30
});
stream.on('data', packet => render(packet.data, stream.info));
stream.on('close', error => console.log(error.code));
await stream.close();
```

`stream.open(name)` 建立持续二进制连接。`latest()` 读取已到达页面的缓存，不发起 RPC。`read()` 按 FIFO 消费 Audio/MIDI，空队列返回 `null`。Frame、Spectrum、Meter、Waveform 和 Binary 保留最新数据。`stream.close()` 只分离当前 consumer，原生 `ReaWeb_CloseStream()` 关闭整个 producer。

传输由独立原生线程负责，不经过 Lua、逐帧 JSON/Base64、普通 RPC 或 `Runtime::tick()` 帧调度。每个 Stream 使用有界缓存。Publish 不分配内存、不加锁、不等待页面。Audio/MIDI 缓存满时丢弃新包，其他类型丢弃过时包。原生丢包计数、序号间断和 consumer `dropped` 可用于诊断。

公共 C ABI 见 [reaweb_stream.h](../src/public/reaweb_stream.h)，示例见 [synthetic producer](../tests/native_stream_extension.cpp)。创建与关闭在 REAPER 主线程执行，每个 Stream 由单个 producer 发布。第三方扩展应注册 `ReaWeb_SetServiceShutdown` 回调，先停止并等待 producer 退出，再关闭 Stream、注销 Service、释放资源。低延迟输入方法可通过 `ReaWeb_SetServiceInput` 注册，回调只更新最新输入状态，不执行耗时命令。

内建 `audio.openStream` 提供 PCM、FFT、Peak/RMS/LUFS 和实时 min/max 波形。`master` 采集硬件输出 0/1，`input` 采集硬件输入 0/1。`selected-track` 或 `track:<GUID>` 使用绑定轨道的 **pre-FX Audio Accessor**，不代表轨道 FX 后或实时输入电平。Accessor 在主线程采样，分析和传输在原生线程执行。已有 `audio.getTrackMeter` 保留 REAPER 瞬时 Peak 语义。文件概览、缓存峰值与缩放查询继续使用 `audio.getWaveform`。

轨道 Source 支持 `aggregate: true`，适用于 `audio`、`spectrum`、`meter` 和 `waveform`：

```js
const stream = await reaper.audio.openStream('spectrum', {
  source: 'selected-track', // 也支持 'track:<GUID>'。
  aggregate: true, fftSize: 2048, updateRate: 30
});
```

聚合递归包含根轨、启用 Parent Send 的 Folder 子轨和音频 Receive 上游。静音 Receive 和仅 MIDI 路由不参与。同一源轨只计算一次，循环路由不会重复累加。每个采样块重新检查路由与 Mute/Solo 状态，排除静音轨道、经静音 Folder 路由的分支及 Solo 排除的源内容，支持 Solo Defeat 和 Solo In Place。根轨静音时返回静音。

所有 Accessor 使用相同工程时间、采样率、双声道布局和块大小读取。Native 层先逐 sample 求和 PCM，再执行现有分析，仅创建一个对外 Stream。保留超过 `1.0` 的浮点结果，不归一化、限幅、按轨平均或补偿增益。Item/Take/Lane 播放状态由 REAPER Accessor 返回的 PCM 决定。

相同轨道、聚合模式、采样率和更新频率的流复用源采样。聚合读取在宿主周期预算耗尽后从下一源轨继续，仅发布同一工程时间位置的完整求和结果。桥接请求采用独立处理预算，避免慢速采样阻塞连接与释放请求。单次 REAPER Accessor 调用无法中断。轨道 Accessor 丢弃偶发的非有限数或 Float32 溢出 PCM 块，恢复时原始 PCM 序号保留间断，分析流重置历史。连续八块无效数据以 `NATIVE_ERROR` 关闭源。其他分析异常仅关闭受影响的流，不会停止其他生产者或后续新建流。

该模式表示 **pre-FX 源 PCM 的同步聚合**，不代表 post-FX、pre-fader、post-fader 或实际轨道输出。不应用轨道或 Send 的增益、声像、相位和通道映射，不创建 FX、Send、Track 或 Undo。`aggregate` 默认为 `false`，省略或显式关闭时保持现有行为。对 `master` 或 `input` 启用聚合会返回 `INVALID_ARGUMENT`。Stream 的 `source` 标识为 `track:<GUID>:pre-fx:aggregate-source`。删除绑定根轨或切换工程会关闭聚合 Stream。

MIDI 通过 `system.openMIDIInput(device)` 消费原始事件，`-1` 表示所有输入。`system.getDevices` 与 `devicesChanged` 提供设备信息。显示器几何、工作区和有效 DPI 通过 `system.getDisplays` 获取，CPU 和 Stream 统计通过 `debug.getDiagnostics` 获取。

`fs.watch` 在原生 worker 每 250 ms 比较文件状态，报告创建、修改、重命名、删除和溢出。短时变化可能合并，页面无需扫描目录。`clipboard.readBinary/writeBinary` 使用自定义 MIME 格式，不自动转换系统图片格式。`system.schedule` 提供一次性和重复原生定时器，适合后台刷新与延时任务，不充当帧时钟。页面关闭或导航后自动释放 consumer、监听和定时器。

完整字段、布局、线程规则、容量、溢出策略和平台坐标差异见 [English contract](native-streams.md)。[Native Stream Demo](../web/native-stream/README.md) 可消费内建分析和第三方 Stream。ReaGBA 的 Service、输入与 `reagba.video` 接入代码保留在独立 ReaGBA 仓库，模拟器按自身约 59.73 Hz 节奏发布画面。

页面如设置 Content Security Policy，需要允许 `connect-src ws://127.0.0.1:*`。连接仍受页面来源及一次性原生连接凭据约束。

`stream.getDiagnostics().transport` 提供当前 Runtime 的成功连接数、握手拒绝数、过期凭据数及最近的握手拒绝代码，不包含连接凭据。传输尚未初始化时不返回此字段。
