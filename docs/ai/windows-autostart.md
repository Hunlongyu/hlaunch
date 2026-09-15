# Windows 开机自启方式全集与选型

- 文档性质：技术参考与选型依据。本文**不新增产品决策**；HLaunch 的既定选择见 `decisions/ADR-0008-logon-task-startup.md`，实现契约见 `windows-integration.md`。
- 适用系统：Windows 10 / Windows 11 客户端。标记为遗留的条目在旧系统或 32 位系统上另有差异。
- 精度约定：本文区分“**登录自启**”（用户登录后运行）与“**引导自启**”（系统启动、无人登录时运行）。日常口语里的“开机自启”，绝大多数场景实际需要的是登录自启。
- 证据边界：正文的机制描述在成稿时逐条对照了微软官方文档（附录链接均已实际访问验证可用），无法核实的说法不写入正文；项目相关结论只引用本仓库已有证据。

## 1. 先定义“一种方式”的边界

Windows 上没有统一的“开机自启 API”。所谓自启方式，是若干**互不相同的注册位置或扩展点**，由**不同的宿主进程**在**不同的启动阶段**读取。因此判断“一共有多少种”，取决于口径：

| 口径 | 数量 | 说明 |
| --- | --- | --- |
| 产品级可选方案 | **7 种** | 能作为正式产品功能交付、可查询、可禁用、可卸载，且不需要注入系统进程 |
| 完整落点清单 | **53 个** | 含遗留机制、系统级挂钩、Shell/COM 扩展点与被安全软件视为持久化恶意行为的位置 |

两种口径都要用同一把尺子衡量。本文用五个维度描述每个落点：

1. **触发点**：引导期 / 服务初始化 / Winlogon 登录 / Shell 就绪 / 登录后事件。
2. **运行身份**：SYSTEM、其他用户，或当前用户；普通权限或提升。
3. **宿主**：谁读取并执行这个位置（内核与会话管理器、服务控制管理器、计划任务服务、Winlogon、Explorer）。
4. **用户可见性**：用户能否在“任务管理器 → 启动”或“设置 → 应用 → 启动”里看到并禁用。
5. **合规性**：是否需要管理员、是否要求数字签名、是否会被 Defender/EDR 判定为可疑持久化。

> 关键事实：**“开机自启”的可靠性差异，主要来自宿主进程不同，而不是写法不同。** Run 键由 Explorer 枚举，计划任务由计划任务服务启动，服务由 SCM 启动。宿主越早、越独立，越不容易被登录期的队列中断影响。

## 2. 启动时间轴

| 阶段 | 宿主 | 该阶段可用的机制 |
| --- | --- | --- |
| 内核初始化 | 内核 | 引导驱动（`SERVICE_BOOT_START`）、`BootExecute` |
| 会话管理器 | `smss.exe` | 系统启动驱动与服务（`SERVICE_SYSTEM_START`）、自动启动服务 |
| 服务控制管理器 | `services.exe` | `SERVICE_AUTO_START`、延迟自动启动、触发启动服务、WMI 永久订阅（随 Winmgmt 启动） |
| Winlogon 登录 | `winlogon.exe` | `Userinit`、`Shell`、凭据提供程序、Active Setup、组策略登录脚本 |
| 用户 Shell 就绪 | `explorer.exe` | Run / RunOnce、启动文件夹、打包应用 StartupTask |
| 登录后事件 | 计划任务服务等 | 登录触发任务、解锁触发、会话连接触发、空闲触发、事件触发 |

时间轴说明三条：

- 上表是**典型次序，不是保证**。Windows 不承诺自启程序之间的执行顺序。
- 计划任务服务的登录触发与 Explorer 的 Run 枚举属于**两条独立路径**，没有先后契约。
- 用户登录本身也可能被延后（自动登录、域策略、快速启动），这会整体平移后续阶段。

### 2.1 前提条件：安全模式与电池

自启方案在异常状态下是否仍然工作，是选型时容易被忽略的一维。

| 方案 | 安全模式 | 电池供电（默认设置下） |
| --- | --- | --- |
| Run / RunOnce、启动文件夹 | **不运行**（默认忽略这些键；RunOnce 值名加 `*` 前缀可强制运行） | 运行 |
| 登录触发的计划任务 | **不运行**：官方明确“Task Scheduler 服务在安全模式下不能使用/不运行” | **不运行**，除非显式关闭 `DisallowStartIfOnBatteries` |
| 引导触发的计划任务 | **不运行**（同上，同一服务） | 同上 |
| Windows 服务（自动启动） | 部分按启动类型运行（服务不依赖计划任务服务） | 运行 |
| 打包应用 StartupTask | 未验证（依赖 Shell） | 运行 |

结论：**没有任何一种方案在安全模式下普遍可用**。这是所有登录自启方案的共同边界，不需要为了“安全模式下也要启动”而升级到服务——那种需求本身应重新评估。

## 3. 一共多少种

### 3.1 产品级可选方案：7 种

| # | 方案 | 注册位置 | 触发点 | 身份与权限 | 需要管理员 | 出现在“启动”页 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 当前用户 Run 键 | `HKCU\...\CurrentVersion\Run` | Shell 就绪 | 当前用户，普通 | 否 | 是 |
| 2 | 当前用户 RunOnce | `HKCU\...\CurrentVersion\RunOnce` | Shell 就绪 | 当前用户，普通 | 否 | 是（执行后自删） |
| 3 | 当前用户启动文件夹 | `%APPDATA%\...\Startup` | Shell 就绪 | 当前用户，普通 | 否 | 是 |
| 4 | 本机 Run / 公共启动文件夹 | `HKLM\...\Run`、`%ProgramData%\...\Startup` | Shell 就绪 | 所有用户，普通 | **是** | 是 |
| 5 | 登录触发的计划任务 | Task Scheduler 2.0 | Winlogon 登录后 | 指定用户，普通或提升 | 否（当前用户） | 否 |
| 6 | 引导/系统触发的计划任务或服务 | Task Scheduler 2.0 / SCM | 引导期 | SYSTEM | **是** | 否 |
| 7 | 打包应用 StartupTask | `AppxManifest.xml` 声明 | Shell 就绪 | 当前用户，包身份沙箱/完全信任 | 安装层面 | 是 |

### 3.2 完整落点清单：53 个

分组编号在后续章节沿用：A 用户级注册表 Run 系列、B 机器级 Run 系列、C 启动文件夹、D 启动审批与延迟、E 任务计划程序、F 服务、G 登录挂钩与早期执行、H Shell/COM 扩展与深层注入、I 打包应用、J 间接触发。

| 组 | 数量 | 性质 |
| --- | --- | --- |
| A 用户级 Run 系列 | 6 | 半数以上是遗留或被策略取代 |
| B 机器级 Run 系列 | 7 | 需要管理员，安装器常用 |
| C 启动文件夹 | 3 | 产品级可用 |
| D 启动审批与延迟 | 2 | 不是启动点，是 Explorer 的控制面 |
| E 任务计划程序 | 5 | 产品级可用，能力最强 |
| F 服务 | 5 | 需要管理员，不适用于托盘类应用 |
| G 登录挂钩与早期执行 | 9 | 除 Active Setup 与组策略脚本外，基本禁止 |
| H Shell/COM 扩展与深层注入 | 12 | 多数禁止；注册自己的 Shell 扩展与 per-user COM 是正当做法 |
| I 打包应用 | 3 | 仅适用于有包身份的应用 |
| J 间接触发 | 1 | 屏保，禁止 |

## 4. 产品级方案详解

### 4.1 方案 1：当前用户 Run 键（A1）

- **位置**：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`，值名即标识，值数据为命令行（`REG_SZ` 或 `REG_EXPAND_SZ`）。
- **机制**：用户登录、Explorer 启动后，Shell 读取该键并逐个创建进程。同一键下多个值之间**没有顺序保证**（官方原文：如果同一个键下注册了多个程序，运行顺序不确定）。
- **官方确认的行为**：
  - 值数据是命令行，**不超过 260 字符**。
  - 系统**不保证及时性**，为改善用户体验，系统可以把 Run 键与启动组的执行**推迟**到更不容易打扰前台操作或彼此干扰的时刻——这是“同一台机器上启动时间飘忽”的官方依据。
  - 安全模式下默认**忽略**这些键。
  - 被启动的程序**不应在执行期间写同一个键**，否则会干扰同键下其他程序的执行。
- **权限**：当前用户写入自己的配置单元，无需管理员，无 UAC。
- **优点**：
  - 实现成本最低，读写各一次注册表调用，无 COM、无服务依赖。
  - 出现在“任务管理器 → 启动”和“设置 → 应用 → 启动”里，用户能自行看到并禁用，符合用户预期。
  - 环境变量可用 `REG_EXPAND_SZ` 展开（例如 `%LOCALAPPDATA%`），对安装位置变化有一定容忍度。
  - 随用户配置单元漫游（域漫游配置文件场景下跟随用户）。
- **缺点**：
  - **依赖 Explorer 的枚举过程**。枚举被中断时该次登录不会启动，本项目 2026-09-12 实测过这种中断（见 ADR-0008），仅重写注册表值无法修复。
  - 无延迟控制、无失败重试、无执行时限、无多实例策略——这些都要自己在程序内实现。
  - 命令行长度**不超过 260 字符**（官方硬限制），长路径需要改用启动文件夹快捷方式或计划任务。
  - 系统可主动推迟 Run 键的执行（官方行为），因此“登录后多久启动”不可控。
  - **需要提权的程序会被阻止**：Run 键与启动文件夹都属“登录路径”，官方明确会阻止需要管理员权限的程序从这些位置启动（见 C1 的说明），所以只能放普通权限程序。
  - 命令行为单个字符串，引号与参数必须由调用方正确拼接，是常见缺陷来源。
  - 程序升级或移动后旧路径残留，注册表仍指向不存在的文件，表现为“静默失效”。
  - 用户可以禁用（写入 `StartupApproved`），此时注册表值仍在，仅查询值本身会误判为“已启用”。

### 4.2 方案 2：当前用户 RunOnce（A2）

- **位置**：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\RunOnce`。
- **机制**：与 Run 相同的枚举路径，但条目在执行前被删除，因此只运行一次。
- **官方确认的行为**：
  - 默认在命令执行**之前**删除值，因此失败会导致“下次不再尝试”。
  - 值名前缀 `!`（感叹号）可把删除**推迟到命令运行之后**——这是实现“失败可重试”的标准写法，也是 RunOnce 唯一值得记住的技巧。
  - 值名前缀 `*`（星号）可让程序**在安全模式下也运行**（默认安全模式忽略这些键）。
  - 官方明确要求：不应持续重建 RunOnce 条目，否则会干扰 Windows 安装程序。
  - `HKLM\...\RunOnce` 只在**管理员组成员登录时**执行（见 B2）。
- **优点**：适合“安装后首次运行”“升级后重建缓存”等一次性初始化；无需额外状态文件；加 `!` 前缀后具备基本重试能力。
- **缺点**：
  - 不加 `!` 前缀时失败不重试、不恢复。
  - 不适合作为常驻自启。把常驻程序写成 RunOnce 自复活，是安全软件明确关注的行为，且官方文档直接反对。
- **结论**：仅在一次性初始化场景使用；常驻自启不要用。

### 4.3 方案 3：当前用户启动文件夹（C1）

- **位置**：`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`（`shell:startup`）。
- **机制**：Shell 就绪后，Explorer 枚举文件夹内容并启动。放 `.lnk` 快捷方式或可执行文件均可。
- **权限**：写自己的文件夹，无需管理员。
- **优点**：
  - 用 `IShellLinkW` + `IPersistFile` 写快捷方式时，**目标、参数、工作目录、显示方式分别保存**，天然避免命令行拼接与 260 字符限制问题。
  - 用户界面友好：用户可直接在开始菜单的“启动”里看到、双击测试、删除。
  - 同样出现在“任务管理器 → 启动”页，可被用户启用/禁用。
- **缺点**：
  - 与 Run 键相同的 Explorer 依赖与队列中断风险。
  - 写 `.lnk` 需要正确设置 `SetPath`/`SetArguments`/`SetWorkingDirectory`，以及 `SLDF_` 标志；比写注册表值复杂。
  - **需要提权的程序会被直接阻止，不是弹 UAC**：官方明确，从“每用户启动文件夹、每机器启动文件夹、每用户 Run 键、每机器 Run 键”启动且需要管理员权限的程序，Windows 会**阻止其运行**（目的是避免用户无法把提权请求对应到具体程序，同时遏制恶意软件把自己放进登录路径）。因此登录路径只能放 `asInvoker` 的普通权限程序；需要提权的功能必须拆成“普通权限应用 + 按需提权的辅助组件”或“应用 + 服务”。
  - 与 Run 键一样存在路径失效残留问题，且更难在代码里“发现”残留（需要读回并解析 `.lnk`）。
- **结论**：需要用户可见、可手工操作的兼容性方案时使用；参数复杂时优于 Run 键。

### 4.4 方案 4：机器级 Run / 公共启动文件夹（B1、C2）

- **位置**：`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`、`%ProgramData%\Microsoft\Windows\Start Menu\Programs\Startup`。
- **机制**：与用户级完全相同的枚举路径，但对所有用户生效。
- **优点**：一次注册对所有用户生效；适合设备级工具（输入法、驱动配套程序、公共终端）。
- **缺点**：
  - **需要管理员写入**，安装时必然触发 UAC；对便携、免安装的单文件程序是致命缺点。
  - 32 位安装包写入 `HKLM` 时会落到 `WOW6432Node` 视图（见 B7），排查困难。
  - 无法表达“只对某个用户生效”，多用户场景会替所有用户启动。
  - 卸载不干净会留下对所有用户生效的失效启动项。
- **结论**：仅安装型、设备级软件使用。用户态启动器不要用。

### 4.5 方案 5：登录触发的计划任务（E1）— 推荐首选

- **位置与接口**：Task Scheduler 2.0（Vista 及以上），通过 COM `ITaskService` / `ITaskDefinition` 注册；任务定义同时以 XML 存放于 `%SystemRoot%\System32\Tasks\`，并有内部缓存。**不要直接编辑文件和缓存**，只使用受支持的 COM（或 `schtasks.exe`）接口。
- **机制**：任务计划服务（`svchost` 承载的 Schedule 服务）在用户交互登录后按触发器启动任务，**不经过 Explorer 的 Run 枚举**。
- **关键配置（当前用户普通权限登录任务）**：

| 设置 | 取值 | 官方默认 | 原因 |
| --- | --- | --- | --- |
| 主体 `UserId` | 当前用户 SID | — | 绑定到具体用户；留空即“任意用户登录都触发” |
| `LogonType` | `InteractiveToken` | — | 不保存密码，只在已存在的交互会话中运行 |
| `RunLevel` | `LeastPrivilege`（LUA） | 低权限 | 普通权限，保留拖放、UIPI 一致性与最小权限 |
| 触发器 | `LogonTrigger`，`Delay = PT0S` | `Delay = PT0M` | 登录即触发，不额外等待 |
| `Priority` | `6` | **`7`** | 6 = `NORMAL` + `IoPriorityNormal` + `MEMORY_PRIORITY_MEDIUM`；默认 7 = `BELOW_NORMAL` + `IoPriorityLow` + `MEMORY_PRIORITY_LOW`，交互式程序必须显式改 |
| `ExecutionTimeLimit` | `PT0S` | **`PT72H`** | 默认 72 小时后任务被停止（且默认允许硬终止），常驻程序必须显式不限时 |
| `DisallowStartIfOnBatteries` | `false` | **`true`** | 默认在电池供电时不启动 |
| `StopIfGoingOnBatteries` | `false` | **`true`** | 默认切到电池即被停止 |
| `MultipleInstances` | `IgnoreNew` | `IgnoreNew` | 抵御“计划任务服务重启后重跑登录任务”造成的重复拉起，与单实例逻辑形成双保险 |
| `StartWhenAvailable` | `true`（按需） | `false` | 错过后补运行；官方注明只对定时任务有意义 |
| `RegistrationInfo.Source` | 稳定的应用标识 | 空 | 所有权标记，防止覆盖同名任务 |

> 注册任务时**不要使用** `S4U`：它虽然也不存密码，但运行在**非交互桌面**，且没有网络凭据、不能访问加密文件——对需要托盘或联网的启动器全是硬伤。`PASSWORD` 类型还要求目标账户具备“作为批处理作业登录”权限。也不要使用已被弃用的 `InteractiveTokenOrPassword`（现值语义等同 `PASSWORD`，官方明说不推荐用于新任务）。


- **优点**：
  - **不依赖 Explorer**：Explorer 崩溃、重启或被替换为自定义 Shell 时，登录任务仍然按计划触发；也避开了 Run 枚举中断这一类故障。
  - **对“服务启动晚于登录”有兜底**：官方说明，计划任务服务启动时会枚举当前已登录的用户，并运行匹配该用户的登录触发任务——即服务重启或延迟启动也不会漏掉本次登录（代价是也会重跑一次，见下）。
  - 能力最完整：延迟、失败重试、多实例策略、不限时、唤醒、错过后补运行（`StartWhenAvailable`）都在系统层实现，程序无需自己兜底。
  - 无需管理员、无需保存密码：当前用户 + `InteractiveToken` 的任务由普通用户注册成功，登录时无 UAC 提示。
  - 状态可精确查询：任务是否存在、是否启用、绑定哪个用户、什么权限级别、执行文件与参数是什么，都能回读校验，能识别“任务被禁用”“EXE 路径已变化”等失效状态。
  - 参数、工作目录与可执行文件分别存放，不存在命令行拼接与长度问题。
  - 卸载与迁移可控：删除任务即彻底移除；迁移时可以先建后删，旧值保留到新机制成功。
- **缺点**：
  - **不出现在“任务管理器 → 启动”页**，用户在熟悉的入口看不到它，可能认为程序没有自启或无法关闭。这是首要代价，必须用程序内的显式开关和说明补偿。
  - 在“任务计划程序”里可见但入口较深；高级用户与安全软件能看到它是好事，普通用户找不到是坏事。
  - 计划任务默认 `Priority` 低于正常，若忘记显式设置，交互响应会变差。
  - 默认 `DisallowStartIfOnBatteries` 为真，笔记本上可能“看起来没启动”，必须显式关闭。
  - **计划任务服务不运行时全部失效**：安全模式下服务不启动，所有登录/引导任务都不会执行。这一点与 Run 键相同（Run 键可用 `*` 前缀在安全模式下强制运行，计划任务没有等价手段）。
  - **服务重启会重跑登录任务**：官方语义是“服务启动时枚举已登录用户并运行匹配的登录任务”，因此计划任务服务重启可能导致同一用户被再次拉起，必须靠 `IgnoreNew` + 程序自身单实例保护。
  - 不要使用 `Hidden = true`：它会让任务在计划程序 UI 中默认不可见，用户既找不到也关不掉，观感与恶意软件无异。
  - 复杂度和代码量高于 Run 键：需要 COM 连接、`ITaskDefinition` 组装、BSTR 与 VARIANT 生命周期管理、后台线程执行 COM。
  - 任务名与 `Source` 需要设计，否则可能与他人任务同名冲突，或在升级后留下旧任务。
  - `RunLevel = HighestAvailable` 的任务需要管理员注册，但注册完成后可以在该用户登录时**不出现 UAC 提示即获得提升权限**。这既是能力也是权限提升风险，普通启动器不应使用；一旦误用，用户看不到任何提示。
- **结论**：**交互式桌面应用的默认首选**。以“用户主动开启”为默认状态，程序内提供开关与状态显示。

### 4.6 方案 6：引导/系统触发的任务或服务（E3、F1–F5）

- **机制**：
  - 计划任务 `BootTrigger`：系统启动时以 SYSTEM 身份运行，可设置延迟；任务计划服务自带失败重试与状态查询。
  - Windows 服务：由 SCM 按 `Start` 类型启动，`SERVICE_AUTO_START`、延迟自动启动、`SERVICE_SYSTEM_START`、`SERVICE_BOOT_START`，另有触发启动（设备到达、网络可用等）。
- **优点**：启动最早、最可靠、与登录无关；服务支持失败自动重启、日志、依赖与启动顺序；可在无人登录时运行（做守护、更新、代理）。
- **缺点**：
  - **需要管理员安装**，必然 UAC；卸载残留会长期影响系统。
  - **Session 0 隔离**：服务不能显示界面、不能加托盘图标、不能与登录用户交互。要服务 + 每会话代理进程（需要 SYSTEM 权限做 `CreateProcessAsUser` 一类的跨会话启动），复杂度和维护成本陡增。
  - 不出现在“启动”页，用户无法在常规入口管理。
  - 常驻服务、引导驱动是被安全软件重点关注的持久化形态，误报风险高。
  - 服务崩溃会进入 SCM 恢复策略，调试与排错链路长。
- **结论**：只用于真正需要“无人登录也要运行”的场景（更新器、设备守护、企业代理）。**面向用户的启动器、托盘程序不要用**，用登录计划任务即可。

### 4.7 方案 7：打包应用 StartupTask（I1、I2）

- **机制**：应用包在 `AppxManifest.xml` 中声明 `uap5:Extension Category="windows.startupTask"` + `uap5:StartupTask`（`TaskId`、`DisplayName`、`Enabled`），由系统在用户登录后启动；程序侧通过 `Windows.ApplicationModel.StartupTask` 查询与请求启用。
- **官方确认的关键行为**（`StartupTask` 类文档）：
  - 打包**桌面应用**（Desktop Bridge / 完全信任）与 UWP 都支持。桌面应用支持自 1607 起，UWP 自 1709 起；桌面应用需 `EntryPoint="Windows.FullTrustApplication"`。
  - 打包桌面应用可在清单中直接写 `Enabled="true"`，**不需要**调用 `RequestEnableAsync`，也没有用户同意对话框；UWP 侧 `Enabled` 被忽略，必须由用户在 UI 线程触发同意对话框后才启用，且 UWP 启动应用会**以最小化方式启动**。
  - 打包桌面应用允许声明多个 `startupTask` 扩展，各自指向不同 EXE。
  - 无论哪种，用户必须先运行过一次应用，或在“设置 → 应用 → 启动”中启用。
  - 状态用 `StartupTaskState` 表达：`Enabled`、`Disabled`、`DisabledByUser`、`DisabledByPolicy`、`EnabledByPolicy`。
  - `RequestEnableAsync()` **不会覆盖用户在任务管理器中的禁用**，此时必须引导用户自行重新启用。
  - `TaskId` 为 1–32767 字符，`DisplayName` 为 1–256 字符且**就是任务管理器中显示的名字**；系统还会自动分析启动影响。
  - 该扩展**需要包身份**：未打包的普通 EXE 既不能用清单声明，也不能用 `StartupTask` API 管理启动项。面向未打包应用的新 API 只有 Windows App SDK 的 `ActivationRegistrationManager.RegisterForStartupActivation`，且仍标注为**实验性**。
- **优点**：与系统启动管理界面原生集成；状态枚举能区分“被用户禁用”“被策略禁用”，程序可以正确表达“用户已拒绝”，而不是反复改写注册表对抗用户。
- **缺点**：
  - **需要包身份**（MSIX/UWP），未打包的单个 EXE 用不了；引入打包就等于改变分发、签名与安装模型。
  - 用户禁用后程序无法自行恢复，只能提示——这既是优点（尊重用户）也是缺点（用户误操作后无法一键修复）。
  - `DisplayName` 会出现在任务管理器中，命名与本地化需要设计。
  - 依赖包与 Shell 基础设施，启动时刻由系统调度，程序无法设置延迟。
- **结论**：走商店或 MSIX 分发时才考虑；单文件绿色程序不适用。

## 5. 其余落点清单（分组）

以下条目**不建议用于产品自启**，但必须知道它们存在：故障排查（用户机器上谁会启动什么）、安全评审、以及实现“不要碰这些位置”的边界。

结论列含义：**可用**（有正当使用场景）、**不推荐**（技术上可行，代价或风险不合理）、**禁止**（属于持久化恶意行为特征，或需要系统级签名与提权）。

### 5.1 A 组：用户级 Run 系列

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| A1 | `HKCU\...\CurrentVersion\Run` | Shell 就绪，当前用户 | 简单、用户可见 | 依赖 Explorer 枚举、无重试、≤260 字符、系统可推迟、安全模式忽略 | 可用 |
| A2 | `HKCU\...\CurrentVersion\RunOnce` | Shell 就绪，当前用户 | 一次性初始化，`!` 前缀可延迟删除 | 失败不重试（未用 `!` 时） | 可用（仅一次性） |
| A3 | `HKCU\...\Policies\Explorer\Run` | Shell 就绪，当前用户 | 策略级、优先级高于普通 Run | 属策略通道，程序不该自行写入 | 不推荐 |
| A4 | `HKCU\...\Windows NT\CurrentVersion\Windows` 的 `Load` | 登录，当前用户 | 无 | 16 位遗留语义 | 禁止 |
| A5 | 同上键的 `Run` | 登录，当前用户 | 无 | 同上，且与 A1 混淆排障 | 禁止 |
| A6 | `Win.ini` `[windows] load=` / `run=` | 登录，当前用户 | 无 | 16 位遗留，行为随版本退化 | 禁止 |

### 5.2 B 组：机器级 Run 系列

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| B1 | `HKLM\...\CurrentVersion\Run` | Shell 就绪，所有用户 | 一次注册全局生效 | 需管理员、无法按用户区分 | 可用（安装型软件） |
| B2 | `HKLM\...\CurrentVersion\RunOnce` | 重启后**仅管理员组成员登录时**，所有用户 | 一次性全局初始化 | 需管理员、登录者必须是管理员组、失败不重试 | 可用（仅一次性，受限） |
| B3 | `HKLM\...\CurrentVersion\RunOnce\Setup` | 安装/首次登录阶段 | 安装器收尾 | 属安装器通道 | 不推荐 |
| B4 | `HKLM\...\CurrentVersion\RunOnceEx` | 登录时由 `rundll32` 处理 | 曾支持顺序与依赖 | 遗留机制，现代系统语义不明确 | 不推荐 |
| B5 | `HKLM\...\Policies\Explorer\Run` | Shell 就绪，所有用户 | 策略下发 | 属策略通道 | 不推荐 |
| B6 | `HKLM\...\CurrentVersion\RunServices` / `RunServicesOnce` | — | 无 | Windows 9x 遗留；**现行文档未确认其在 Windows 10/11 上还有什么行为，应按“无效”对待** | 禁止 |
| B7 | `WOW6432Node` 视图（`HKLM\SOFTWARE\WOW6432Node\...\Run`） | 同上 | 32 位程序自动落入 | 双视图导致重复项与排障困难 | 仅需知晓 |

> 排障提示：64 位 Windows 上 `HKLM\SOFTWARE\...\Run` 与 `HKLM\SOFTWARE\WOW6432Node\...\Run` 是**两个位置**，32 位进程写入会重定向。查启动项时两个都要看（HKCU 侧的对应视图是 `StartupApproved\Run32`）。

### 5.3 C 组：启动文件夹

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| C1 | 用户 Startup 文件夹 | Shell 就绪，当前用户 | 用户可见可管理、快捷方式可带参数与工作目录 | 依赖 Explorer 枚举 | 可用 |
| C2 | 公共 Startup 文件夹 | Shell 就绪，所有用户 | 一次注册全局生效 | 需管理员 | 可用（安装型软件） |
| C3 | `User Shell Folders` 重定向 Startup 路径 | — | 企业重定向场景 | 改动用户 Shell 文件夹影响面大 | 不推荐 |

### 5.4 D 组：启动审批与延迟（Explorer 控制面）

| 编号 | 落点 | 说明 | 结论 |
| --- | --- | --- | --- |
| D1 | `HKCU\...\Explorer\StartupApproved\Run` / `Run32` / `StartupFolder` | “任务管理器 → 启动”页的禁用状态就存这里。`REG_BINARY`，本项目实测“已启用”为 12 字节且首 DWORD 低字节为 `2`；禁用状态与其他长度必须视为未知格式。任务管理器**不会删除** Run 值，只改这里的记录；也**并非每个 Run 条目都有**对应记录（缺失即视为未禁用）。**这不是公开 API**（微软未文档化其格式），只能保守读取，不能扩展解读或主动改写对抗用户。 | 只读参考 |
| D2 | `HKCU\...\Explorer\Serialize\StartupDelayInMSec` | 控制 Run 键与启动文件夹项在登录后的延迟，用于给桌面留出就绪时间。官方在 Run/RunOnce 文档中明确：系统可自行把 Run 键与启动组的执行推迟到不打扰前台操作的时刻。计划任务与服务**不受**它影响。 | 仅需知晓 |

> 这两项不是启动点，而是 Explorer 的配套状态。它们解释了三个常见现象：注册表值存在却不启动（被 D1 禁用）、同一台机器上启动时间飘忽（D2 与系统负载）、以及计划任务方案为什么“管不到启动页”。

### 5.5 E 组：任务计划程序

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| E1 | 登录触发任务（指定用户） | 登录后，指定用户，普通或提升 | 不依赖 Explorer、能力完整、无需密码 | 不在启动页可见 | **可用（首选）** |
| E2 | 登录触发任务（任意用户） | 登录后，任意登录用户 | 一次注册多用户生效 | 默认权限与身份语义更宽，易误用 | 需要时使用 |
| E3 | 引导触发任务 | 引导时，SYSTEM | 最早、无人登录也运行 | 需管理员、无用户会话 | 仅系统级需求 |
| E4 | 其他触发（解锁、会话连接、空闲、事件、注册时） | 登录后事件 | 可表达“解锁时才启动”等策略；`SessionUnlock` / `ConsoleConnect` 可用于“外壳重启或解锁后确保运行” | 触发条件复杂，用户难以预期 | 需要时使用 |
| E5 | 任务计划程序 1.0（`.job` / `AT` 服务） | — | 官方仍列出对 XP/Vista 的支持 | **官方已标注 1.0 接口弃用**（“All new code should target the Task Scheduler 2.0 API”）；仅 C++ 接口、不支持现代触发器与设置项 | 不推荐 |

> 为什么首选 E1 而不是 E4：`LogonTrigger` 是语义最清晰、可回读校验、且与“用户主动开关”一致的触发器；事件类触发器会把自启行为变成难以解释的偶发行为。

### 5.6 F 组：Windows 服务

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| F1 | 自动启动服务（`SERVICE_AUTO_START`） | SCM 启动，SYSTEM/服务账户 | 可靠、可恢复、可排序；**官方建议优先用触发器启动而非自动启动** | 需管理员、无法与桌面交互 | 仅系统级需求 |
| F2 | 延迟自动启动（`DelayedAutostart`） | 自动启动服务之后 | 降低登录期争抢 | 启动时刻更晚且不稳定；官方口径是“其他自动启动服务之后再加**约两分钟**”，且不保证具体时刻 | 仅系统级需求 |
| F3 | 系统/引导启动（`SERVICE_SYSTEM_START` / `BOOT_START`） | 会话管理器阶段 | 最早 | **这两个启动类型只对驱动服务有效**；应用程序无法使用，错误可致系统无法启动 | 禁止 |
| F4 | 触发启动服务（设备到达、网络变化等） | 事件触发 | 按需启动，节省资源 | 触发条件与调试复杂 | 仅系统级需求 |
| F5 | 内核/文件系统过滤驱动 | 引导期 | 可拦截系统调用 | 需 WHQL 签名与极高权限 | 禁止 |

> 附：**每用户服务**（per-user services）确实存在于 Windows 10/11——用户登录时由系统创建、注销时停止并删除，并以该用户身份运行。但它由系统为内置组件创建，**第三方应用没有受支持的注册途径**，所以不构成一条可用的自启方案。选型时不要把它误当成“不需要管理员的用户级服务”。

### 5.7 G 组：登录挂钩与早期执行

| 编号 | 落点 | 触发与身份 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| G1 | `Winlogon\Userinit` | 登录时，SYSTEM/用户 | 无 | 写错会导致无法登录；典型劫持位置 | 禁止 |
| G2 | `Winlogon\Shell` | 登录时，用户 | 自定义 Shell（信息亭） | 替换 Explorer 会破坏用户环境；**受支持的替代是 Shell Launcher**（仅 Enterprise / Education / IoT 版） | 禁止（信息亭专用） |
| G3 | `Winlogon\Notify` | 登录时 | 无 | **已死**：官方说明 GINA 与 Winlogon 通知包**自 Vista 起被忽略** | 禁止 |
| G4 | Active Setup（`Installed Components\{GUID}`） | 每用户首次登录 | 每用户一次性初始化、支持版本比较 | 同步阻塞登录；被滥用后会显著拖慢登录；**微软未把它当作正式的自启扩展点文档化**，依据主要来自安装器实践 | 仅一次性初始化 |
| G5 | `Session Manager\BootExecute` | 引导期，原生 | 无 | 需原生镜像；写错导致系统无法启动 | 禁止 |
| G6 | 组策略登录/启动脚本 | 登录/引导 | 企业集中管理；启动脚本以 SYSTEM 在**用户被邀请登录之前**运行；在 GPEDIT/GPMC 中可见可审计 | 登录脚本以登录用户身份运行（非管理员）；默认最多等待 600 秒（0 = 无限）；域加入后本地 GPO 会被域 GPO 覆盖 | 仅企业 |
| G7 | WMI 永久事件订阅 | Winmgmt 启动后，SYSTEM | 表达能力极强（WQL 事件级） | 需管理员；消费者以 LocalSystem 在会话 0 运行；对用户完全不可见；失败多为静默；**Defender 有专门的攻击面缩减规则会拦截它** | 禁止 |
| G8 | `AppInit_DLLs`（配合 `LoadAppInit_DLLs`、`RequireSignedAppInit_DLLs`） | 每个加载 user32 的进程 | 无 | 官方明确“**不推荐**”：会注入所有交互式进程，即使合法也可能造成系统死锁与性能问题；**启用 Secure Boot 时该机制被直接禁用**；Win8 桌面应用认证明确禁止使用它 | 禁止 |
| G9 | IFEO `Debugger` 值 | 目标进程启动时 | 调试器替代启动 | 会破坏目标程序，典型劫持 | 禁止 |

> **WMI 永久订阅为什么被单独点名**：微软在 Defender 攻击面缩减（ASR）里有专门的规则 “Block persistence through WMI event subscription”，用于阻止“通过 WMI 事件订阅实现持久化”。它需要管理员、以 SYSTEM 运行、对任务管理器/服务/计划任务三个界面都不可见、失败通常悄无声息，且删除其磁盘文件也无法清除。对正规软件是纯负分，排查时可用 `Autoruns -a m` 查看。
>
> **本地 GPO 在非域机器上也可用**：`gpedit.msc` 可以配置本地计算机/用户策略，脚本存放在隐藏目录 `%SystemRoot%\System32\GroupPolicy`。但对消费级安装包不合适：配置痕迹明显、在域环境中会被域策略覆盖，且部分 SKU 不提供 `gpedit.msc`。

### 5.8 H 组：Shell/COM 扩展与深层注入

| 编号 | 落点 | 加载方式 | 结论 |
| --- | --- | --- | --- |
| H1 | `ShellServiceObjectDelayLoad` | Explorer 启动时加载 COM | 禁止（**行为未文档化**：微软文档中无此页，Win10/11 是否仍被读取未验证） |
| H2 | `ShellExecuteHooks` | Shell 动作时加载 | 禁止（**未文档化**：键路径与当前加载条件均无官方说明） |
| H3 | 右键菜单 / 属性页 / 拖放处理器等 Shell 扩展 | Explorer 进程内按需加载 | **可用**（正当扩展点；须 `ThreadingModel = Apartment`，崩溃会拖垮 Explorer；Win11 推荐用 `IExplorerCommand` + 包身份） |
| H4 | `ShellIconOverlayIdentifiers`（图标叠加） | Explorer 进程内 | 不推荐（槽位有限且优先级不可靠；微软推荐改用云文件 API 的状态图标） |
| H5 | Browser Helper Objects | IE 加载 | 禁止（IE 已退役） |
| H6 | `HKCU\Software\Classes\CLSID` COM 劫持 | 组件被使用时 | 劫持他人 CLSID：禁止；注册**自己的** per-user COM：可用 |
| H7 | 凭据提供程序 | 登录界面加载 | 禁止（MFA / 智能卡类安全产品除外） |
| H8 | LSA 认证包 / 安全包 / 通知包（密码过滤器） | 登录与改密时加载 | 禁止（运行在 lsass，LocalSystem） |
| H9 | 时间提供程序 | 时间服务加载 | 禁止 |
| H10 | NetSh 帮助程序 | 由 `netsh.exe` 自身启动时加载（**不是自启点**） | 禁止 |
| H11 | 打印监视器 / 打印处理器 | 打印后台服务启动时加载 | 禁止 |
| H12 | Winsock 分层服务提供程序（LSP）与命名空间提供程序 | 引导期由网络栈加载进使用 Winsock 的进程 | 禁止 |

> 这一组的共同点：加载在**高权限系统进程**里、写入需要管理员、属于系统级扩展点，并且多数是安全软件默认关注的持久化技术。**H3 与 H6 的后半句是例外**：注册自己的 Shell 扩展与自己的 per-user COM 服务器都是微软文档化的正当做法，只是它们本身不构成“登录自启”，只能让代码在别人进程里按需加载。补充一条边界：**完整性级别高于中等的进程会忽略 per-user COM 配置、只读机器级配置**，所以 `HKCU` 侧的改动影响不到提权进程——这是系统刻意设计的防提权措施。

### 5.9 I 组与 J 组

| 编号 | 落点 | 说明 | 结论 |
| --- | --- | --- | --- |
| I1 | MSIX/UWP `windows.startupTask` | 有包身份才可用 | 打包分发时可用 |
| I2 | 打包桌面应用（完全信任）自启声明 | 同上，需打包；可声明 `Enabled="true"` 免用户同意 | 打包分发时可用 |
| I3 | Windows App SDK `ActivationRegistrationManager.RegisterForStartupActivation` | 面向**未打包** Win32 应用的新 API，但仍标注**实验性**；其注册最终落到 Run 键或启动文件夹，具体落点官方未说明 | 暂不推荐（API 未稳定） |
| J1 | 屏保程序（`SCRNSAVE.EXE`） | 空闲触发，非登录自启；`.scr` 只是普通 PE 文件 | 作自启：禁止；作正常空闲显示：不推荐 |

> 关于屏保的两个事实：`ScreenSaveTimeOut` **自 Windows 7 起默认不存在**——不通过组策略设置超时，系统根本不会自动启动屏保（所以“屏保自启”在实践中通常需要额外配置）；微软也发布过针对恶意屏保的安全公告（主题文件可携带屏保并执行任意代码，此后系统不再自动选用随自定义主题附带的内置之外屏保）。屏保本身**不在 Autoruns 的自启分类里**，把它当作自启点属于滥用而非文档化机制。

## 6. 横向对比矩阵

对 7 种产品级方案，按工程决策最关心的维度比较。

| 维度 | HKCU Run | HKCU RunOnce | 用户启动文件夹 | HKLM Run / 公共启动文件夹 | 登录计划任务 | 引导任务 / 服务 | 打包 StartupTask |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 需要管理员 | 否 | 否 | 否 | 是 | 否 | 是 | 安装时 |
| 能启动需提权的程序 | **否**（被系统阻止） | 否 | **否**（被系统阻止） | 否 | 仅 `HighestAvailable` 且需管理员注册 | 是 | 否 |
| 保存密码 | 否 | 否 | 否 | 否 | 否 | 否（SYSTEM 除外） | 否 |
| 触发时机 | Shell 就绪 | Shell 就绪 | Shell 就绪 | Shell 就绪 | 登录后（可设延迟） | 引导期/服务期 | Shell 就绪 |
| 依赖 Explorer | **是** | **是** | **是** | **是** | **否** | 否 | 是 |
| 安全模式下生效 | 默认否（`*` 前缀可强制） | 同左 | 否 | 否 | **否**（计划任务服务不运行） | 服务：是；引导任务：否 | 未验证 |
| 可设启动延迟 | 否（受 D2 影响） | 否 | 否 | 否 | **是** | 是 | 否 |
| 失败自动重试 | 否 | 否 | 否 | 否 | **是**（重试策略） | **是**（SCM 恢复） | 否 |
| 多实例控制 | 否 | 否 | 否 | 否 | **是**（`IgnoreNew`） | 否 | 否 |
| 不限时运行 | 是 | 是 | 是 | 是 | 需显式 `PT0S` | 是 | 是 |
| 参数/工作目录直传 | 否（单字符串） | 否 | **是**（快捷方式） | 否 | **是** | 是 | 是 |
| 可查询真实状态 | 需配合 D1 | 需配合 D1 | 需配合 D1 | 需配合 D1 | **是**（含路径/参数/身份） | 是 | 是 |
| 启动页可见 | 是 | 是 | 是 | 是 | **否** | 否 | 是 |
| 卸载清理 | 删值 | 自删 | 删文件 | 删值 | 删任务 | 删服务 | 卸载包 |
| 无提示提权风险 | 否 | 否 | 否 | 否 | **是**（`HighestAvailable`） | 是 | 否 |
| 安全软件敏感度 | 低 | 低 | 低 | 低 | 低 | 中 | 低 |
| 实现成本 | 低 | 低 | 中 | 低 | 中高 | 高 | 高（需打包） |

## 7. 工程落地要点（不论选哪种方案都要处理）

1. **状态真相唯一**：不要在自己配置文件里再存一份自启开关。每次查询以系统实际状态为准，否则会出现“配置说开着、系统里已经没了”。
2. **所有权标识**：任务用稳定的 `RegistrationInfo.Source` + 固定任务名；注册表用固定值名。同名冲突时**拒绝修改并报错**，不要覆盖别人的任务或启动项。
3. **幂等启动**：给自启路径传一个显式标记（如 `--autostart`），并配合单实例互斥体。重复自启请求必须安静退出，不激活、不弹窗。
4. **静默与不抢焦点**：登录期不允许弹错误框、不允许抢前台。托盘等依赖 Shell 的服务延迟重试，失败只记录日志。
5. **路径与引号**：绝对路径、必要引号、参数与工作目录分别传递；不要在代码里拼接 `cmd /c` 命令行。计划任务参数是独立字段，不存在拼接问题——这也是选它的理由之一。
6. **尊重用户意图**：优先读取系统“已禁用”状态并如实显示。用户禁用后不要自动改回。不得改写 `StartupApproved` 一类的私有状态来对抗用户。
7. **迁移与清理**：换机制时先建新的、成功后再删旧的；卸载时删除自己的注册项；升级检测到旧路径失效要提示或重注册。
8. **失败可见**：查询失败禁用开关并记日志；写入失败给出可操作提示并回滚到上一个有效状态。
9. **权限一致性**：普通权限自启 + 按需对单个条目提权（如 Shell `runas`），不要为了让个别操作提权而把整个自启改成最高权限。注意 Run 键与启动文件夹属“登录路径”，**需要提权的程序会被系统直接阻止**，所以普通权限不只是偏好，而是这些位置的前提。
10. **可测试性**：注册/查询/禁用/迁移/失败保留/外部禁用/无关任务保护/特殊字符路径等用集成测试覆盖（测试使用唯一任务名与临时注册表键，避免污染真实环境）；**真实注销重登与重启必须单独人工验证**，计划任务“手动运行”不能替代登录触发验证。

## 8. 推荐技术方案

### 8.1 首选：当前用户交互登录计划任务

**适用**：需要登录后立即可用、驻留托盘、与用户会话交互的桌面应用（启动器、便签、输入辅助、常驻工具）。

**方案要点**：

- Task Scheduler 2.0 `LogonTrigger`，`UserId` 为当前用户 SID，`Delay` 为 0 或很短（0–5 秒）。
- `InteractiveToken` + `LeastPrivilege`：不存密码、不要管理员、保持普通权限。
- `Priority = 6`（正常），`ExecutionTimeLimit = PT0S`，`DisallowStartIfOnBatteries = false`、`StopIfGoingOnBatteries = false`，`MultipleInstances = IgnoreNew`。
- 执行文件、参数（`--autostart` 等标记）、工作目录分三项设置。
- **不要用 `Hidden = true`**，不要用 `S4U`，不要用 `HighestAvailable` 换取便利。
- 以“用户主动开启”为默认；托盘或设置中提供开关，并显示真实状态。
- 不要假设“一定早于或晚于 Run 键”——官方无此保证。若必须“外壳就绪后再出现”，用 `Delay`、会话解锁/连接触发，或等 `GetShellWindow()` 一类的就绪信号，而不是硬编码时刻。

**为什么不选 Run 键**：Run 键依赖 Explorer 在登录期枚举，本项目实测过枚举被中断导致未启动，而注册表值与路径均正确；计划任务由计划任务服务独立启动，不受该队列影响，并自带延迟、重试、多实例与状态回读能力。

**必须接受的代价与补偿**：

- 代价：不出现在“任务管理器 → 启动”页，用户不易自行管理；`Priority` 与电池策略默认值对交互式程序不友好，必须显式覆盖；安全模式下同样不启动（这是所有登录自启方案的共同边界）。
- 补偿：程序内提供清晰的开关与状态提示；文档与界面说明任务名（如 `HLaunch.Logon.<SID>`），便于高级用户在“任务计划程序”里找到；检测到任务被外部禁用或删除时如实显示“已关闭”。
- 重复拉起防护：计划任务服务重启会重跑匹配的登录任务，因此“`MultipleInstances = IgnoreNew` + 程序内单实例互斥体”要同时存在，不能只依赖其中之一。

### 8.2 次选：当前用户 Run 键（+ 尊重系统禁用状态）

**适用**：需要出现在系统“启动”页、实现必须极简、或需要最大兼容性的轻量场景。

**取舍**：实现成本与用户可见性占优；可靠性（Explorer 枚举）、延迟控制、重试能力、参数表达能力占劣。若采用，必须：读回并尊重 `StartupApproved` 的禁用状态、处理 260 字符限制、升级后检测路径失效。

**不推荐双写**：计划任务 + Run 键同时注册会造成“启动页显示一个实际不生效的项”，也会让排障出现两个真相源。迁移时用“先建后删”，日常只保留一个机制。

### 8.3 按需求选择（决策树）

1. 需要**无人登录**也运行、或需要守护其他进程？→ Windows 服务 / 引导任务（E3、F1）。否则继续。
2. 目标是**打包/MSIX 分发**，且希望用户能在系统“启动”页管理？→ 打包 StartupTask（I1）。否则继续。
3. 硬需求是“用户必须在**任务管理器 → 启动**里看到并禁用”？→ 当前用户 Run 键或启动文件夹（A1、C1）；参数复杂时优先启动文件夹。否则继续。
4. 其余情况（绝大多数交互式桌面应用）→ **当前用户登录计划任务（E1）**。
5. 只有在需要**顺序、失败重试、错过后补运行**时，才考虑给登录任务增加额外配置；不要为此升级到服务。

### 8.4 明确禁止使用的位置

`Winlogon\Userinit`、`Winlogon\Shell`、`Winlogon\Notify`、`BootExecute`、`AppInit_DLLs`、IFEO `Debugger`、WMI 永久事件订阅、BHO、COM 劫持、LSA 包与密码过滤器、凭据提供程序、时间提供程序、NetSh 帮助程序、打印监视器、Winsock LSP、屏保伪装、`RunServices` 系列。

理由：需要管理员、加载在高权限系统进程中、破坏系统稳定性（写错会无法登录或无法启动）、无用户可管理的入口，且是安全软件默认判定为持久化恶意行为的特征。收益远小于风险。

### 8.5 实现要点（C++ / Win32）

**当前用户 Run 键**

- 用 `RegCreateKeyExW` + `RegSetValueExW` 写入，固定值名即为所有权标识；值类型用 `REG_SZ`，需要环境变量展开时用 `REG_EXPAND_SZ`。
- 写入 `HKLM` 前必须显式提权，不要静默尝试后忽略失败。
- 读回时必须校验值数据仍指向当前 EXE，否则应报告“未启用”而不是显示已勾选。
- `StartupApproved` 只做保守只读：仅识别已知的“已启用”编码，其余长度与内容按未知处理，**不得改写**（它不是公开 API）。

**当前用户登录计划任务**

- `CoCreateInstance(CLSID_TaskScheduler)` → `Connect` → `GetFolder(L"\\")` → `NewTask(0)` → 依次配置 `IRegistrationInfo`（`Source` 作所有权标记）、`IPrincipal`、`ITaskSettings`、`ITriggerCollection`（`TASK_TRIGGER_LOGON` + `ILogonTrigger`）、`IActionCollection`（`TASK_ACTION_EXEC` + `IExecAction`）→ `RegisterTaskDefinition`。
- 任务名带用户 SID（如 `HLaunch.Logon.<SID>`），避免多用户与多安装冲突。
- 查询用 `GetTask`，返回 `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)` 表示“不存在”，这是正常状态而非错误；`RegistrationInfo.Source` 不匹配时必须**拒绝修改**，防止覆盖他人同名任务。
- 查询必须逐项校验：启用状态、目标用户、登录类型、权限级别、执行文件路径、参数。任一不符即视为“未启用”，这样可以识别“任务被禁用”“EXE 已移动”等失效状态。
- `BSTR` 用 WIL 等 RAII 管理，`VARIANT` 的生命周期不要手工泄漏；时间用 ISO 8601 时长字符串（不限时为 `PT0S`），布尔用 `VARIANT_TRUE` / `VARIANT_FALSE`。
- COM 调用放在后台线程，结果通过窗口消息回到 UI 线程后再更新界面；不要在 UI 线程同步等待任务计划服务。
- 禁用与迁移的策略：禁用即删除任务；迁移时**先成功创建任务，再删除旧 Run 值**，创建失败必须保留旧值。

## 9. 与 HLaunch 现状的对应

本节只记录**已有证据**，不构成新决策。

- 已接受的决策：`decisions/ADR-0008-logon-task-startup.md`——当前用户普通权限登录计划任务，登录不设额外等待，不保存密码，不新增服务或辅助程序。
- 实现契约：`windows-integration.md` 的“开机启动”一节（任务名 `HLaunch.Logon.<当前用户 SID>`、`InteractiveToken` + `LeastPrivilege`、`Priority=6`、`PT0S`、`IgnoreNew`、`--autostart` 标记、查询校验来源/启用/用户/权限/触发器/参数/路径、先建后删的旧 Run 迁移、保守处理 `StartupApproved` 记录）。
- 代码：`src/platform/windows/startup_registration.{h,cpp}`（`buildStartupCommand`、`isStartupEnabled`、`setStartupEnabled`、`migrateStartupRegistration`），测试 `tests/startup_registration_tests.cpp`。
- 验证现状：注册、查询、禁用、迁移、失败保留旧值、外部禁用、未知审批格式、无关任务保护由自动测试覆盖；本机手动触发已通过；**真实注销重登与重启触发未验证**（见 `quality.md`）。
- 本文相对 ADR-0008 没有改变任何选择，只是把当时隐含的取舍写全：尤其是“不在任务管理器启动页可见”这一代价，以及“禁止使用注入类位置”的边界。
- 已知边界：该自启在**安全模式下不生效**（计划任务服务不运行），这是所有登录自启方案的共同限制；计划任务服务重启会重跑匹配的登录任务，实现里已由任务 `IgnoreNew` 与进程级单实例互斥体共同兜底，`--autostart` 参数让重复实例安静退出。

## 10. 附录

### 10.1 速查：如何检查一台机器上的自启项

| 位置 | 检查方式 |
| --- | --- |
| Run / RunOnce（两个视图） | 注册表编辑器查看 `HKCU` 与 `HKLM` 的 Run 键，以及 `WOW6432Node` |
| 用户/公共启动文件夹 | `shell:startup`、`shell:common startup` |
| 启动页与审批状态 | 任务管理器 → 启动；`StartupApproved` 键 |
| 计划任务 | `taskschd.msc`，或 `schtasks /query /v /fo LIST` |
| 服务 | `services.msc`，或 `sc query type= service state= all` |
| 综合 | Sysinternals **Autoruns**（见下） |

Autoruns 的 `autorunsc` 命令行开关本身就是一份权威的落点分类（`-a` 参数）：`b` 引导执行镜像、`d` AppInit DLLs、`e` Explorer 扩展、`h` 映像劫持、`i` IE 扩展（含 BHO）、`l` 登录启动项、`m` WMI 条目、`n` Winsock 协议与网络提供程序、`o` 编解码器、`p` 打印监视器、`r` LSA 安全提供程序、`s` 自启服务与驱动、`t` 计划任务、`w` Winlogon 条目、`k` Known DLLs、`g` 桌面小工具。

其中 **编解码器**（`o`）、**Known DLLs**（`k`）与 **AutoPlay/设备处理器**属于“被使用时按需加载”或设备触发，不属于启动或登录触发，因此未计入本文的 53 个落点，但排障时应一并检查。**桌面小工具**（`g`）只存在于 Windows Vista/7，Windows 8 起已移除。

### 10.2 权威资料链接

以下链接均为微软官方文档。成稿时逐条访问验证可用（HTTP 200）；其中攻击面缩减规则一页以本地化同路径页面与规则 GUID 交叉核对。

- [Run and RunOnce Registry Keys](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)：260 字符限制、顺序不确定、`!` 与 `*` 前缀、系统可推迟执行
- [Developing Applications that Run at Logon on Windows Vista](https://learn.microsoft.com/en-us/previous-versions/bb325654(v=msdn.10))：**登录路径阻止需要提权的程序**（启动文件夹与 Run 键四处位置）、拆分普通权限与提权组件的建议
- [AppInit DLLs and Secure Boot](https://learn.microsoft.com/en-us/windows/win32/dlls/secure-boot-and-appinit-dlls)：启用 Secure Boot 时 AppInit_DLLs 被禁用、官方不推荐、Win8 认证禁止
- [Task Scheduler for developers](https://learn.microsoft.com/en-us/windows/win32/taskschd/task-scheduler-start-page)：2.0 与 1.0 的系统版本要求、可用触发器清单
- [TaskSettings.Priority](https://learn.microsoft.com/en-us/windows/win32/taskschd/tasksettings-priority)：优先级 0–10 与进程优先级类的对应关系、默认值 7
- [LogonTrigger object](https://learn.microsoft.com/en-us/windows/win32/taskschd/logontrigger)：`UserId` 语义、服务启动时枚举已登录用户、按组触发
- [Services](https://learn.microsoft.com/en-us/windows/win32/services/services)：服务与驱动服务、SCM 关系、触发启动
- [Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)：Vista 起服务不能直接与用户交互、会话 0、`NoInteractiveServices`
- [Troubleshooting Task Scheduler](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2008-R2-and-2008/cc721846(v=ws.10))：**安全模式下计划任务服务不运行**、任务 XML 位于 `System32\Tasks`
- [[MS-SCMR] SERVICE_DELAYED_AUTO_START_INFO](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-scmr/805b8296-863d-4d1e-8ae8-f639adf8c6cb)：延迟自动启动为“其他自动启动服务之后再加约两分钟”
- [Attack surface reduction rules reference](https://learn.microsoft.com/en-us/defender-endpoint/attack-surface-reduction-rules-reference)：阻止通过 WMI 事件订阅实现持久化的 ASR 规则
- [Per-user services in Windows](https://learn.microsoft.com/en-us/windows/application-management/per-user-services-in-windows)：每用户服务的创建与生命周期
- [StartupTask Class](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.startuptask)：`windows.startupTask` 清单声明、桌面应用与 UWP 差异、`RequestEnableAsync` 不覆盖用户禁用
- [AppInit_DLLs in Windows 7 and Windows Server 2008 R2](https://learn.microsoft.com/en-us/windows/win32/win7appqual/appinit-dlls-in-windows-7-and-windows-server-2008-r2)：`LoadAppInit_DLLs` / `AppInit_DLLs` / `RequireSignedAppInit_DLLs` 与代码签名要求
- [Credential providers in Windows](https://learn.microsoft.com/en-us/windows/win32/secauthn/credential-providers-in-windows)：凭据提供程序在登录界面的加载方式
- [Installing and registering a password filter DLL](https://learn.microsoft.com/en-us/windows/win32/secmgmt/installing-and-registering-a-password-filter-dll)：`Lsa\Notification Packages` 的写入方式与管理员要求
- [Autoruns for Windows](https://learn.microsoft.com/en-us/sysinternals/downloads/autoruns)：全部自启落点的分类与检查工具

> 文档化状态提醒：`StartupApproved`、`Explorer\Serialize\StartupDelayInMSec`、Active Setup、`RunOnceEx`、`RunServices`、`Win.ini` 的 `load`/`run` 等位置**微软未作为公开扩展点文档化**，本文对它们只作现象描述，不作为实现依据。
