# JoyCon2 Connector — 项目分析说明（供后续 Agent 查询）

> 本文件由代码分析生成，描述仓库结构、各文件职责、构建方式与已加入的调试日志机制。
> 修改代码前建议先阅读本文件与 README.md / README_CN.md。

## 1. 项目概览

- **名称**：JoyCon2 Connector
- **平台**：仅 Windows x64
- **类型**：C++20 GUI 桌面应用（Win32 + DirectX 11 + Dear ImGui）
- **功能**：通过 BLE 连接 Nintendo Switch 2 系列手柄（Joy-Con 2、Pro 2、NSO GC），并转换为虚拟 DS4 或 Xbox 360 手柄输出给 PC 游戏/模拟器。额外支持右 Joy-Con 2 光学鼠标、Pro 2 GL/GR 背键映射、体感输出、中英文界面、系统托盘、自动更新检查。
- **核心依赖**：
  - [ViGEmBus 驱动](https://github.com/ViGEm/ViGEmBus/releases/latest)（运行时必须安装）
  - Microsoft Visual C++ Redistributable 2015–2022 x64
  - Visual Studio 2022 + Windows 10/11 SDK + MSVC（构建）
  - 仓库自带：Dear ImGui（`src/imgui/`）、ViGEm 头文件（`include/ViGEm/`）、`ViGEmClient.lib`

## 2. 总体架构与数据流

```
蓝牙广播
  │  DeviceManager（BLE 扫描，WinRT BluetoothLEAdvertisementWatcher）
  ▼
ConnectedJoyCon { device, inputChar, writeChar, bleAddress }
  │  PlayerManager 注册 ValueChanged 通知
  ▼
GattValueChangedEventArgs → 原始输入字节
  │  JoyConDecoder（解析按钮/摇杆/鼠标/体感，生成 DS4_REPORT_EX 或 XUSB_REPORT）
  ▼
ViGEmManager（ViGEmBus 客户端）→ 虚拟 DS4 / Xbox 360 手柄
```

1. `WinMain`（App.cpp）创建无边框 DPI-aware 窗口、初始化 D3D11/ImGui。
2. `DeviceManager` 在后台线程扫描任天堂厂商数据（manufacturer ID `1363`，前缀 `01 00 03 7E`），发现设备后枚举 GATT 服务/特征。
3. `PlayerManager` 根据用户选择创建 Single / Dual / Pro / NSO-GC 播放器，订阅输入特征通知，并在 BLE 回调中解码输入、写入 ViGEm 目标。
4. 右 Joy-Con 鼠标模式：CHAT 键循环切换关闭/快/中/慢；使用 `GetRawOpticalMouse` 读取位移，通过 `SendInput` 移动光标，插值线程以配置频率（100–500 Hz）平滑输出。
5. 设置保存在相对工作目录的 `joycon2_config.json`；语言 JSON 在构建时由 CMake 嵌入二进制。
6. `UpdateChecker` 使用 WinRT `Windows.Web.Http` 查询 GitHub Releases 最新版本。

## 3. 仓库根目录文件

| 文件/目录 | 作用 |
|---|---|
| `README.md` / `README_CN.md` | 英文/中文项目说明、依赖、用法、构建步骤、BLE 协议表 |
| `LICENSE` | 许可证 |
| `.gitignore` | 忽略 `build/`、`joycon2_connector/build/`、`.claude/`、`.vscode/`、`docs/` 及三个 Lua 抓包脚本 |
| `common.lua` | Wireshark 共用 Lua 工具函数：hex 转换、缓冲区字节串、bit 测试 |
| `command_handler.lua` | Wireshark Switch 2 **命令协议**解析器（USB/ble/handle 注册），解析 SPI、MCU、振动、配对、固件、灯效等命令/回复 |
| `input_handler.lua` | Wireshark Switch 2 **输入报告**解析器（HID/ble），解析左右 Joy-Con、Pro、GC 输入报告及 IMU |
| `agent.md` | 本文件，供后续 Agent 查询 |

> Lua 三个脚本不参与 C++ 构建，是独立 Wireshark 抓包分析工具，且当前被 .gitignore 忽略。

## 4. joycon2_connector 构建相关

| 文件/目录 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层构建：C++20、MSVC `/Zc:char8_t-`、静态 CRT、生成 `version.h` 与嵌入式语言头 `lang_data.h`；源文件含 `App.cpp`、`JoyConDecoder.cpp`、**`Logger.cpp`**；链接 `setupapi hid ViGEmClient windowsapp d3d11 dxgi d3dcompiler dwmapi`；构建后复制 manifest |
| `cmake/embed_languages.cmake` | 扫描 `languages/*.json`，转义后生成 `build/generated/lang_data.h`（`GetEmbeddedLanguages()`） |
| `include/ViGEm/` | ViGEmBus C API 头文件（`Client.h`、`Common.h` 等） |
| `lib/ViGEmClient.lib` | ViGEmBus 客户端静态库 |
| `languages/en_us.json`、`zh_cn.json` | 中英文 UI 翻译文本，包含 `_locale` 与 `_display_name` 元数据 |
| `resources/app.ico` | 应用图标 |
| `resources/app.manifest` | DPI-aware（PerMonitorV2）+ Win10/11 兼容 manifest |
| `resources/app.rc` | 资源脚本：图标 `101` + 嵌入 manifest |
| `src/version.h.in` | CMake 版本模板，生成 `APP_VERSION` 宏 |
| `build/` | 本地 CMake/VS 构建产物，已被 gitignore，不应修改 |
| `build_check/` | 本地 CMake/VS 构建产物（当前 .gitignore 未覆盖，属于未跟踪本地目录，不应提交） |

### 构建命令

```sh
cd joycon2_connector
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# 产物：build/Release/joycon2_connector.exe
```

## 5. 核心源码文件职责

### `src/App.cpp`
程序入口与窗口/渲染主循环。
- 无边框 `WS_POPUP | WS_THICKFRAME` 窗口，自定义标题栏（最小化/最大化/关闭）。
- D3D11 设备与交换链创建/清理、ImGui 初始化、字体加载（YaHei → Segoe UI + CJK → 系统 CJK → 内置，支持 DPI 重建）。
- 侧边栏导航（仪表盘 / 添加设备 / 背键布局 / 鼠标设置 / 设置）。
- 系统托盘（左键双击恢复、右键菜单显示/断开/退出），`WM_CLOSE` 按配置最小化到托盘。
- `WinMain` 中调用 `Logger::Init()` 并 `std::set_terminate(Logger::OnTerminate)`；随后初始化 ViGEm、加载配置/语言、进入主循环。
- `WndProc` 处理 `WM_SIZE`、`WM_DPICHANGED`、`WM_NCHITTEST`（无边框拖动/缩放）、`WM_GETMINMAXINFO` 等。

### `src/BluetoothLog.h`
WinRT 蓝牙/GATT 错误码转人类可读文本的辅助函数：
- `DescribeBluetoothError`：`BluetoothError` 枚举含义（蓝牙关闭、资源占用、策略禁用等）。
- `DescribeGattStatus`：`GattCommunicationStatus` + ATT `ProtocolError` 详情。
- `DescribeHResultError`：WinRT 异常的 HRESULT 与错误消息。
- `GuidToString`、连接状态/连接参数请求状态描述等。

### `src/BLECommands.h`
Joy-Con/Pro 手柄 BLE 命令发送工具（内联函数）：
- `SendGenericCommand`：标准 0x91 子命令帧，写后延时 35ms。
- `SendCustomCommands`：连接后的 0x0c 初始化命令组。
- `EmitSound`、`SetPlayerLEDs`、`SendVibrationSample`。
- `SendRawVibration`：0x5N 原始振动帧（16 字节）。
- `EncodeVibrationPayload`：将 DS4/X360 马达值编码为 12 字节振动负载（协议未完全逆向）。
- `*Async` 版本：从 ViGEm 回调线程内非阻塞发送。

### `src/ConfigManager.h`
JSON 配置持久化与默认值：
- 结构：`GLGRLayout`、`ProControllerConfig`、`MouseConfig`、`VibrationConfig`、`DeviceSettings`（按 BLE 地址保存 `swapABXY / useRawVibration / useXboxEmulation`）、`AppConfig`。
- 手写极简 JSON 序列化/反序列化（`ConfigToJSON`、`JSONToConfig`、`ExtractJson*`）。
- 单例 `ConfigManager`：`Load()`/`Save()`/`EnsureDefaults()`/`GetDeviceSettings(addr)`；配置文件为相对工作目录的 `joycon2_config.json`。
- 注意：`configFile` 是**工作目录相对路径**，不是 exe 目录。

### `src/DeviceManager.h`
异步 BLE 扫描器（替换原阻塞扫描）：
- 定义 `ConnectedJoyCon`、`ScanState { Idle, Scanning, Found, Error, Timeout }`。
- 常量：任天堂 manufacturer ID `1363`、厂商数据前缀、输入/写入特征 UUID。
- `StartScan`：后台线程运行 `RunScan`；用 `BluetoothLEAdvertisementWatcher` 过滤广播，匹配后 `FromBluetoothAddressAsync`、`GetGattServicesAsync`、`GetCharacteristicsAsync` 获取 `inputChar` 和 `writeChar`；请求最短连接间隔；超时 30s。
- `StopScan` 通过 `cancelScan` 原子标志取消；线程 detach 以免阻塞 UI。

### `src/JoyConDecoder.h` / `.cpp`
输入解码器（纯函数，UI/网络无关）：
- `JoyConSide`（左/右）、`JoyConOrientation`（竖握/横握）、`GyroSource`（左/右/双）。
- `DecodeJoystick`：12 位摇杆、死区 0.08、1.7 倍增益、侧握旋转。
- `GenerateDS4Report` / `GenerateDualJoyConDS4Report` / `GenerateProControllerReport` / `GenerateNSOGCReport`：生成 `DS4_REPORT_EX`。
- `GenerateXUSBReport` 及 Dual/Pro/GC 对应版本：生成 `XUSB_REPORT`。
- `GetRawOpticalMouse`、`DecodeMouseCoords`（触控板坐标）、`DecodeMotion`。
- `ApplyABXYSwap` / `ApplyABXYSwapXUSB`：A⇄B、X⇄Y 交换。
- 关键缓冲区偏移：按钮左 4/右 3；摇杆左 10/右 13；鼠标 `0x10/0x12`；IMU `0x30–0x3B`；GC 模拟扳机 `0x3C/0x3D`；多数函数要求 `buffer.size() >= 0x3C`（GC 0x3E）。

### `src/PlayerManager.h`
核心手柄管理单例（最大文件，约 1160 行）：
- `ControllerType { SingleJoyCon, DualJoyCon, ProController, NSOGCController }`。
- 玩家结构：`SingleJoyConPlayer`、`DualJoyConPlayer`、`ProControllerPlayer`；保存 BLE 特征、ViGEm target、鼠标/振动/设置状态。
- ViGEm 振动回调 `DS4VibrationCallback` / `X360VibrationCallback`：节流 50ms，按强度缩放，支持 raw（0x5N）或预置采样（0x0A）双模式。
- 背键工具：`ApplyButtonMapping` / `ApplyGLGRMappings`（DS4 与 XUSB）。
- `HandleSpecialProButtons`：截图键→F12；ZL+ZR+GL+GR 打开布局管理；C 键循环布局。
- `AddSingleJoyCon`：分配 ViGEm 目标、订阅通知、CHAT 切换鼠标模式、插值/直接 SendInput、滚轮、侧键。
- `AddDualJoyConFirstStep/SecondStep`：两步扫描，合并左右输入到一条 DS4/X360 报告，独立 update 线程。
- `AddProOrGC`：Pro 2 / NSO GC 订阅与映射。
- `RemovePlayerByGlobalIndex`、`Shutdown`：按全局索引移除、反注册通知、释放目标；`StartMouseInterpolThread` 共享插值线程。

### `src/ViGEmManager.h`
ViGEmBus 客户端单例封装：`vigem_alloc/connect`、分配 DS4/X360 target、`AddTarget/RemoveTarget`、`Shutdown`；仪表盘状态指示依赖 `IsConnected()`。

### `src/UI_Pages.h`
ImGui 页面渲染（全部 inline）：
- `RenderDashboard`：ViGEm 状态、玩家卡片、单/双/Pro 设置弹窗、Xbox 映射警告弹窗。
- `RenderAddDevice`：5 步向导（选择类型→配置左右/握法/体感源→扫描→双 Joy-Con 右成功→扫描左）。
- `RenderLayoutManager`：背键布局增删改、GL/GR 下拉映射、激活布局。
- `RenderMouseSettings`：鼠标启用、三档灵敏度、滚轮速度、插值开关/频率。
- `RenderSettings`：语言切换、最小化托盘、更新检查、关于/GitHub、更新弹窗。
- 通用控件：`BeginCard/EndCard`、`Primary/Secondary/DangerButton`、`StatusChip`、`Spinner`、`DrawControllerIcon` 等。

### `src/UI_Theme.h`
Material Design 3 暖色主题与 DPI 缩放：`UITheme::DpiScale`、`S()` 缩放函数、色板常量、`Apply()` 设置 ImGui 样式。

### `src/UpdateChecker.h`
GitHub Releases 异步更新检查：`CheckForUpdate()`（手动，UI 显示状态）与 `CheckForUpdateSilent()`（启动自动检查，仅发现新版弹窗），10s 超时，比较 `MAJOR.MINOR.PATCH` 版本。

### `src/i18n.h`
嵌入式 JSON 国际化：
- `I18nManager` 从生成的 `lang_data.h` 加载语言，手写 JSON 解析（含 `\uXXXX`）。
- `DetectSystemLanguage()` 返回 `zh_cn`/`en_us`。
- 全局 `T(key)` 供 UI 取翻译文本。

### `src/app_icon.h`
嵌入的图标像素数据（`icon_width/icon_height/icon_pixels`），用于 ImGui 标题栏图标纹理。

### `src/imgui/`
Dear ImGui 上游库（v1.x，含 DX11/Win32 后端）。属于 vendored 第三方代码，一般不需要改。

## 6. 调试日志功能（--debug）

日志实现文件：
- `src/Logger.h`：接口 + 日志宏。
- `src/Logger.cpp`：实现（新增，已加入 CMake `APP_SOURCES`）。

### 行为

- **仅**在启动参数包含 `--debug`（不区分大小写）时启用，例如：
  ```sh
  joycon2_connector.exe --debug
  ```
- 日志文件输出到**可执行文件所在目录**：
  ```
  <exe所在目录>\joycon2_connector_debug.log
  ```
  而不是当前工作目录。
- 文件以追加（append）方式打开，每次写日志后立即 `FlushFileBuffers`，降低崩溃时丢失日志的概率。
- 日志格式：
  ```
  [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [PID:TID] message
  ```
  LEVEL 为 `DEBUG / INFO / WARN / ERROR`。
- 提供宏（未启用 `--debug` 时开销极低）：
  ```cpp
  APP_LOG_DEBUG(...)
  APP_LOG_INFO(...)
  APP_LOG_WARNING(...)
  APP_LOG_ERROR(...)
  ```
  参数格式与 `printf` 相同，支持 `%s`（窄字符串）和 `%S`（宽字符串，MSVC）。
- 崩溃捕获：
  - `Logger::Init()` 在调试模式开启时调用 `SetUnhandledExceptionFilter`；捕获 Win32 结构化异常（访问冲突等），记录异常码、标志、地址、异常参数及 x64/x86 关键寄存器后返回 `EXCEPTION_EXECUTE_HANDLER`。
  - `App.cpp` 调用 `std::set_terminate(Logger::OnTerminate)`，未捕获 C++ 异常会记录异常信息后 `abort()`。
  - 崩溃路径使用 `TryEnterCriticalSection`，避免与日志锁死锁。
- 正常退出时 `Logger::Shutdown()` 会刷新日志文件；为避免与仍在运行的后台线程竞争，文件句柄由操作系统在进程退出时回收。

### 已埋点位置（示例）

启动/窗口/D3D 生命周期、ViGEm 初始化与错误、配置加载保存、语言选择、更新检查、BLE 扫描各阶段、玩家添加/移除/关闭、鼠标模式切换、DPI 变化、添加设备向导结果等。

蓝牙扫描/连接阶段会额外记录：
- 广播 RSSI、匹配到的厂商数据。
- `BluetoothLEAdvertisementWatcher` 停止原因（`BluetoothError`）。
- `FromBluetoothAddressAsync` / `GetGattServicesAsync` / `GetCharacteristicsAsync` 抛出的 HRESULT 及错误消息。
- `GattCommunicationStatus` 与 ATT `ProtocolError`（含常见 ATT 错误码含义）。
- 发现的 GATT 服务/特征 UUID、输入/写入特征缺失情况。
- 输入通知（CCCD）订阅结果或异常、BLE 命令写入失败状态。
- 设备连接状态变化、连接参数请求状态。

### 注意事项

- 日志文件路径含非 ASCII 字符也能正确打开（使用宽字符 `CreateFileW`）。
- 若 exe 所在目录不可写（如 Program Files），日志无法创建且当前实现不降级到其他目录。
- 配置 `joycon2_config.json` 仍写在**当前工作目录**；这与日志位置不同，是既有行为。

## 7. 修改代码时的高风险点

1. **WinRT apartment**：`WinMain` 先 `Logger::Init()` 再 `winrt::init_apartment()`；DeviceManager/UpdateChecker 等 WinRT 对象都在初始化后使用。
2. **多线程**：BLE `ValueChanged` 回调线程、DeviceManager 扫描线程、双 Joy-Con merge 线程、鼠标插值线程、ViGEm 振动回调线程都并发运行；日志已做临界区保护。修改 PlayerManager 时注意原子变量（`std::atomic`）与生命周期。
3. **指针稳定性**：`SingleJoyConPlayer` 存入 `std::vector<std::unique_ptr<...>>` 后把原始指针捕获进 BLE 回调；增删玩家会使 vector 元素移动但对象地址稳定（unique_ptr 的对象是堆分配），不要改成直接 `vector<SingleJoyConPlayer>`。
4. **缓冲区边界**：JoyConDecoder 所有函数必须检查 `buffer.size()`，报告最小 0x3C（GC 0x3E），否则异常崩溃。
5. **viGEm 目标生命周期**：移除玩家前先 `unregister_notification` 再 `RemoveTarget`，否则回调可能访问已释放的 `VibrationContext`。
6. **CMake 静态 CRT**：`CMAKE_MSVC_RUNTIME_LIBRARY` 设为 MultiThreaded(/D)，发布 exe 仍建议安装 VC++ Redistributable 仅作为兼容兜底。
7. **资源生成**：修改 `languages/*.json` 后需重新生成/构建，CMake 自定义目标会自动触发 `embed_languages.cmake`。
