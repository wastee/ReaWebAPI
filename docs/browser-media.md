# Browser files and media

ReaWebAPI v0.3.8.20 adds an opt-in SDK module, `browser-files.mjs`, for exporting browser-generated files through the existing native save dialog. Copy the module into your App and import it explicitly. It does not change navigation or install global download behavior in other Apps.

```js
import { saveBlob, installDownloadHandler } from './browser-files.mjs';

// Returns the selected path, or null on cancellation.
await saveBlob(new Blob(['example'], { type: 'text/plain' }), 'example.txt');

// For libraries that append and click an <a download> element.
const dispose = installDownloadHandler({ onError: error => console.error(error) });
addEventListener('pagehide', dispose, { once: true });
```

The handler intercepts connected `data:` and `blob:` download links. Ordinary navigation, remote downloads, detached anchors and `window.open()` are unchanged. Only one export is processed at a time. A native save dialog selects the destination, then `reaper.fs.writeBinary` writes it with overwrite enabled. Native errors propagate from `saveBlob`, or reach `onError` for intercepted links. Exports above 16 MiB reject with `BUFFER_LIMIT`. The streaming URL reader enforces this bound before accumulating a larger payload. Disposing removes the listener and aborts pending URL reads. It cannot dismiss an already open native save dialog.

## Local playback

Use `<input type="file" multiple>` or DOM file drops to obtain browser `File` objects. Pass those objects directly to the player or create a Blob URL. Revoke URLs when their files are no longer in use. This path does not use `reaper.fs.readBinary` and is not subject to its 16 MiB binary-value limit. Browser memory, metadata parsing and codec limits still apply.

Windows uses WebView2's native file chooser. Linux uses WebKitGTK's [default file chooser](https://webkitgtk.org/reference/webkit2gtk/stable/class.FileChooserRequest.html). macOS implements [WKUIDelegate's open-panel callback](https://developer.apple.com/documentation/webkit/wkuidelegate/webview(_:runopenpanelwith:initiatedbyframe:completionhandler:)), including multiple selection and cancellation. WKOpenPanelParameters does not expose HTML `accept` filters to the host. Apps must validate selected file contents.

Web Audio, media codecs and WebGL2 require feature detection. Resume a suspended AudioContext from a user gesture. Close the audio context and release media resources when disposing a player. Blob URLs cannot be restored across documents. Save file metadata only, then ask users to select the original files again.

Allow local `connect-src`, `media-src blob:` and the player's required images, styles and Workers in the App CSP. Some visualization presets compile expressions and require `script-src 'unsafe-eval'`. Scope that policy to the App that needs it. None of these browser features imply REAPER master-audio routing or Native Stream conversion.

The save helper is shared JavaScript for Windows, macOS and Linux. Windows build and regression coverage do not substitute for WKWebView or WebKitGTK acceptance.
