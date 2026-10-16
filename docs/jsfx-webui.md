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
| `setParam` | `{index,value,activation,normalized?:boolean}`. Raw values by default. Returns the current parameter value. |
| `endEdit` | `{activation}`. Ends the native parameter edit and managed Undo gesture. |
| `present` | `{activation}`. Shows the page after rendering current host state. |
| `paramChanged` | `{parameters,revision,activation}` with changed values for this instance only. |
| `visibilityChanged` | `{active,activation,state?}`. Reactivation includes refreshed host state. |

Parameter metadata includes `index`, `name`, `ident`, `min`, `max`, `mid`, `defaultNormalized`, `step`, `smallStep`, `largeStep`, `toggle`, `section`, `enum`, `value`, `normalized` and `formatted`. Unsupported metadata is `null`. The native API supplies normalized defaults. `mid` is the host midpoint, not a default. Section and enum labels are not exposed by this service. The normalized setter preserves REAPER's parameter shaping.

```js
import { connectJSFX } from './jsfx-webui.mjs';

await reaper.lifecycle.ready;
const fx = await connectJSFX(reaper, error => showError(error.message));
render(fx.state);
fx.onChange(render);
fx.onVisibilityChange(active => active ? startVisualUpdates() : stopVisualUpdates());
knob.onpointerdown = () => fx.beginEdit(0).catch(showError);
knob.oninput = () => fx.setParam(0, Number(knob.value)); // 0..1
knob.onchange = () => fx.endEdit().catch(showError);
knob.onpointercancel = () => fx.endEdit().catch(showError);
reset.onclick = () => fx.reset(0).catch(showError);
```

The helper keeps at most one request in flight and one pending value per parameter. Call `endEdit()` on pointer release, cancellation and keyboard completion. A single `setParam` outside a gesture creates a discrete Undo operation. Navigation, invalidation and a 30-second inactive gesture timeout end edits. Initialization and reload never write parameter defaults.

Visible bound editors receive bounded main-thread difference detection, with a 33 ms interval after each completed scan and native FX-change notifications requesting an earlier scan. Scans share a 1 ms budget and rotate between editors. The limit is 256 JSFX sliders plus REAPER's Bypass, Wet and Delta parameters. Editor discovery runs every 200 ms, with FX chain selection changes and Windows paint notifications requesting an earlier scan. Host calls never run on the audio thread. Bindings validate project lifetime, track GUID, FX GUID and source identity before every operation and resolve reordered FX indices again.

## Page lifetime

Pages are created on demand and retained when their editor closes or switches FX. The module keeps four hidden pages, with one temporary handoff slot for up to 500 ms. It evicts the least recently used page above that limit and releases pages after 60 seconds idle. FX deletion and project invalidation release their pages. Reopening a retained instance reuses its WebView and document, refreshes host state and waits for `present` before showing it. Active editors are not evicted.

The helper handles activation tokens and presentation acknowledgements. It drops pending edits on deactivation and ignores replies from earlier activations. When switching to an unassociated FX, the previous Web UI remains visible until the target native editor is available. Parameter edits and polling stop as soon as the selected FX changes. Hidden pages stop native parameter polling. Use `fx.active` and `onVisibilityChange` to pause application timers, spectrum subscriptions and animations. Browser visibility throttling does not replace application cleanup. A direct Service client must render `visibilityChanged.state`, then acknowledge its `activation` with `present`.

## Editor compatibility

Windows uses a native child HWND. macOS uses a SWELL child view containing WKWebView. Linux uses the existing SWELL/X11 WebKit helper embedding path. The parent FX editor controls placement and docking. On macOS, automatic size adjustments preserve the window's upper edge. App window placement and icons are not applied to that parent.

Associated native controls and editor painting are suppressed during initialization and while the page is active. Cached pages stay in hidden native containers. FX chains host pages in a persistent content viewport, so cached pages can follow list selection without waiting for native editor replacement. Clearing the list selection retains the page for REAPER's current FX editor. Standalone FX chains conceal native controls and apply the associated size before creating the viewport, then retain their window size while switching between associated editors. On Windows, a newly opened associated FX chain remains cloaked by DWM while its layout and page are prepared, including when opened from the TCP FX button. The chain is revealed after the page acknowledges `present` and its WebView is ready to display. The viewport uses the page background color during handoff. Failed initialization restores the original controls.

This adapter targets REAPER 7.82 through the REAPER 7 layout family and verifies the native editor's control hierarchy. Control IDs are private REAPER implementation details, not an SDK replacement API. Unrecognized structures and failed initialization retain or restore the original controls. Track FX Chain and floating track FX are supported. Take FX, input FX, container FX, legacy `@gfx` conversion and audio/`gmem` streams are outside this module.

## 简体中文

带有效同名 JSON 页面关联配置的原生 JSFX 自动使用 Web UI，无需全局首选项或 INI 配置。关联配置的 `enabled` 字段默认为 `true`。使用现有 Web Runtime、资源协议和 Service 桥，DSP 与参数状态由 REAPER 持有。

页面先调用 `getState`，再更新控件。拖动使用 `beginEdit`、合并后的 `setParam` 和 `endEdit`。`defaultNormalized` 是宿主提供的归一化默认值，不支持的元数据为 `null`。工程切换、FX 删除、编辑器关闭和页面重载会使旧请求失效。切换到未关联的 FX 时，保留当前 Web UI，直到目标原生编辑器可显示，选中实例变化后立即停止旧页面的参数编辑与轮询。普通 App 无法通过此 Service 访问其他 JSFX 实例。

页面按需创建，关闭或切换时隐藏并保留。缓存 4 个隐藏页面，切换期间允许额外保留 1 个页面，最长 500 毫秒。超过上限淘汰最久未使用的页面，闲置 60 秒后释放。再次打开时复用页面，刷新宿主参数并确认后显示。隐藏期间停止参数轮询，应用通过 `onVisibilityChange` 暂停频谱订阅、定时器和动画。初始化与复用期间均隐藏关联 JSFX 的原始控件并抑制原生编辑器绘制。FX Chain 使用持久内容容器，缓存页面随列表选择切换，无需等待原生编辑器重建。取消列表选中状态时，保留 REAPER 当前 FX 编辑器对应的页面。独立 FX Chain 在创建内容容器前隐藏原生控件并应用关联尺寸，切换关联编辑器时保持尺寸。Windows 通过 DWM 暂时隐藏新打开的关联 FX Chain，在布局与页面准备期间不呈现窗口，包括从 TCP 的 FX 按钮打开窗口。页面确认 `present` 且 WebView 可显示后才呈现窗口。切换期间，内容区域保留与页面颜色一致的背景。

适配器验证 REAPER 7.82 起的 7.x 编辑器结构，支持轨道 FX Chain 和浮动轨道 FX。macOS 自动调整尺寸时保持窗口上边缘位置。窗口结构不匹配或初始化失败时保留或恢复原生控件。Take FX、输入 FX、容器 FX、`@gfx` 转换及音频数据流不在支持范围内。
