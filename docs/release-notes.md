# ReaWebAPI

## English

- Prepare and retain the shared Windows WebView2 environment on the first host idle tick, with controlled initialization retries.
- Register independent JSFX event subscriptions concurrently and clean up failed or late registrations.
- Add bounded native startup timings and JSFX connection measures without changing first-frame gating.
- Query independent Runtime Studio App metadata concurrently.
- Preserve Windows maximized state during first presentation, instance reuse, Hide/Show and Dock/Undock.

## 简体中文

- Windows 在首次主线程空闲回调中准备并保留共享 WebView2 环境，支持受控的初始化重试。
- JSFX 独立事件订阅改为并发注册，清理失败连接和迟到的订阅。
- 新增有界的原生启动计时及 JSFX 连接计时，保持现有首帧门控。
- Runtime Studio 并发查询互不依赖的 App 元数据。
- 修复 Windows 首次显示、实例复用、隐藏显示及停靠切换时丢失最大化状态。
