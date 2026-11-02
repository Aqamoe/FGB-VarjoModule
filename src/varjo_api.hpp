#pragma once
// 本模块用到的最小 Varjo C API 声明（手写，不复制厂商头）。
//
// 为什么不直接带 varjo.h：Varjo SDK 的再分发受 Varjo SDK 许可限制，仓库里不得
// 提交厂商头文件与 DLL（见 NOTICE）。这里只声明本模块真正调用的少数符号，
// 全部通过运行时 GetProcAddress（Kit 的 library.hpp）取得函数指针，不链接、
// 不打包任何厂商库。
//
// 声明依据（一手核对，2026-11）：Varjo SDK for Custom Engines 4.16.0
// 官方包内的 include/Varjo.h 与 include/Varjo_types.h。要点：
//   * 新版 SDK 的头文件是大写开头的 Varjo.h（历史 2.x 时代才叫 varjo.h），
//     运行时 DLL 是 VarjoLib.dll（3.10/4.16 官方包均如此，2.x 时代为 varjo.dll）。
//   * 结构体字段名、顺序、类型逐一对照上述头文件抄录，禁止“看起来差不多”。
//   * varjo_GetGazeData / varjo_GetGazeDataArray 官方注释：需要 Varjo Base Pro
//     授权；睁眼度与瞳孔各项在文档中标注为 experimental（无精度保证）。

#include <cstdint>

namespace varjo {

// ---- 标量类型（Varjo_types.h）----
using Bool = std::int32_t;          // typedef int32_t varjo_Bool;
using Nanoseconds = std::int64_t;   // typedef int64_t varjo_Nanoseconds;
using Error = std::int64_t;         // typedef int64_t varjo_Error;

inline constexpr Bool kFalse = 0;
inline constexpr Bool kTrue = 1;

// varjo_GazeStatus：int64_t 别名 + static const 取值。
inline constexpr std::int64_t kGazeStatusInvalid = 0;    //!< 数据不可用：未佩戴或找不到眼睛
inline constexpr std::int64_t kGazeStatusAdjust = 1;     //!< 佩戴中，但眼动正在校准/调整
inline constexpr std::int64_t kGazeStatusValid = 2;      //!< 数据有效

// varjo_GazeEyeStatus：逐眼状态。
inline constexpr std::int64_t kGazeEyeStatusInvalid = 0;      //!< 未追踪（不可见或闭合）
inline constexpr std::int64_t kGazeEyeStatusVisible = 1;      //!< 可见但不可靠追踪（扫视/瞬目）
inline constexpr std::int64_t kGazeEyeStatusCompensated = 2;  //!< 已追踪但质量受损（头显移动）
inline constexpr std::int64_t kGazeEyeStatusTracked = 3;      //!< 正常追踪

// ---- 结构体（字段顺序与 Varjo_types.h 一致）----

// struct varjo_Ray：origin 米、forward 归一化方向；左手系（X 右、Y 上、Z 前），
// 相对头姿。
struct Ray {
    double origin[3];
    double forward[3];
};

// struct varjo_Gaze
struct Gaze {
    Ray leftEye;
    Ray rightEye;
    Ray gaze;
    double focusDistance;  // 注视聚焦距离，米，[0,2]
    double stability;      // 焦点稳定性 [0,1]
    Nanoseconds captureTime;
    std::int64_t leftStatus;   // varjo_GazeEyeStatus
    std::int64_t rightStatus;  // varjo_GazeEyeStatus
    std::int64_t status;       // varjo_GazeStatus
    std::int64_t frameNumber;
    double leftPupilSize;   // 已弃用（归一化 [0,1]）
    double rightPupilSize;  // 已弃用（归一化 [0,1]）
};

// struct varjo_EyeMeasurements：注意这些是 float（不是双精度），
// 睁眼度/瞳孔等由 varjo_GetGazeData 一并给出，0 表示不可用。
struct EyeMeasurements {
    std::int64_t frameNumber;
    Nanoseconds captureTime;
    float interPupillaryDistanceInMM;
    float leftPupilIrisDiameterRatio;
    float rightPupilIrisDiameterRatio;
    float leftPupilDiameterInMM;
    float rightPupilDiameterInMM;
    float leftIrisDiameterInMM;
    float rightIrisDiameterInMM;
    float leftEyeOpenness;   // 1 全开、0 全闭
    float rightEyeOpenness;  // 1 全开、0 全闭
};

struct Session;  // 不透明句柄

// ---- 函数指针签名（与 Varjo.h 中 VARJO_API 声明一致）----
using SessionInitFn = Session*();
using SessionShutDownFn = void(Session*);
using IsGazeAllowedFn = Bool(Session*);
using GazeInitFn = void(Session*);
using GetGazeFn = Gaze(Session*);
using GetGazeDataFn = Bool(Session*, Gaze*, EyeMeasurements*);

// 需要解析的全部导出符号名（缺任何一个都聚合进错误提示）。
inline constexpr const char* kSymbolSessionInit = "varjo_SessionInit";
inline constexpr const char* kSymbolSessionShutDown = "varjo_SessionShutDown";
inline constexpr const char* kSymbolIsGazeAllowed = "varjo_IsGazeAllowed";
inline constexpr const char* kSymbolGazeInit = "varjo_GazeInit";
inline constexpr const char* kSymbolGetGaze = "varjo_GetGaze";
inline constexpr const char* kSymbolGetGazeData = "varjo_GetGazeData";

// 一次会话需要的 API 指针集合；BindApiSymbols 成功后全部非空。
struct Api {
    SessionInitFn* session_init = nullptr;
    SessionShutDownFn* session_shut_down = nullptr;
    IsGazeAllowedFn* is_gaze_allowed = nullptr;
    GazeInitFn* gaze_init = nullptr;
    GetGazeFn* get_gaze = nullptr;
    GetGazeDataFn* get_gaze_data = nullptr;
};

} // namespace varjo
