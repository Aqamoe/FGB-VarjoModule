# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- 运行时动态加载 Varjo 厂商 C API（`VarjoLib.dll`，回退旧名 `varjo.dll`）：
  `varjo_GazeInit` + `varjo_GetGazeData` / `varjo_GetGaze`，100/200Hz 眼动数据。
- 双眼/合并注视方向（左手系翻 Z → 右手系 -Z 前）、辐辏距离、睁眼度与瞳孔直径
  → FGB eyes/gaze 通道；逐眼状态门控属性位，NaN/Inf 全部清洗。
- 未安装 Varjo Base 时优雅失败并提示「请先安装 Varjo Base」；缺失符号逐个列名；
  无 Varjo Base Pro 时降级为只输出注视方向；摘头显 2 秒报断流、恢复自动回到活跃。
- 手写最小 API 声明（依据官方 SDK 4.16.0），仓库不含厂商头文件与二进制。
- 映射单测 + 符号聚合/断流单测（假解析器驱动，CI 无需 Varjo SDK/硬件）与真实 ABI 冒烟。

### 已知限制
- 未在真机（Varjo 头显 + Varjo Base）验证，状态为未验证。
- 睁眼度/瞳孔为厂商实验特性且需 Varjo Base Pro 授权，无精度保证。
