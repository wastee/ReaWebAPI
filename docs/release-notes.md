# ReaWebAPI

## English

- Reduce JSFX initialization latency on Windows, macOS and Linux by completing initial capability, editor-size and parameter reads in the dispatching host tick.
- Reduce Windows JSFX first-frame preparation overhead by using JPEG for the internal readiness capture, preserving the existing presentation gates.

## 简体中文

- Windows、macOS 和 Linux 的 JSFX 初始能力、编辑器尺寸与参数读取在分发请求的宿主调度周期内完成，减少初始化等待。
- Windows JSFX 首帧就绪检查改用内部 JPEG 截图，降低准备开销，保留现有显示条件。
