// Varjo 眼追 → FaceGaze Bridge 原生模块。
//
// 运行时加载 Varjo 厂商运行时（VarjoLib.dll，旧版为 varjo.dll），经
// varjo_GazeInit + varjo_GetGazeData / varjo_GetGaze 轮询 100/200Hz 的眼动
// 数据，映射成双眼注视方向、合并注视、辐辏距离、睁眼度与瞳孔直径。
// 不链接、不打包厂商 DLL；没装 Varjo Base 时模块卡片直接给出「请先安装
// Varjo Base」。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "varjo_mapping.hpp"
#include "varjo_runtime.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

// 眼动按 100/200Hz 出帧；2 秒没有有效数据就认为断流（摘头显、被抢占等）。
constexpr std::uint64_t kNoDataTimeoutNs = 2ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class VarjoSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        std::string dll_path;
        if (config.Load(directory, "config.ini", config_error)) {
            config.AllowKeys({"dll_path"});
            dll_path = config.Get("dll_path", "");
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        if (!runtime_.Open(directory, dll_path, error)) {
            return false;
        }
        host.Info("已加载 Varjo 运行时：" + runtime_.LoadedDll() + "，等待眼动数据");
        host.StatusStarting("等待 Varjo 眼动数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        varjo::PollResult result{};
        if (!runtime_.Poll(result)) {
            return false;
        }

        if (!result.has_measurements && !measurements_warned_) {
            measurements_warned_ = true;
            host.Warn("varjo_GetGazeData 没有返回眼测量数据（睁眼度/瞳孔需要 Varjo Base Pro "
                      "授权），本会话只输出注视方向");
        }

        if (result.gaze.status == varjo::kGazeStatusAdjust) {
            host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs,
                               "Varjo 眼动正在校准/调整中，暂时无有效注视数据");
        } else if (result.gaze.status == varjo::kGazeStatusInvalid && !result.fresh &&
                   last_valid_ns_ == 0) {
            host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs,
                               "Varjo 眼动状态为「数据不可用」：请确认佩戴头显且双眼可见");
        }

        if (result.fresh) {
            last_valid_ns_ = now_ns;
        }

        // 断流就停帧；无效帧同样不提交，绝不给属性位「装数据」。
        if (!staleness_.Check(host, last_valid_ns_, now_ns) || !result.fresh) {
            return false;
        }

        varjo::WriteFrame(result.gaze, result.has_measurements ? &result.measurements : nullptr,
                          &slot);
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        runtime_.Close();
        host.StatusStopped("Varjo 眼动已停止");
    }

private:
    varjo::VarjoRuntime runtime_;
    varjo::StallMonitor staleness_{kNoDataTimeoutNs, "Varjo"};
    std::uint64_t last_valid_ns_ = 0;
    std::uint64_t warn_ns_ = 0;
    bool measurements_warned_ = false;
};

} // namespace

FGB_KIT_EXPORT(VarjoSource, "com.fgbbridge.varjo", "Varjo Eye Tracking",
               FGB_CAP_EYES | FGB_CAP_GAZE)
