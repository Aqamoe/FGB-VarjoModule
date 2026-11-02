#include "varjo_runtime.hpp"

#include "fgb/kit/host.hpp"
#include "fgb/kit/library.hpp"
#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cstdint>
#include <map>
#include <string>

using namespace fgb::kit;
using namespace varjo;

namespace {

// 假解析器：只“导出”名单里的符号，模拟运行时符号缺失。
class FakeResolver : public SymbolResolver {
public:
    void Provide(const std::string& name) { symbols_[name] = reinterpret_cast<void*>(&storage_); }
    void* Resolve(const std::string& name) override {
        const auto found = symbols_.find(name);
        return found == symbols_.end() ? nullptr : found->second;
    }

private:
    std::map<std::string, void*> symbols_;
    int storage_ = 0;
};

int64_t SecondsNs(std::uint64_t seconds) {
    return static_cast<int64_t>(seconds) * 1000000000ll;
}

} // namespace

int main() {
    // ---- 符号齐全时 BindApiSymbols 全部落位 ----
    Api full{};
    FakeResolver complete;
    complete.Provide(kSymbolSessionInit);
    complete.Provide(kSymbolSessionShutDown);
    complete.Provide(kSymbolIsGazeAllowed);
    complete.Provide(kSymbolGazeInit);
    complete.Provide(kSymbolGetGaze);
    complete.Provide(kSymbolGetGazeData);
    std::vector<std::string> missing;
    CHECK(BindApiSymbols(complete, full, missing));
    CHECK(missing.empty());
    CHECK(full.session_init != nullptr);
    CHECK(full.get_gaze_data != nullptr);

    // ---- 缺符号：缺失符号名逐个聚合进提示 ----
    Api partial{};
    FakeResolver deficient;
    deficient.Provide(kSymbolSessionInit);
    deficient.Provide(kSymbolSessionShutDown);
    deficient.Provide(kSymbolGazeInit);
    deficient.Provide(kSymbolGetGaze);
    missing.clear();
    CHECK(!BindApiSymbols(deficient, partial, missing));
    CHECK_EQ(missing.size(), static_cast<std::size_t>(2));
    CHECK(missing[0] == std::string{kSymbolIsGazeAllowed});
    CHECK(missing[1] == std::string{kSymbolGetGazeData});
    const std::string hint = FormatMissingSymbols("VarjoLib.dll", missing);
    CHECK(hint.find("varjo_IsGazeAllowed") != std::string::npos);
    CHECK(hint.find("varjo_GetGazeData") != std::string::npos);
    CHECK(hint.find("请先安装/升级 Varjo Base") != std::string::npos);

    // ---- 找不到运行时：提示必须给出「请先安装 Varjo Base」 ----
    const std::string not_found = FormatRuntimeNotFound({"VarjoLib.dll", "varjo.dll"});
    CHECK(not_found.find("请先安装 Varjo Base") != std::string::npos);
    CHECK(not_found.find("VarjoLib.dll") != std::string::npos);
    CHECK(not_found.find("varjo.dll") != std::string::npos);
    CHECK(not_found.find("dll_path") != std::string::npos);

    // ---- 真实加载路径：文件不存在时 Library 报错、Open 给出安装提示 ----
    fgb::kit::Library library;
    std::string load_error;
    CHECK(!library.Load("fgb_varjo_definitely_absent.dll", "", load_error));
    CHECK(load_error.find("找不到") != std::string::npos);
    CHECK(!load_error.empty());

    // ---- 断流监控：先报「还没数据」，恢复→活跃，再断流→限频一次 ----
    testing::HostMock host;
    Host logger{host.services()};
    StallMonitor monitor{2 * 1000000000ull, "Varjo"};

    CHECK(!monitor.Check(logger, 0, static_cast<std::uint64_t>(SecondsNs(1))));
    CHECK(!monitor.Check(logger, 0, static_cast<std::uint64_t>(SecondsNs(2))));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(1));
    CHECK(host.LastStatusDetail().find("还没输出有效眼动数据") != std::string::npos);

    const std::uint64_t good_ns = static_cast<std::uint64_t>(SecondsNs(10));
    CHECK(monitor.Check(logger, good_ns, good_ns));
    CHECK(host.HasStatus(FGB_STATUS_ACTIVE));
    CHECK(host.LastStatusDetail().find("数据已恢复") != std::string::npos);

    // 沉默超过超时：报一次「已 N 秒」，再查不重复报。
    CHECK(!monitor.Check(logger, good_ns, static_cast<std::uint64_t>(SecondsNs(15))));
    CHECK(!monitor.Check(logger, good_ns, static_cast<std::uint64_t>(SecondsNs(30))));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));
    CHECK(host.LastStatusDetail().find("已 5 秒") != std::string::npos);

    // 再次来数据 → 恢复，只报一次。
    const std::uint64_t back_ns = static_cast<std::uint64_t>(SecondsNs(40));
    CHECK(monitor.Check(logger, back_ns, back_ns));
    CHECK(monitor.Check(logger, back_ns, static_cast<std::uint64_t>(SecondsNs(41))));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ACTIVE), static_cast<std::size_t>(2));

    return CheckSummary("varjo-runtime");
}
