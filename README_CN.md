[English Documentation](./README.md)

---

# JoyCon2 Connector

> [joycon2cpp](https://github.com/TheFrano/joycon2cpp) 的  Fork 版本 —— 重写了完整的 GUI 界面，优化光学鼠标支持，让 Switch 2 手柄在  PC 上直接可用。
> 
> ![Dashboard](https://s41.ax1x.com/2026/03/01/peSqb0P.png)    
*点个 star 谢谢喵，爱你喵*
---

## 功能简介

- **全新 GUI 界面** —— 替换了原有的命令行交互方式，提供简洁现代的图形界面。
- **优化鼠标模式** —— 支持右 Joy-Con 2 的光学传感器作为 PC 鼠标使用，按下 **CHAT 键**切换鼠标模式（高 / 中 / 低灵敏度 + 关闭），可在鼠标设置页面调整灵敏度与滚轮速度，添加插值输入，使鼠标移动更平滑。
- **GL/GR 背键映射** —— 支持为 Pro Controller 2 的背键设置自定义映射，可创建多个命名布局并在游戏中随时切换
- **多语言支持** —— 界面支持中文与英文切换（左下角语言按钮）
- **多手柄支持**——支持 Joy-Con 2 、Pro 2 手柄 与 NSO GC 手柄。
- **体感支持**——支持模拟体感，供支持的游戏 / 模拟器使用。

---

## 截图

| 仪表盘                                                 | 添加设备                                                 |
| --------------------------------------------------------- | ---------------------------------------------------------- |
| ![Dashboard](https://s41.ax1x.com/2026/03/01/peSqb0P.png) | ![Add Device](https://s41.ax1x.com/2026/03/01/peSqIld.png) |

| 背键映射                                           | 鼠标设置                                               |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| ![Back Button Layout](https://s41.ax1x.com/2026/03/01/peSq5SH.png) | ![Mouse Settings](https://s41.ax1x.com/2026/03/01/peSqqTf.png) |

---

## 免责声明

本项目**仅支持 Windows**。虚拟手柄输出依赖 `ViGEmBus` 驱动，该驱动仅适用于 Windows 平台。如有需要，欢迎自行 Fork 适配 macOS / Linux，本项目与 Nintendo 无任何关系，仅供学习与交流使用。

---

## 运行依赖

运行前请确保已安装以下内容：

- [ViGEmBus 驱动](https://github.com/ViGEm/ViGEmBus/releases/latest)
- [Microsoft Visual C++ Redistributable 2015–2022 (x64)](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist)

---

## 使用方法

1. 从 [Releases](../../releases) 页面下载并运行程序（或自行从源码构建）。
2. 在侧边栏点击**添加设备**，选择手柄类型：
   - 单 Joy-Con
   - 双 Joy-Con（L + R 配对）
   - Pro 手柄
   - NSO GameCube 手柄
3. 按照界面提示操作：单 Joy-Con 需选择左右，双 Joy-Con 需逐一配对。
4. 连接成功后，手柄将显示在**仪表盘**中，以虚拟 DS4 手柄的形式供 PC 游戏使用。

### 鼠标模式（仅限右 Joy-Con 2）

右 Joy-Con 2 内置的光学传感器可作为 PC 鼠标使用。按下 **CHAT 键**循环切换三档鼠标模式（高 / 中 / 低灵敏度）或关闭。可在**鼠标设置**页面调节各档灵敏度和滚轮速度。

### 背键布局（Pro Controller 2）

在**背键布局**页面可为 GL 和 GR 键分配任意按键。支持创建多个命名布局，游戏中按 **C 键**可快速切换布局，按 **ZL + ZR + GL + GR** 可呼出布局管理界面。

---

## 从源码构建

### 环境要求

通过 **Visual Studio Installer** 安装以下组件：

- Visual Studio 2022 或更新版本
- 工作负载：**使用 C++ 的桌面开发**
- 组件：**Windows 10 或 11 SDK**
- 组件：**MSVC v14.x**

### 构建步骤

1. 打开对应 VS 版本的 **x64 Native Tools 命令提示符**。

2. 进入 `joycon2_connector` 目录并创建构建文件夹：
   ```sh
   cd joycon2_connector
   mkdir build
   cd build
   ```

3. 生成 Visual Studio 项目文件：
   ```sh
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```

4. 以 Release 模式构建：
   ```sh
   cmake --build . --config Release
   ```

5. 编译完成后，可执行文件位于：
   ```
   build\Release\joycon2_connector.exe
   ```

---

## 常见问题

**连接错误/无法连接：**
请确保手柄是关机状态，开始扫描后再按住配对键让它开机，连接成功后再松开配对键。

**手柄多次尝试连接后无响应：**
这是手柄本身的冷却行为，并非系统或蓝牙驱动问题。等待几分钟后重试即可自动恢复。

**ViGEm 未检测到：**
请在*启动程序前*确认 ViGEmBus 驱动已正确安装。仪表盘顶部的状态指示灯为绿色即表示连接正常。

**左右 Joy-Con 的按键布局不同**：
设置时请务必正确选择左/右侧。

---

## 致谢

- 原始项目：[joycon2cpp](https://github.com/TheFrano/joycon2cpp)
- BLE 协议研究：[@german77](https://github.com/german77)
- 虚拟手柄输出：[ViGEmBus](https://github.com/ViGEm/ViGEmBus)
