#include "varjo_runtime.hpp"

#include <algorithm>
#include <cctype>

namespace varjo {

namespace {

// 与 fgb-native.json 的 DllFileName 保持一致：本模块的 DLL 已改名为 varjo-eyetracking.dll，不再与厂商旧名同名。
// 加载“旧时代”厂商名 varjo.dll 时绝不能拼模块目录，否则会命中我们自己。
constexpr const char* kLegacyVendorDll = "varjo.dll";
constexpr const char* kCurrentVendorDll = "VarjoLib.dll";

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// 取“本代码所属模块”的句柄（真实宿主里就是本模块 DLL 自己）。
HMODULE SelfModuleHandle() {
    HMODULE self = nullptr;
    const BOOL ok = ::GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&SelfModuleHandle), &self);
    return ok && self != nullptr ? self : nullptr;
}

// 候选名是否撞上了我们自己：本模块 DLL 原名也是 varjo.dll，会与厂商旧名撞车，现已改名为 varjo-eyetracking.dll，
// 而 LoadLibrary 对“已加载的同名模块”直接返回现有句柄——不拦的话会把我们自己
// 当成厂商运行时，报出一串假“缺失符号”。
bool IsSelfDll(const std::string& name) {
    const HMODULE self = SelfModuleHandle();
    if (self == nullptr) {
        return false;
    }
    char path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameA(self, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    const std::string full{path, length};
    const std::size_t slash = full.find_last_of("\\/");
    const std::string base = slash == std::string::npos ? full : full.substr(slash + 1);
    return ToLower(base) == ToLower(name);
}

bool TryLoad(fgb::kit::Library& library, const std::string& name, const std::string& module_dir,
             std::string& error) {
    if (IsSelfDll(name)) {
        error = "候选 " + name + " 与本模块自身同名，已跳过（不会把自己当厂商运行时）";
        return false;
    }
    std::string directory = module_dir;
    if (ToLower(name) == ToLower(kLegacyVendorDll)) {
        // 旧厂商名与我们的 DLL 同名：即便句柄检查放行（外部真厂商 DLL），也绝不
        // 拼模块目录，避免命中随模块分发的同名文件。
        directory.clear();
    }
    return library.Load(name, directory, error);
}

// 单个符号绑定：缺失就按出现顺序记名（MSVC 14.36 对「捕获数组 + lambda 内下标」
// 会 ICE，所以这里用具名函数而不是 lambda）。
bool BindOneSymbol(SymbolResolver& resolver, const char* name, void** slot,
                   std::vector<std::string>& missing) {
    void* address = resolver.Resolve(name);
    if (address == nullptr) {
        missing.emplace_back(name);
        return false;
    }
    *slot = address;
    return true;
}

} // namespace

bool BindApiSymbols(SymbolResolver& resolver, Api& api, std::vector<std::string>& missing) {
    BindOneSymbol(resolver, kSymbolSessionInit, reinterpret_cast<void**>(&api.session_init),
                  missing);
    BindOneSymbol(resolver, kSymbolSessionShutDown, reinterpret_cast<void**>(&api.session_shut_down),
                  missing);
    BindOneSymbol(resolver, kSymbolIsGazeAllowed, reinterpret_cast<void**>(&api.is_gaze_allowed),
                  missing);
    BindOneSymbol(resolver, kSymbolGazeInit, reinterpret_cast<void**>(&api.gaze_init), missing);
    BindOneSymbol(resolver, kSymbolGetGaze, reinterpret_cast<void**>(&api.get_gaze), missing);
    BindOneSymbol(resolver, kSymbolGetGazeData, reinterpret_cast<void**>(&api.get_gaze_data),
                  missing);
    return missing.empty();
}

std::string FormatRuntimeNotFound(const std::vector<std::string>& tried_names) {
    std::string joined;
    for (const auto& name : tried_names) {
        if (!joined.empty()) {
            joined += "、";
        }
        joined += name;
    }
    return "找不到 Varjo 运行时（已尝试：" + joined +
           "）。请先安装 Varjo Base 并连接 Varjo 头显，然后重启引擎；"
           "也可以在模块目录的 config.ini 里用 dll_path 指定 VarjoLib.dll 的完整路径。";
}

std::string FormatMissingSymbols(const std::string& dll_name,
                                 const std::vector<std::string>& missing) {
    std::string joined;
    for (const auto& name : missing) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += name;
    }
    return "在 " + dll_name + " 里找不到 " + std::to_string(missing.size()) +
           " 个必需的 Varjo 符号：" + joined +
           "。这通常说明它不是 Varjo 运行时或版本过旧；请先安装/升级 Varjo Base。";
}

bool VarjoRuntime::Open(const std::string& module_dir, const std::string& dll_path_override,
                        std::string& error) {
    Close();

    std::vector<std::string> tried;
    bool loaded = false;
    std::string last_error;

    if (!dll_path_override.empty()) {
        tried.emplace_back(dll_path_override);
        loaded = library_.Load(dll_path_override, "", last_error);
    }
    if (!loaded) {
        tried.emplace_back(kCurrentVendorDll);
        loaded = TryLoad(library_, kCurrentVendorDll, module_dir, last_error);
    }
    if (!loaded) {
        tried.emplace_back(kLegacyVendorDll);
        loaded = TryLoad(library_, kLegacyVendorDll, module_dir, last_error);
    }
    if (!loaded) {
        error = FormatRuntimeNotFound(tried);
        return false;
    }
    loaded_name_ = library_.Name();

    LibraryResolver resolver(library_);
    std::vector<std::string> missing;
    if (!BindApiSymbols(resolver, api_, missing)) {
        error = FormatMissingSymbols(loaded_name_, missing);
        library_.Close();
        return false;
    }

    session_ = api_.session_init();
    if (session_ == nullptr) {
        error = "无法创建 Varjo 会话。请先安装 Varjo Base，确认头显已连接、"
                "Varjo Base 正在运行，然后重试。";
        library_.Close();
        return false;
    }

    if (api_.is_gaze_allowed(session_) != kTrue) {
        error = "Varjo Base 未允许使用眼动数据。请在 Varjo Base 的「设置 → 眼动」中启用"
                "眼动追踪并完成校准。";
        api_.session_shut_down(session_);
        session_ = nullptr;
        library_.Close();
        return false;
    }

    // 默认初始化：Standard 平滑 + 头显支持的最高频率（Base 4.1+ 默认即最高，可 200Hz）。
    api_.gaze_init(session_);
    return true;
}

bool VarjoRuntime::Poll(PollResult& result) {
    if (session_ == nullptr) {
        return false;
    }
    Gaze raw{};
    EyeMeasurements measurements{};
    bool has_measurements = false;
    if (api_.get_gaze_data(session_, &raw, &measurements) == kTrue) {
        has_measurements = true;
    } else {
        // 拿不到眼测量（常见于没有 Varjo Base Pro 授权）：退回注视方向接口。
        raw = api_.get_gaze(session_);
    }

    CopySample(raw, result.gaze);
    if (has_measurements) {
        CopyMeasurements(measurements, result.measurements);
    }
    result.has_measurements = has_measurements;

    // 「帧号推进 + 状态有效」才算一次新数据；重复帧与无效帧不能刷新断流计时。
    const bool valid = raw.status == kGazeStatusValid && raw.frameNumber != 0;
    result.fresh = valid && raw.frameNumber != last_frame_number_;
    if (result.fresh) {
        last_frame_number_ = raw.frameNumber;
    }
    return true;
}

void VarjoRuntime::Close() {
    if (session_ != nullptr && api_.session_shut_down != nullptr) {
        api_.session_shut_down(session_);
    }
    session_ = nullptr;
    api_ = Api{};
    library_.Close();
    loaded_name_.clear();
    last_frame_number_ = -1;
    gaze_data_warned_ = false;
}

bool StallMonitor::Check(fgb::kit::Host& host, std::uint64_t last_ns, std::uint64_t now_ns) {
    const bool healthy = last_ns != 0 && now_ns >= last_ns && now_ns - last_ns <= timeout_ns_;
    if (healthy) {
        if (!healthy_) {
            host.StatusActive(label_ + " 数据已恢复");
            reported_ = false;
        }
        healthy_ = true;
        return true;
    }
    healthy_ = false;
    if (reported_) {
        return false;
    }
    reported_ = true;
    if (last_ns == 0) {
        host.StatusError(label_ + " 还没输出有效眼动数据：请佩戴头显、在 Varjo Base 中允许"
                                 "眼动追踪并完成校准");
    } else {
        const std::uint64_t silent_seconds = (now_ns - last_ns) / 1000000000ull;
        host.StatusError(label_ + " 已 " + std::to_string(silent_seconds) +
                         " 秒没有有效眼动数据：请确认仍佩戴头显，且眼动没有被其它应用独占");
    }
    return false;
}

} // namespace varjo
