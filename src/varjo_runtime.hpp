#pragma once
// Varjo 运行时适配层：厂商 DLL 的动态加载、符号绑定与错误文案。
//
// CI 上没有 Varjo SDK / 头显，所以这里把“取符号”抽象成 SymbolResolver：
//   * 真实路径用 Kit 的 fgb::kit::Library（LoadLibrary + GetProcAddress）；
//   * 单测用假解析器，验证“缺哪些符号、提示怎么说”，不需要厂商运行时。

#include "varjo_api.hpp"
#include "varjo_mapping.hpp"

#include "fgb/kit/host.hpp"
#include "fgb/kit/library.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace varjo {

// 按名字取导出符号；返回 nullptr 表示缺失。
class SymbolResolver {
public:
    virtual ~SymbolResolver() = default;
    virtual void* Resolve(const std::string& name) = 0;
};

// 基于 fgb::kit::Library 的真实解析器。
class LibraryResolver : public SymbolResolver {
public:
    explicit LibraryResolver(fgb::kit::Library& library) : library_(library) {}
    void* Resolve(const std::string& name) override { return library_.Symbol(name); }

private:
    fgb::kit::Library& library_;
};

// 把全部所需符号绑进 Api；missing 按出现顺序收集缺失符号名。
bool BindApiSymbols(SymbolResolver& resolver, Api& api, std::vector<std::string>& missing);

// 「一个符号都没有」的运行时候选（不存在文件 / 不是 Varjo 运行时）统一提示。
// 文案必须包含「请先安装 Varjo Base」，宿主卡片直接可读。
std::string FormatRuntimeNotFound(const std::vector<std::string>& tried_names);

// 找到 DLL 但符号不全：把缺失符号名逐个列进提示。
std::string FormatMissingSymbols(const std::string& dll_name,
                                 const std::vector<std::string>& missing);

// 一次成功轮询的结果。
struct PollResult {
    GazeSample gaze{};
    EyeMeasurementSample measurements{};
    bool has_measurements = false;  // varjo_GetGazeData 成功才有眼测量（需 Base Pro）
    bool fresh = false;             // 拿到了新的有效帧（状态 Valid 且帧号推进）
};

// 眼追运行时的抽象：真实实现走厂商 DLL，单测可以完全不碰它。
class IGazeRuntime {
public:
    virtual ~IGazeRuntime() = default;
    virtual bool Open(const std::string& module_dir, const std::string& dll_path_override,
                      std::string& error) = 0;
    virtual bool Poll(PollResult& result) = 0;
    virtual void Close() = 0;
};

// 真实实现：加载 VarjoLib.dll（回退旧名 varjo.dll）→ 会话 → GazeInit → 轮询。
class VarjoRuntime : public IGazeRuntime {
public:
    bool Open(const std::string& module_dir, const std::string& dll_path_override,
              std::string& error) override;
    bool Poll(PollResult& result) override;
    void Close() override;

    const std::string& LoadedDll() const { return loaded_name_; }

private:
    fgb::kit::Library library_;
    Api api_{};
    Session* session_ = nullptr;
    std::string loaded_name_;
    std::int64_t last_frame_number_ = -1;
    bool gaze_data_warned_ = false;
};

// 断流监控：语义同 Kit 的 Staleness，但提示文案换成 Varjo 场景
//（佩戴 / 眼动开关 / 校准），不再提“同一网段的数据包”。
class StallMonitor {
public:
    StallMonitor(std::uint64_t timeout_ns, std::string label)
        : timeout_ns_(timeout_ns), label_(std::move(label)) {}

    // 返回 true 表示当前有新数据；失败时已经通过 host 报过一次错误。
    bool Check(fgb::kit::Host& host, std::uint64_t last_ns, std::uint64_t now_ns);

private:
    std::uint64_t timeout_ns_ = 0;
    std::string label_;
    bool healthy_ = false;
    bool reported_ = false;
};

} // namespace varjo
