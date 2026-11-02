# Varjo 眼动 C API 要点

来源：Varjo SDK for Custom Engines **4.16.0** 官方包内的 `include/Varjo.h` /
`include/Varjo_types.h`，以及 developer.varjo.com 的 Native「Eye tracking」与
「Supported features」文档（一手核对，2026-11）。本模块未复制任何厂商头文件或
二进制，只在 `src/varjo_api.hpp` 手写最小声明。

## 命名与运行时（与常见摘要的差异）

| 摘要常说 | 实际情况（4.x / 3.10+ 官方包核对） |
|---|---|
| 头文件叫 `varjo.h` | 新版是大写开头的 `Varjo.h`（拆成 `Varjo_types.h` 等）；`varjo.h` 是 2.x 时代旧名 |
| 运行时 DLL 叫 `varjo.dll` | 现为 **`VarjoLib.dll`**（官方包 `bin/`、导出表均确认）；`varjo.dll` 只在旧版存在 |
| `varjoCreateGazePlane` | 那是注视平面**渲染** API，本模块不渲染，用不到；取数据用下面的函数 |
| GazeData 全双精度 | `varjo_Gaze` 的向量/距离/稳定性是 `double`；**`varjo_EyeMeasurements` 全是 `float`**（含毫米与睁眼度） |

本模块的加载顺序：`config.ini` 的 `dll_path`（完整路径）→ `VarjoLib.dll`（模块目录/系统
目录/PATH）→ 旧名 `varjo.dll`（仅系统目录/PATH；本模块的 DLL 已改名为 varjo-eyetracking.dll，不再与厂商旧名同名，
必须避开模块目录，否则会把自己当厂商运行时）。

## 用到的函数（签名逐字对照 Varjo.h）

```c
struct varjo_Session* varjo_SessionInit(void);
void                  varjo_SessionShutDown(struct varjo_Session* session);
varjo_Bool            varjo_IsGazeAllowed(struct varjo_Session* session);
void                  varjo_GazeInit(struct varjo_Session* session);
struct varjo_Gaze     varjo_GetGaze(struct varjo_Session* session);          // 按值返回
varjo_Bool            varjo_GetGazeData(struct varjo_Session*,
                            struct varjo_Gaze*, struct varjo_EyeMeasurements*);
```

- 全部 `varjo_*` 符号运行时从 DLL 解析；缺哪个就把名字聚合进错误提示。
- `varjo_GetGazeData` / `varjo_GetGazeDataArray` 的官方注释：**需要 Varjo Base Pro
  授权**。拿不到时退回 `varjo_GetGaze`，只输出注视方向（日志会提示一次）。
- `varjo_GazeInit` 用默认参数：Standard 平滑 + 头显最高频率。Base 4.1 起默认即最高；
  官方口径：眼动输出 **100Hz 或 200Hz**（XR-4 等 200Hz）。
- 官方文档：眼动队列只保留 500ms，需频繁轮询——本模块每个宿主 tick 都取最新帧。

## 结构体与单位

`varjo_Gaze`（字段顺序照抄 `Varjo_types.h`）：

| 字段 | 类型 | 含义/单位 |
|---|---|---|
| `leftEye` / `rightEye` / `gaze` | `varjo_Ray{double origin[3]; double forward[3];}` | 逐眼与合并注视射线；米；**左手系**（X 右、Y 上、Z 前），相对头姿；forward 归一化 |
| `focusDistance` | `double` | 聚焦距离，米，[0,2] |
| `stability` | `double` | 焦点稳定性 [0,1] |
| `captureTime` | `varjo_Nanoseconds`(int64) | Varjo 时钟纳秒 |
| `leftStatus` / `rightStatus` | `varjo_GazeEyeStatus`(int64) | 0 未追踪 / 1 可见不可靠 / 2 质量受损 / 3 正常 |
| `status` | `varjo_GazeStatus`(int64) | 0 不可用（未佩戴） / 1 校准中 / 2 有效 |
| `frameNumber` | `int64_t` | 单调递增帧号 |
| `leftPupilSize` / `rightPupilSize` | `double` | **已弃用**（归一化 [0,1]），本模块不用 |

`varjo_EyeMeasurements`（全部 `float`，除非注明）：`frameNumber`(int64)、`captureTime`(int64)、
`interPupillaryDistanceInMM`、`left/rightPupilIrisDiameterRatio`（[0,1]）、
`left/rightPupilDiameterInMM`（毫米，0=不可用）、`left/rightIrisDiameterInMM`（毫米）、
`left/rightEyeOpenness`（1 全开、0 全闭）。官方标注瞳孔/睁眼度为**实验特性**，无精度保证。

## 本模块的换算与门控

- 方向：只翻 Z（`z = -forward.z`）转成 ABI 的右手系 -Z 前；再防御性归一化并降为 `float`。
- 主状态 ≠ Valid：不写任何注视/合并/辐辏位；逐眼状态 < Visible：只砍该眼的方向位。
- 睁眼度/瞳孔只在 `varjo_GetGazeData` 成功时写；瞳孔限幅 (0.5, 12] mm 防异常值；
  `pupil_dilation` 不写（Varjo 只给直径/比例，没有相对基线的 -1..1 口径）。
- 合并置信度 = `stability`；逐眼置信度 = 状态映射（Tracked 0.95 / Compensated 0.70 /
  Visible 0.40 / 其余 0.25）。
- 辐辏 = `focusDistance`（米，限 (0, 2]）。
- 全部 NaN/Inf 在进入帧槽前清洗；退化向量/非法值 → 对应属性位保持不置。

## 状态与合规

- 眼动必须在 Varjo Base 中允许（`varjo_IsGazeAllowed`），并要求完成校准。
- 厂商 DLL 一律不打包、不链接、不提交；头文件声明为手写最小原型（见 NOTICE）。
