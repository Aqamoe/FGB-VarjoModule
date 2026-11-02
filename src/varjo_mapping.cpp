#include "varjo_mapping.hpp"

#include <cmath>

namespace varjo {

namespace {

// 瞳孔合理范围（毫米）：Varjo 用 0 表示不可用；上限只是防御异常值。
constexpr double kPupilMinMm = 0.5;
constexpr double kPupilMaxMm = 12.0;

// 方向长度下限：清洗后小于它就认为本帧该方向不可用。
constexpr double kMinDirectionLength = 1e-9;

// 逐眼状态 → 置信度（具名常量，测试锁死数值）。
constexpr float kConfidenceTracked = 0.95f;
constexpr float kConfidenceCompensated = 0.70f;
constexpr float kConfidenceVisible = 0.40f;
constexpr float kConfidenceInvalid = 0.25f;

// 双精度 → float 的 NaN 清洗：非有限值一律当 0，绝不让 NaN 进帧槽。
float CleanToFloat(double value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return static_cast<float>(value);
}

// NaN/Inf → 0，其余截断到 [0,1] 后转 float。
float Clamp01Clean(double value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    if (value <= 0.0) {
        return 0.0f;
    }
    if (value >= 1.0) {
        return 1.0f;
    }
    return static_cast<float>(value);
}

double CleanCoordinate(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return value;
}

} // namespace

void CopySample(const Gaze& raw, GazeSample& out) {
    out.frame_number = raw.frameNumber;
    out.status = raw.status;
    out.left_status = raw.leftStatus;
    out.right_status = raw.rightStatus;
    out.left_forward = {raw.leftEye.forward[0], raw.leftEye.forward[1], raw.leftEye.forward[2]};
    out.right_forward = {raw.rightEye.forward[0], raw.rightEye.forward[1], raw.rightEye.forward[2]};
    out.combined_forward = {raw.gaze.forward[0], raw.gaze.forward[1], raw.gaze.forward[2]};
    out.focus_distance_m = raw.focusDistance;
    out.stability = raw.stability;
}

void CopyMeasurements(const EyeMeasurements& raw, EyeMeasurementSample& out) {
    out.left_openness = raw.leftEyeOpenness;
    out.right_openness = raw.rightEyeOpenness;
    out.left_pupil_mm = raw.leftPupilDiameterInMM;
    out.right_pupil_mm = raw.rightPupilDiameterInMM;
    out.ipd_mm = raw.interPupillaryDistanceInMM;
}

bool DirectionToSlot(const Vec3& forward_lh, float* out3) {
    // 左手系（X 右、Y 上、Z 前）→ FGB 的右手系（-Z 前）：翻 Z。
    const double x = CleanCoordinate(forward_lh.x);
    const double y = CleanCoordinate(forward_lh.y);
    const double z = -CleanCoordinate(forward_lh.z);
    const double length = std::sqrt(x * x + y * y + z * z);
    if (!(length > kMinDirectionLength)) {
        out3[0] = 0.0f;
        out3[1] = 0.0f;
        out3[2] = 0.0f;
        return false;
    }
    // Varjo 承诺 forward 已归一化；这里仍做防御性归一化并降为 float。
    out3[0] = static_cast<float>(x / length);
    out3[1] = static_cast<float>(y / length);
    out3[2] = static_cast<float>(z / length);
    return true;
}

float EyeConfidence(std::int64_t eye_status) {
    if (eye_status >= kGazeEyeStatusTracked) {
        return kConfidenceTracked;
    }
    if (eye_status == kGazeEyeStatusCompensated) {
        return kConfidenceCompensated;
    }
    if (eye_status == kGazeEyeStatusVisible) {
        return kConfidenceVisible;
    }
    return kConfidenceInvalid;
}

void WriteFrame(const GazeSample& sample, const EyeMeasurementSample* measurements,
                FgbFrameSlot* slot) {
    if (slot == nullptr) {
        return;
    }

    // ---- 逐眼注视方向（eyes 通道的 gaze 属性；右手系单位向量，-Z 前）----
    float direction[3];
    if (DirectionToSlot(sample.left_forward, direction) &&
        sample.status == kGazeStatusValid && sample.left_status >= kGazeEyeStatusVisible) {
        slot->left_eye.gaze_dir[0] = direction[0];
        slot->left_eye.gaze_dir[1] = direction[1];
        slot->left_eye.gaze_dir[2] = direction[2];
        slot->left_eye.gaze_confidence = EyeConfidence(sample.left_status);
        slot->attributes |= FGB_ATTR_LEFT_GAZE;
    }
    if (DirectionToSlot(sample.right_forward, direction) &&
        sample.status == kGazeStatusValid && sample.right_status >= kGazeEyeStatusVisible) {
        slot->right_eye.gaze_dir[0] = direction[0];
        slot->right_eye.gaze_dir[1] = direction[1];
        slot->right_eye.gaze_dir[2] = direction[2];
        slot->right_eye.gaze_confidence = EyeConfidence(sample.right_status);
        slot->attributes |= FGB_ATTR_RIGHT_GAZE;
    }

    // ---- 合并注视方向 + 置信度（厂商的 stability）----
    if (sample.status == kGazeStatusValid && DirectionToSlot(sample.combined_forward, direction)) {
        slot->combined_gaze_dir[0] = direction[0];
        slot->combined_gaze_dir[1] = direction[1];
        slot->combined_gaze_dir[2] = direction[2];
        slot->combined_gaze_confidence = Clamp01Clean(sample.stability);
        slot->attributes |= FGB_ATTR_COMBINED_GAZE;
    }

    // ---- 辐辏距离（focusDistance 单位米，厂商口径 0..2）----
    if (sample.status == kGazeStatusValid && std::isfinite(sample.focus_distance_m) &&
        sample.focus_distance_m > 0.0 && sample.focus_distance_m <= 2.0) {
        slot->vergence_distance_m = static_cast<float>(sample.focus_distance_m);
        slot->attributes |= FGB_ATTR_VERGENCE;
    }

    // ---- 眼测量：睁眼度 / 瞳孔（没有 measurements 就一位都不置）----
    if (measurements == nullptr) {
        return;
    }

    slot->left_eye.openness = Clamp01Clean(measurements->left_openness);
    slot->left_eye.openness_confidence = EyeConfidence(sample.left_status);
    slot->attributes |= FGB_ATTR_LEFT_OPENNESS;

    slot->right_eye.openness = Clamp01Clean(measurements->right_openness);
    slot->right_eye.openness_confidence = EyeConfidence(sample.right_status);
    slot->attributes |= FGB_ATTR_RIGHT_OPENNESS;

    if (std::isfinite(measurements->left_pupil_mm) && measurements->left_pupil_mm >= kPupilMinMm &&
        measurements->left_pupil_mm <= kPupilMaxMm) {
        slot->left_eye.pupil_diameter_mm = CleanToFloat(measurements->left_pupil_mm);
        slot->attributes |= FGB_ATTR_LEFT_PUPIL;
    }
    if (std::isfinite(measurements->right_pupil_mm) &&
        measurements->right_pupil_mm >= kPupilMinMm &&
        measurements->right_pupil_mm <= kPupilMaxMm) {
        slot->right_eye.pupil_diameter_mm = CleanToFloat(measurements->right_pupil_mm);
        slot->attributes |= FGB_ATTR_RIGHT_PUPIL;
    }
    // pupil_dilation 不写：Varjo 只给直径/比例，没有相对基线的 -1..1 口径。
}

} // namespace varjo
