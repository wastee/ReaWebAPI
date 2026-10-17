# JSFX Web UI

Native JSFX editors with a valid Web UI association are hosted automatically. No global preference or INI setting is required. The association's `enabled` field defaults to `true`.

The module uses the existing WebView2, WKWebView or WebKitGTK runtime, browser profile, `reaweb://<appId>` origin and Service bridge. REAPER continues to own the JSFX DSP and parameter state. No Lua backend or additional extension is required.

## Association

Place a JSON file beside the JSFX with the same basename. Identification uses REAPER's `fx_type=JS` and `fx_ident`, never the display name.

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

`entry` must resolve to an HTML file beneath the JSFX directory. The JSFX must resolve beneath `Effects`. Missing, disabled or invalid associations leave the native editor intact. `width` and `height` request the initial editor area (100..2048, defaults 640×420) for standalone FX windows. Docked windows retain host geometry. An optional `app.json` in the HTML directory follows the normal App identity contract. Without it, the module derives a stable App ID from `fx_ident`. Copy `SDK/jsfx-webui.mjs` beside the page to use the helper below.

## Parameter service

After `reaper.lifecycle.ready`, use `reaper.host.service('jsfx')`. Requests bind to the calling Hosted WebView. Ordinary Apps and External Clients cannot select or impersonate a binding.

| Method / event | Payload / result |
| --- | --- |
| `getState` | Returns `{fxIdent,count,parameters,revision,activation,active}`. Required before editing. |
| `beginEdit` | `{index,activation}`. Opens one managed Undo gesture. |
| `setParam` | `{index,value,activation,normalized?:boolean}`. Raw values by default. Returns the current parameter value with `revision` and `activation`. |
| `endEdit` | `{activation}`. Ends the native parameter edit and managed Undo gesture. |
| `present` | `{activation}`. Shows the page after rendering current host state. |
| `paramChanged` | `{parameters,revision,activation}` with changed values for this instance only. |
| `visibilityChanged` | `{active,activation,state?}`. Reactivation includes refreshed host state. |

Parameter metadata includes `index`, `name`, `ident`, `min`, `max`, `mid`, `defaultNormalized`, `step`, `smallStep`, `largeStep`, `toggle`, `section`, `enum`, `value`, `normalized` and `formatted`. Unsupported metadata is `null`. The native API supplies normalized defaults. `mid` is the host midpoint, not a default. Section and enum labels are not exposed by this service. The normalized setter preserves REAPER's parameter shaping.

```js
import { connectJSFX } from './jsfx-webui.mjs';

await reaper.lifecycle.ready;
const fx = await connectJSFX(reaper, error => showError(error.message));
const activeEdits=new Map();let gesture,closing=Promise.resolve(),epoch=0;
const current=edit=>edit.epoch===epoch&&fx.active;
const begin=index=>{
 if(gesture?.index===index)return gesture;
 if(gesture)end();
 const edit={index,epoch,value:undefined};gesture=edit;activeEdits.set(index,edit);
 edit.open=closing.then(async()=>{if(!current(edit))return false;await fx.beginEdit(index);return current(edit);});
 return edit;
};
const set=(index,value)=>{
 const edit=begin(index);edit.value=value;
 edit.open.then(opened=>{if(opened&&current(edit)&&edit.value!==undefined){const value=edit.value;edit.value=undefined;fx.setParam(index,value);}}).catch(e=>{if(current(edit))showError(e);});
};
const end=()=>{
 const edit=gesture;if(!edit)return closing;gesture=undefined;
 closing=edit.open.then(opened=>{if(opened&&current(edit))return fx.endEdit();}).catch(e=>{if(current(edit))showError(e);}).finally(()=>{
  if(activeEdits.get(edit.index)===edit){activeEdits.delete(edit.index);render(fx.state);}
 });
 return closing;
};
const render = state => {
  if (!activeEdits.has(0)) knob.value = state.parameters[0].normalized;
};
render(fx.state);
fx.onChange(render);
fx.onVisibilityChange(active => {
  epoch++; gesture = undefined; activeEdits.clear(); render(fx.state);
  active ? startVisualUpdates() : stopVisualUpdates();
});
knob.onpointerdown = event => { knob.setPointerCapture(event.pointerId); begin(0); };
knob.oninput = () => set(0, Number(knob.value));
knob.onchange = knob.onpointerup = knob.onpointercancel = knob.onlostpointercapture = end;
reset.onclick = () => end().then(() => fx.reset(0)).catch(showError);
```

Keep the actively dragged control at its local pointer position. Continue receiving host state, then render the calibrated host value after `endEdit()`. Serialize successive gestures and scope completion callbacks to their gesture, so a delayed release cannot repaint a new drag. Start a new relative drag from the displayed value. Let native range inputs manage their own pointer tracking. Reserve explicit pointer capture for custom controls such as knobs. Parameter metadata retains the host's raw floating-point values. Display `formatted` when available.

The helper keeps at most one request in flight and one pending value per parameter. Call `endEdit()` on pointer release, cancellation and keyboard completion. A single `setParam` outside a gesture creates a discrete Undo operation. Navigation, invalidation and a 30-second inactive gesture timeout end edits. Initialization and reload never write parameter defaults.

Visible bound editors receive bounded main-thread difference detection, with a 33 ms interval after each completed scan and native FX-change notifications requesting an earlier scan. Scans share a 1 ms budget and rotate between editors. The limit is 256 JSFX sliders plus REAPER's Bypass, Wet and Delta parameters. Editor discovery runs every 200 ms, with FX chain selection changes and host open/focus notifications requesting an earlier scan. Native paint observers verify unresolved editors before their first paint. Host calls never run on the audio thread. Bindings validate project lifetime, track GUID, item/take GUIDs where applicable, FX GUID and source identity before every operation. Reordered or nested FX are resolved through their GUID and the host container hierarchy within the original chain. Moving a take to another track invalidates the old binding. A new binding requires verified editor ownership. Take bindings never follow the active take or item selection.

## Page lifetime

Pages are created on demand and retained when their editor closes or switches FX. The module keeps four hidden pages, with one temporary handoff slot for up to 500 ms. It evicts the least recently used page above that limit and releases pages after 60 seconds idle. FX deletion and project invalidation release their pages. Reopening a retained instance reuses its WebView and document, refreshes host state and waits for `present` before showing it. Active editors are not evicted.

The helper handles activation tokens and presentation acknowledgements. It drops pending edits on deactivation and rejects older replies and snapshots, including within the same activation. Current host replies still update the authoritative state. Apply that calibration to an actively dragged control when its gesture ends. Reload resets initialization, activation, visibility and the 15-second presentation deadline. When switching to an unassociated FX, the previous Web UI remains visible until the target native editor is available. Parameter edits and polling stop as soon as the selected FX changes. Hidden pages stop native parameter polling. Use `fx.active` and `onVisibilityChange` to pause application timers, spectrum subscriptions and animations. Browser visibility throttling does not replace application cleanup. A direct Service client must render `visibilityChanged.state`, then acknowledge its `activation` with `present`.

## Editor compatibility

Windows uses a native child HWND. macOS uses a SWELL child view containing WKWebView. Linux uses the existing SWELL/X11 WebKit helper embedding path. The parent FX editor controls placement and docking. On macOS, automatic size adjustments preserve the window's upper edge. App window placement and icons are not applied to that parent.

Only the associated JSFX content controls and content painting are suppressed during initialization and while the page is active. REAPER's preset, parameter, routing, wet/dry and bypass controls remain native above the page, including any UI switch supplied by the host. No additional toolbar is created in chain or floating editors. Toolbar layout, resizing, menus and control actions remain owned by REAPER. Native input/output meters remain visible beside the page. A JSFX declaring `options:no_meter` uses the full content width without reserving meter space. Both modes retain the native meter bottom inset. The content inset follows the platform's native FX toolbar layout. Cached pages stay in hidden native containers. FX chains host pages in a persistent content viewport, so cached pages can follow list selection without waiting for native editor replacement. Clearing the list selection retains the page for REAPER's current FX editor. Floating an FX suspends its chain page and reuses the same page in the floating editor. Returning to the chain reapplies the associated content size. Floating window resizing preserves REAPER's saved native origin. Windows keeps a verified floating frame cloaked until its content size and WebView presentation are ready. Unresolved frames fall back to native presentation after two seconds. Cold Take FX discovery uses a bounded scan when no FX-open notification is available. Standalone FX chains conceal native controls and apply the associated size before creating the viewport, then retain their window size while switching between associated editors. The chain is revealed after the page acknowledges `present` and its WebView is ready to display. The viewport uses the page background color during handoff. Failed initialization restores the original controls.

This adapter targets REAPER 7.82 through the REAPER 7 layout family. It supports track and master FX, take FX, track input FX, master monitoring FX and nested containers in those chains, including floating editors and host FX Chain windows. Docking remains controlled by REAPER.

Discovery combines official FX APIs with the native window hierarchy. Container chains validate their parent container GUID and resolve embedded selection through the native chain hierarchy. Floating containers use their verified ancestor window for sizing. Clearing list selection retains the current editor by GUID while its native editor remains visible. New take or container chain windows may require focus before REAPER exposes enough information to confirm ownership. Ambiguous windows are deferred. Window titles, list counts and last-touched FX are not binding identities. Content bounds are refreshed when the native editor changes layout.

Control IDs are private REAPER implementation details, not an SDK replacement API. Unrecognized structures and failed initialization retain or restore the original controls. Reopen the editor to retry after a reported initialization failure. Legacy `@gfx` conversion and audio/`gmem` streams are outside this module.

## 简体中文

带有效同名 JSON 页面关联配置的原生 JSFX 自动使用 Web UI，无需全局首选项或 INI 配置。关联配置的 `enabled` 字段默认为 `true`。使用现有 Web Runtime、资源协议和 Service 桥，DSP 与参数状态由 REAPER 持有。

页面先调用 `getState`，再更新控件。拖动使用 `beginEdit`、合并后的 `setParam` 和 `endEdit`。拖动中的控件保持本地指针位置，持续接收宿主状态，手势结束后再校准显示，避免延迟更新导致回跳。`defaultNormalized` 是宿主提供的归一化默认值，不支持的元数据为 `null`。工程切换、FX 删除、编辑器关闭和页面重载会使旧请求失效。切换到未关联的 FX 时，保留当前 Web UI，直到目标原生编辑器可显示，选中实例变化后立即停止旧页面的参数编辑与轮询。普通 App 无法通过此 Service 访问其他 JSFX 实例。

页面按需创建，关闭或切换时隐藏并保留。缓存 4 个隐藏页面，切换期间允许额外保留 1 个页面，最长 500 毫秒。超过上限淘汰最久未使用的页面，闲置 60 秒后释放。再次打开时复用页面，刷新宿主参数并确认后显示。隐藏期间停止参数轮询，应用通过 `onVisibilityChange` 暂停频谱订阅、定时器和动画。初始化与复用期间仅隐藏关联 JSFX 内容区的原始控件并抑制该区域绘制。保留 REAPER 原生预设、Param、I/O、干湿、bypass 及宿主提供的 UI 切换控件。效果链与浮动编辑器均不额外创建标准栏。标准栏布局、伸缩、菜单和控件操作仍由 REAPER 管理。保留页面两侧的原生输入、输出电平表。JSFX 声明 `options:no_meter` 时，页面使用完整内容宽度，不预留电平表空间。两种模式均保留与原生电平表一致的底部间隔。内容区间距遵循各平台的原生 FX 标准栏布局。FX Chain 使用持久内容容器，缓存页面随列表选择切换，无需等待原生编辑器重建。取消列表选中状态时，保留 REAPER 当前 FX 编辑器对应的页面。浮动 FX 时撤下效果链中的页面，并在浮动编辑器中复用。返回效果链时重新应用关联内容尺寸。调整浮动窗口尺寸时保留 REAPER 保存的原生位置。Windows 将已确认归属的浮动窗口保持隐藏，内容尺寸和 WebView 就绪后再呈现。两秒内未能接管编辑器时恢复原生显示。Take FX 首次打开且缺少 FX 打开通知时采用有界扫描。独立 FX Chain 在创建内容容器前隐藏原生控件并应用关联尺寸，切换关联编辑器时保持尺寸。页面确认 `present` 且 WebView 可显示后才呈现窗口。切换期间，内容区域保留与页面颜色一致的背景。

适配器验证 REAPER 7.82 起的 7.x 编辑器结构，支持轨道、主轨、Take、输入、监听 FX 及各链中的嵌套容器，覆盖浮动编辑器和宿主 FX Chain 窗口。停靠由 REAPER 管理。绑定同时核对官方 FX 信息和原生窗口层级，容器链还校验父容器 GUID，并通过原生效果链层级解析内嵌编辑器。浮动容器使用已验证的祖先窗口调整尺寸。取消列表选中时，若当前原生编辑器仍可见，按 GUID 保留其 Web UI。新打开的 Take 或容器链窗口可能需要获得焦点后才能确认归属，信息不足时延后挂载，不按窗口标题、列表数量或最后触碰 FX 猜测绑定。

每次参数操作均校验工程、轨道、Item/Take 和 FX 身份。重排与容器层级变化后按 GUID 重新寻址，Take 页面不跟随激活 Take 或选中 Item 改绑。Item 跨轨移动会使旧绑定失效，新绑定需重新确认窗口归属。参数旧回复和旧快照不能覆盖较新宿主值，重载会重置初始化、激活令牌、可见状态及 15 秒显示期限。

原生编辑器布局变化后重新计算内容区域，macOS 自动调整尺寸时保持窗口上边缘位置。窗口结构不匹配或初始化失败时保留或恢复原生控件，报错后可重新打开编辑器重试。`@gfx` 转换及音频数据流不在本模块范围内。
