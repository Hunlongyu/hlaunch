# 屏幕边缘停留唤起

## 配置

功能默认关闭，由用户主动启用。支持 Left、Right、Top、Bottom、TopLeft、TopRight、BottomLeft、BottomRight，可同时启用。首次启用时使用：边缘宽度 4 DIP、角落 16 DIP、停留 300 ms、采样 40 ms、冷却 500 ms。

允许范围建议：边缘 2–16 DIP；停留 100–1000 ms；采样 30–50 ms。配置解析必须做上下限校验，不能直接信任 JSON。

## 状态机

```text
Idle --进入热区--> Pending --达到停留时间--> Triggered
Pending --离开/被抑制--> Idle
Triggered --已过冷却且离开热区--> Idle
```

冷却计时从触发时开始；重新武装同时要求“鼠标已离开”和“冷却已结束”。停留期间若切换到另一热区，应重新计时。边缘触发只执行 Show，不能因 Launcher 已显示而将它隐藏。

## 采样和 UI 线程

使用 `CreateThreadpoolTimer` + `GetCursorPos`，不使用低级鼠标 Hook 或忙循环。定时器只采样和计算；触发后把 `ActivationContext` 投递给 UI 线程。关闭时必须先取消并等待在途回调，防止访问已销毁对象。

## 坐标和 DPI

- `GetMonitorInfoW` 的 `RECT.right` 和 `RECT.bottom` 是排他边界。
- 转换后的物理像素厚度记为 `t`，右边应判断 `x >= right - t && x < right`，下边同理。
- 布局配置使用 DIP；每次按目标显示器 DPI 转成物理像素，至少为 1 px。
- 角落优先于边；同一采样只能匹配一个规范化热区。
- 负坐标显示器、上下错位、不同缩放和动态插拔都必须测试。

## 多显示器模式

- `Every monitor edge`：当前显示器的每条边都可触发，包括显示器接缝。
- `Desktop outer edge`：只允许显示器集合的外轮廓触发。不能只比较虚拟桌面的外接矩形；L 形布局的裸露边也属于外轮廓。判断某一边是否外露时，应检查该边对应位置外侧是否存在相邻显示器。
- 显示拓扑或 DPI 变化后刷新缓存；不要每 40 ms 枚举全部显示器。

## 抑制规则

全屏抑制默认开启。判断前台窗口时优先使用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`，失败再退回 `GetWindowRect`；与目标显示器矩形做小容差比较，并排除最小化、不可见、cloaked 和桌面/Shell 窗口。该检测是启发式，需针对无边框游戏、视频和远程桌面运行验证。

P1 进程黑名单按规范化的前台可执行文件名匹配。只在前台 HWND 变化或准备触发时刷新并缓存进程信息，不能每次采样都重复查询；获取失败时不应阻塞或崩溃。

## 显示与隐藏

- 边缘触发的窗口位置跟随 `TriggerZone`，并限制在目标显示器工作区内。
- 热区与窗口点击区域之间不能有造成失焦隐藏的空隙。
- 显示后，边缘状态机不再管理窗口隐藏；启动条目、失焦或用户操作由 Launcher 自己处理。
- 动画建议 80–120 ms 的轻微 Fade/Slide；窗口可见后命中测试必须立即生效。

## 性能禁区

采样循环中不得执行 `EnumWindows`、进程快照、文件 I/O、图标提取或 Shell 查询。CPU 目标必须以 Release 构建在空闲系统上测量，不能仅凭实现推断。
