# FGB VarjoModule

[![build](https://github.com/Aqamoe/FGB-VarjoModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-VarjoModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。API 声明、映射与 ABI 生命周期有单元测试；
符号解析与「无运行时优雅失败」有冒烟程序，但 CI 上没有 Varjo Base 与头显，
真机（Varjo XR/VR 系列）验收待补，见 [docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 Varjo 眼追原生模块。
运行时加载 Varjo 官方 C API（`varjo_GazeInit` / `varjo_GetGazeData`，100/200Hz），
读取 Varjo 头显的眼动数据，输出双眼/合并注视方向、辐辏距离、睁眼度与瞳孔直径。
不链接、不打包厂商 DLL，不需要 VRCFT，不注入任何进程，也不需要管理员权限。

Varjo eye-tracking native module for FaceGaze Bridge. Loads the official Varjo
C API at runtime and reads 100/200 Hz gaze data from a Varjo headset: per-eye
and combined gaze rays, vergence distance, eye openness and pupil diameter.
No vendor DLLs linked or packaged, no VRCFT, no process injection, no admin rights.

## 功能 / Features

- **注视**：逐眼与合并注视方向（Varjo 左手系翻 Z 转成 ABI 的右手系 -Z 前，
  单位向量、双精度→float），置信度按厂商逐眼状态映射；合并置信度用 `stability`。
- **辐辏**：`focusDistance`（米，0..2）直接写入 `vergence_distance_m`。
- **眼睛**：睁眼度 `left/rightEyeOpenness`（1 全开 / 0 全闭）与瞳孔直径（毫米）
  透传；瞳孔为 0（不可用）或异常越界时不置属性位。
- **优雅降级**：未装 Varjo Base → 卡片提示「请先安装 Varjo Base」；无 Pro 授权 →
  只输出注视方向并记一次日志；摘头显 → 2 秒内报断流，戴上自动恢复。
- **可诊断**：缺失符号逐个列名；NaN/Inf 全部清洗，绝不让无效数据置属性位。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 表情 | Varjo 眼动不含嘴部/面部系数 |
| 2D 注视点 | 厂商给的是 3D 射线（相对头姿），标定到显示视口需要渲染上下文，本模块不做 |
| 瞳孔散动 dilation | Varjo 只给直径/比例，没有相对基线的 -1..1 口径 |
| 头姿 | `varjo_WaitSync`/`FrameInfo` 属渲染 API，本模块不创建交换链，不接头姿 |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 硬件 | 带眼动追踪的 Varjo 头显（XR-3/XR-4、VR-3/VR-4、Aero 等） |
| 运行时 | Varjo Base（需在「设置 → 眼动」中允许眼动并完成校准） |
| 可选 | 睁眼度/瞳孔需 **Varjo Base Pro** 授权（厂商侧实验特性，无精度保证） |
| 其它 | 关闭 VRCFaceTracking，避免与其 Varjo 模块争用眼动数据 |

## 安装

1. 从 Releases 下载 `varjo-eyetracking-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. 「模块」页把 **Varjo Eye Tracking** 设为启动，角色通常给 `eyes`（需要注视通道时加 `gaze`）。
4. 佩戴头显并重启引擎；模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
dll_path = C:\某个\目录\VarjoLib.dll
```

默认按 `VarjoLib.dll` → 旧名 `varjo.dll` 的顺序在系统目录/PATH 中查找厂商运行时；
只有自动查找失败时才需要写 `dll_path`（完整路径；Varjo Base 安装目录或官方
SDK 包 `varjo-sdk\bin\` 里的 `VarjoLib.dll` 都可以）。改完重启引擎。
未知键会被记录一条警告，不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（审计脚本用）。
**不需要 Varjo SDK**：仓库不含厂商头文件，构建时不链接厂商库。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="D:/code/FGB-Module/FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/varjo-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 映射与符号聚合单测（无需厂商环境）
cmake --build build --config Release --target smoke-test        # 真实 ABI 冒烟（需装了 Varjo Base 的机器）
```

CI 不跑冒烟：GitHub runner 上没有 Varjo Base 与头显，见 `.github/workflows/ci.yml` 注释。

## 目录结构

```
src/main.cpp                ABI 生命周期、轮询、断流与状态上报
src/varjo_api.hpp           手写最小 Varjo C API 声明（依据 SDK 4.16.0，不含厂商代码）
src/varjo_runtime.*         厂商 DLL 动态加载、符号聚合、假解析器可测
src/varjo_mapping.*         采样→帧槽：坐标系/单位/置信度/NaN 清洗
module/                     fgb-native.json 与图标
tests/                      映射单测、符号与断流单测、真实 ABI 冒烟
docs/protocol.md            API 依据与换算口径
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「请先安装 Varjo Base」 | 安装并启动 Varjo Base；或把厂商 `VarjoLib.dll` 的完整路径写进 `config.ini` 的 `dll_path` |
| 卡片显示「找不到必需的 Varjo 符号」 | 命中的 DLL 可能不是 Varjo 运行时或版本过旧，升级 Varjo Base |
| 「Varjo Base 未允许使用眼动数据」 | Varjo Base「设置 → 眼动」中启用眼动追踪并完成校准 |
| 一直显示「还没输出有效眼动数据」 | 确认已佩戴头显、双眼可见、没有被其它应用（如 VRCFT）独占眼动 |
| 睁眼度/瞳孔没有 | `varjo_GetGazeData` 需要 Varjo Base Pro 授权；无授权时只输出注视方向（日志已提示） |
| 方向左右/上下反了 | 属未验证风险点：按 docs/verification.md 记录型号与版本后提 issue |

## 致谢 / Attribution

- API 事实依据官方 [Varjo SDK for Custom Engines](https://developer.varjo.com)
  （4.16.0 包内 `Varjo.h`/`Varjo_types.h`）与 developer.varjo.com 文档；
  本仓库不含厂商头文件或二进制，声明为手写最小原型。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
