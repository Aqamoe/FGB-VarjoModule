// 走真实 ABI 的冒烟：加载 varjo-eyetracking.dll → query → initialize → 若干 tick → teardown。
//
// 与 MeowFace 不同，本模块没有可以伪造的厂商运行时：CI 的 Windows runner 上
// 既没有 Varjo Base 也没有 Varjo 头显，所以 workflows 里 run_smoke=false，
// 这个程序只在开发机（装了 Varjo Base）或「无运行时也必须优雅失败」的验证中手动跑：
//   cmake --build build --config Release --target smoke-test
// 两种结局都接受：
//   * 有 Varjo Base：initialize 成功，允许 0 或多个提交帧（取决于是否佩戴），干净停止；
//   * 没有 Varjo Base：initialize 必须返回 0，且模块卡片状态里出现「请先安装 Varjo Base」，
//     绝不崩溃、绝不加载假符号。

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"
#include "fgb_module_api.h"

#include <windows.h>

#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法：%s <varjo-eyetracking.dll 路径>\n", argv[0]);
        return 2;
    }

    HMODULE library = ::LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(library != nullptr);
    if (library == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu（DLL 是否已构建？依赖是否在同目录？）\n",
                    ::GetLastError());
        return CheckSummary("varjo-abi-smoke");
    }

    const auto query = reinterpret_cast<decltype(&fgb_module_query_v2)>(
        ::GetProcAddress(library, "fgb_module_query_v2"));
    CHECK(query != nullptr);

    fgb::kit::testing::HostMock host;
    FgbInitParams params = host.MakeParams();

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    CHECK(module != nullptr);
    if (module == nullptr) {
        ::FreeLibrary(library);
        return CheckSummary("varjo-abi-smoke");
    }
    CHECK(module->id == std::string{"com.fgbbridge.varjo"});
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    CHECK_EQ(module->capabilities, static_cast<std::uint32_t>(FGB_CAP_EYES | FGB_CAP_GAZE));
    CHECK(query(FGB_MODULE_ABI_V2 + 1) == nullptr);  // ABI 不匹配必须拒绝

    void* self = nullptr;
    const int initialized = module->initialize(&self, host.services(), &params);
    if (initialized == 0) {
        // 无 Varjo Base / 头显：必须给出可操作的错误状态，而不是崩溃。
        CHECK(self == nullptr);
        CHECK(host.HasStatus(FGB_STATUS_ERROR));
        const std::string detail = host.LastStatusDetail();
        CHECK(detail.find("Varjo Base") != std::string::npos);
        std::printf("无厂商运行时路径验证通过：%s\n", detail.c_str());
    } else {
        CHECK(self != nullptr);
        // 有运行时：允许佩戴/校准导致的空提交，只要稳定不崩即可。
        for (int tick = 0; tick < 30; ++tick) {
            module->update(self);
        }
        for (const auto& slot : host.committed()) {
            CHECK((slot.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                      FGB_ATTR_COMBINED_GAZE | FGB_ATTR_LEFT_OPENNESS |
                                      FGB_ATTR_RIGHT_OPENNESS)) != 0 ||
                  slot.attributes == 0);
        }
        module->teardown(self);
        CHECK(host.HasStatus(FGB_STATUS_STOPPED));
        std::printf("真机路径验证通过：提交 %zu 帧\n", host.committed().size());
    }

    ::FreeLibrary(library);
    return CheckSummary("varjo-abi-smoke");
}
