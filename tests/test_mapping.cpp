#include "varjo_mapping.hpp"

#include "fgb/kit/testing/check.hpp"

#include <cmath>
#include <limits>

using namespace varjo;

namespace {

GazeSample ValidSample() {
    GazeSample sample{};
    sample.frame_number = 42;
    sample.status = kGazeStatusValid;
    sample.left_status = kGazeEyeStatusTracked;
    sample.right_status = kGazeEyeStatusTracked;
    // 厂商左手系单位向量：X 右、Y 上、Z 前
    sample.left_forward = {0.0, 0.0, 1.0};
    sample.right_forward = {0.5, 0.0, std::sqrt(0.75)};
    sample.combined_forward = {0.25, 0.0, std::sqrt(1.0 - 0.0625)};
    sample.focus_distance_m = 1.2;
    sample.stability = 0.8;
    return sample;
}

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

EyeMeasurementSample Measurements() {
    EyeMeasurementSample meas{};
    meas.left_openness = 0.9;
    meas.right_openness = 0.6;
    meas.left_pupil_mm = 3.4;
    meas.right_pupil_mm = 3.2;
    meas.ipd_mm = 63.5;
    return meas;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

} // namespace

int main() {
    // ---- 厂商结构 → 采样：数组降为具名字段，双精度原样保留 ----
    Gaze raw{};
    raw.leftEye.forward[0] = 0.1;
    raw.leftEye.forward[1] = -0.2;
    raw.leftEye.forward[2] = 0.97;
    raw.status = kGazeStatusValid;
    raw.leftStatus = kGazeEyeStatusTracked;
    raw.rightStatus = kGazeEyeStatusInvalid;
    raw.frameNumber = 7;
    raw.focusDistance = 1.23456789012345;
    GazeSample copied{};
    CopySample(raw, copied);
    CHECK_NEAR(copied.left_forward.x, 0.1, 1e-12);
    CHECK_NEAR(copied.left_forward.y, -0.2, 1e-12);
    CHECK_NEAR(copied.focus_distance_m, 1.23456789012345, 1e-12);
    CHECK_EQ(copied.frame_number, static_cast<std::int64_t>(7));
    CHECK_EQ(copied.right_status, kGazeEyeStatusInvalid);

    EyeMeasurements raw_meas{};
    raw_meas.leftEyeOpenness = 0.75f;
    raw_meas.leftPupilDiameterInMM = 2.5f;
    raw_meas.interPupillaryDistanceInMM = 62.0f;
    EyeMeasurementSample copied_meas{};
    CopyMeasurements(raw_meas, copied_meas);
    CHECK_NEAR(copied_meas.left_openness, 0.75, 1e-9);
    CHECK_NEAR(copied_meas.left_pupil_mm, 2.5, 1e-9);

    // ---- 坐标系：左手系（+Z 前）→ ABI 右手系（-Z 前），只翻 Z ----
    float direction[3] = {};
    CHECK(DirectionToSlot({0.0, 0.0, 1.0}, direction));
    CHECK_NEAR(direction[0], 0.0f, 1e-7);
    CHECK_NEAR(direction[1], 0.0f, 1e-7);
    CHECK_NEAR(direction[2], -1.0f, 1e-7);
    CHECK(DirectionToSlot({0.5, 0.25, std::sqrt(1.0 - 0.25 - 0.0625)}, direction));
    CHECK_NEAR(direction[0], 0.5f, 1e-7);
    CHECK_NEAR(direction[1], 0.25f, 1e-7);
    CHECK_NEAR(direction[2], -std::sqrt(0.6875), 1e-6);

    // ---- 正常帧：双眼方向 + 合并 + 辐辏 + 睁眼度 + 瞳孔全量写出 ----
    const GazeSample sample = ValidSample();
    const EyeMeasurementSample meas = Measurements();
    FgbFrameSlot slot = FreshSlot();
    WriteFrame(sample, &meas, &slot);

    CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
    CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0);
    CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) != 0);
    CHECK((slot.attributes & FGB_ATTR_VERGENCE) != 0);
    CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    CHECK((slot.attributes & FGB_ATTR_RIGHT_OPENNESS) != 0);
    CHECK((slot.attributes & FGB_ATTR_LEFT_PUPIL) != 0);
    CHECK((slot.attributes & FGB_ATTR_RIGHT_PUPIL) != 0);
    CHECK((slot.attributes & FGB_ATTR_EXPRESSION) == 0);  // 本模块不产表情

    // 双精度 → float：值按 float 精度透传，单位不变（米/毫米/比例）
    CHECK_NEAR(slot.left_eye.gaze_dir[2], -1.0f, 1e-7);
    CHECK_NEAR(slot.right_eye.gaze_dir[0], 0.5f, 1e-7);
    CHECK_NEAR(slot.vergence_distance_m, 1.2f, 1e-7);
    CHECK_NEAR(slot.left_eye.openness, 0.9f, 1e-7);
    CHECK_NEAR(slot.left_eye.pupil_diameter_mm, 3.4f, 1e-7);
    // dilation 无基线口径，必须保持 0
    CHECK_NEAR(slot.left_eye.pupil_dilation, 0.0f, 1e-9);

    // 置信度 = 具名的逐眼状态映射
    CHECK_NEAR(slot.left_eye.gaze_confidence, 0.95f, 1e-9);
    CHECK_NEAR(slot.combined_gaze_confidence, 0.8f, 1e-9);

    // ---- 无效帧不置属性位 ----
    // 主状态无效：注视/合并/辐辏全部不写。
    GazeSample invalid = ValidSample();
    invalid.status = kGazeStatusInvalid;
    FgbFrameSlot no_gaze = FreshSlot();
    WriteFrame(invalid, nullptr, &no_gaze);
    CHECK((no_gaze.attributes & FGB_ATTR_LEFT_GAZE) == 0);
    CHECK((no_gaze.attributes & FGB_ATTR_RIGHT_GAZE) == 0);
    CHECK((no_gaze.attributes & FGB_ATTR_COMBINED_GAZE) == 0);
    CHECK((no_gaze.attributes & FGB_ATTR_VERGENCE) == 0);

    // 校准中同样停帧
    GazeSample adjusting = ValidSample();
    adjusting.status = kGazeStatusAdjust;
    FgbFrameSlot calibrating = FreshSlot();
    WriteFrame(adjusting, &meas, &calibrating);
    CHECK((calibrating.attributes & FGB_ATTR_COMBINED_GAZE) == 0);
    // 眼测量在厂商侧「status 0 行仍可用」，睁眼度不受主状态影响
    CHECK((calibrating.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);

    // 单眼未追踪：只砍那一只眼的方向位，另一只照常
    GazeSample one_eye = ValidSample();
    one_eye.left_status = kGazeEyeStatusInvalid;
    FgbFrameSlot half = FreshSlot();
    WriteFrame(one_eye, &meas, &half);
    CHECK((half.attributes & FGB_ATTR_LEFT_GAZE) == 0);
    CHECK((half.attributes & FGB_ATTR_RIGHT_GAZE) != 0);

    // 没有 measurements（如无 Base Pro）：不写任何睁眼度/瞳孔位
    FgbFrameSlot gaze_only = FreshSlot();
    WriteFrame(ValidSample(), nullptr, &gaze_only);
    CHECK((gaze_only.attributes & FGB_ATTR_LEFT_GAZE) != 0);
    CHECK((gaze_only.attributes & FGB_ATTR_LEFT_OPENNESS) == 0);
    CHECK((gaze_only.attributes & FGB_ATTR_RIGHT_PUPIL) == 0);

    // ---- 数值清洗：NaN/Inf 不进帧槽、退化向量不置位 ----
    GazeSample poisoned = ValidSample();
    poisoned.left_forward = {kNaN, 0.0, kInf};
    poisoned.combined_forward = {kNaN, kNaN, kNaN};
    poisoned.focus_distance_m = kNaN;
    poisoned.stability = kNaN;
    FgbFrameSlot clean = FreshSlot();
    WriteFrame(poisoned, &meas, &clean);
    CHECK((clean.attributes & FGB_ATTR_LEFT_GAZE) == 0);      // 方向退化 → 不写
    CHECK((clean.attributes & FGB_ATTR_RIGHT_GAZE) != 0);     // 右眼未受影响
    CHECK((clean.attributes & FGB_ATTR_COMBINED_GAZE) == 0);  // 全 NaN 方向 → 不写
    CHECK((clean.attributes & FGB_ATTR_VERGENCE) == 0);       // NaN 距离 → 不写
    CHECK(std::isfinite(clean.left_eye.gaze_dir[0]));
    CHECK(std::isfinite(clean.right_eye.gaze_dir[2]));

    // 睁眼度 NaN/越界 → 清洗为 [0,1] 内有限值；瞳孔非法值 → 不置位
    EyeMeasurementSample bad_meas = Measurements();
    bad_meas.left_openness = kNaN;
    bad_meas.right_openness = 1.7;
    bad_meas.left_pupil_mm = 0.0;   // 厂商语义：0 = 不可用
    bad_meas.right_pupil_mm = 999.0;  // 越界防御
    FgbFrameSlot cleaned = FreshSlot();
    WriteFrame(ValidSample(), &bad_meas, &cleaned);
    CHECK_NEAR(cleaned.left_eye.openness, 0.0f, 1e-9);
    CHECK(std::isfinite(cleaned.left_eye.openness));
    CHECK_NEAR(cleaned.right_eye.openness, 1.0f, 1e-9);
    CHECK((cleaned.attributes & FGB_ATTR_LEFT_PUPIL) == 0);
    CHECK((cleaned.attributes & FGB_ATTR_RIGHT_PUPIL) == 0);
    CHECK((cleaned.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);

    // 焦点距离超过厂商口径上限（>2m）：不写辐辏
    GazeSample far = ValidSample();
    far.focus_distance_m = 2.5;
    FgbFrameSlot no_vergence = FreshSlot();
    WriteFrame(far, nullptr, &no_vergence);
    CHECK((no_vergence.attributes & FGB_ATTR_VERGENCE) == 0);

    return CheckSummary("varjo-mapping");
}
