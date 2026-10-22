# ReaWebAPI

## English

- Avoid Canvas initialization for computed integer sRGB backgrounds with alpha 0 or 1 on Windows, macOS and Linux, retaining Canvas conversion for other colors.
- Recover initial JSFX size and parameter reads from STALE_SESSION with at most two retries after refreshing capabilities, without retrying writes.
- Add detailed native and browser timing for JSFX initialization and page background synchronization.

## 简体中文

- Windows、macOS 和 Linux 对计算样式中的整数 sRGB 背景色直接解析，透明度为 0 或 1 时跳过 Canvas 初始化，其他颜色保留 Canvas 转换。
- JSFX 初始尺寸与参数读取遇到 STALE_SESSION 时刷新能力并最多重试两次，不重试写入。
- 增加 JSFX 初始化与页面背景同步的原生及浏览器细分计时。
