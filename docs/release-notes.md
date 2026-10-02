# ReaWebAPI

## English

- Fix audio analysis failures stopping the shared native worker and leaving subsequent streams waiting indefinitely on Windows, macOS and Linux.
- Reuse track sampling across analysis streams and split aggregate reads across host ticks to reduce stalls when switching tracks. Release detached producers before opening replacements.

## 简体中文

- 修复 Windows、macOS 和 Linux 上音频分析异常导致共用原生工作线程退出、后续流持续等待数据的问题。
- 多路分析复用轨道采样，聚合读取分散到多个宿主周期，减少切换轨道时的卡顿。打开替代流前回收已断开的生产者。
