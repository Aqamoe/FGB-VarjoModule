#pragma once
// Varjo 眼动数据 → FGB 帧槽（eyes / gaze 通道）的映射。
//
// 输入是与厂商 API 解耦的采样结构（GazeSample / EyeMeasurementSample），
// 单测用假数据驱动；真实运行时（varjo_runtime.*）只负责把 varjo_Gaze 与
// varjo_EyeMeasurements 原样搬进这两个结构。
//
// 换算依据（一手核对 Varjo SDK 4.16.0 文档与头文件注释）：
//   * Varjo 注视方向是左手系、相对头姿：X 右、Y 上、Z 前，forward 已归一化；
//     FGB ABI 要求右手系、-Z 前（OpenXR 口径）→ 只翻转 Z 分量，再防御性归一化。
//   * varjo_EyeMeasurements 的瞳孔直径单位是毫米，与 ABI 的 pupil_diameter_mm
//     同单位，直接透传；0 表示厂商侧不可用，不置属性位。
//   * 睁眼度 [0,1]（1 全开），与 ABI 同义；NaN/Inf 一律清洗，无效帧不置任何属性位。

#include "varjo_api.hpp"

#include "fgb_module_api.h"

#include <cstdint>

namespace varjo {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// varjo_Gaze 的中转副本（避免把厂商 POD 的数组成员带进测试断言）。
struct GazeSample {
    std::int64_t frame_number = 0;
    std::int64_t status = kGazeStatusInvalid;        // varjo_GazeStatus
    std::int64_t left_status = kGazeEyeStatusInvalid; // varjo_GazeEyeStatus
    std::int64_t right_status = kGazeEyeStatusInvalid;
    Vec3 left_forward{};   // 左手系单位向量
    Vec3 right_forward{};
    Vec3 combined_forward{};
    double focus_distance_m = 0.0; // [0,2] 米
    double stability = 0.0;        // [0,1]
};

// varjo_EyeMeasurements 的中转副本。
struct EyeMeasurementSample {
    double left_openness = 0.0;    // [0,1]
    double right_openness = 0.0;
    double left_pupil_mm = 0.0;    // 0 = 不可用
    double right_pupil_mm = 0.0;
    double ipd_mm = 0.0;
};

// 从厂商结构拷贝（数组→具名字段，双精度原样保留）。
void CopySample(const Gaze& raw, GazeSample& out);
void CopyMeasurements(const EyeMeasurements& raw, EyeMeasurementSample& out);

// 左手系 → 右手系（翻 Z）+ 归一化 + NaN 清洗；out3 是 float[3]。
// 返回 false 表示清洗后长度退化（全 NaN/零向量），调用方不得置属性位。
bool DirectionToSlot(const Vec3& forward_lh, float* out3);

// 逐眼状态 → 置信度常量（具名映射，便于测试与调整）。
float EyeConfidence(std::int64_t eye_status);

// 写双眼方向 / 合并方向 / 辐辏 / 睁眼度 / 瞳孔，并只给真正写了数据的属性位置位。
// measurements == nullptr 表示本轮没有眼测量数据（如无 Varjo Base Pro），
// 此时不写任何 openness/pupil 位。
void WriteFrame(const GazeSample& sample, const EyeMeasurementSample* measurements,
                FgbFrameSlot* slot);

} // namespace varjo
