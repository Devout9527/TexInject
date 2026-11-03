#pragma once

#include <array>
#include <cstring>
#include <string>
#include <string_view>

// 来源：custom-resource-source-standalone-20261023（MIT，见 LICENSE-resource_mount.txt）。
// 改动：别名后缀与物理目录改为运行期传入（原来是 3.10 专用的编译期常量），
// 这样同一份代码能同时服务 3.9 的 resource_packs/<FirstPatch> 和 3.10 的
// netease_client_packs/pack_<md5>，物理目录也可以放在用户能看见的外部目录。
namespace resource_mount {
using namespace std::string_view_literals;

inline constexpr size_t kMaxPath = 4096;
enum class Route { Original, Redirect, Invalid };

inline bool Within(std::string_view path, std::string_view root) {
    return path == root || (path.size() > root.size() &&
        path.substr(0, root.size()) == root && path[root.size()] == '/');
}
inline bool CleanAbsolute(std::string_view path) {
    if (path.size() < 2 || path.size() >= kMaxPath || path[0] != '/' ||
        path.back() == '/' || path.find('\0') != path.npos) return false;
    for (size_t begin = 1; begin <= path.size();) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == path.npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == path.npos) break;
        begin = end + 1;
    }
    return true;
}
struct Paths {
    std::array<std::string, 3> aliases{};
    std::string storage;
    // aliasSuffix: 相对 dataDir 的发现别名（游戏本来就会加载的那个包目录），
    //   storage: 我们自己的物理目录绝对路径（可以是外部存储）。
    bool Initialize(const std::string& supplied, const std::string& canonical,
                    const std::string& aliasSuffix, const std::string& storagePath) {
        if (!CleanAbsolute(supplied) || !CleanAbsolute(canonical)) return false;
        if (!CleanAbsolute(storagePath)) return false;
        if (aliasSuffix.size() < 2 || aliasSuffix[0] != '/' || aliasSuffix.back() == '/')
            return false;
        aliases[0] = supplied + aliasSuffix;
        aliases[1] = canonical + aliasSuffix;
        // Android's two spellings of the same owner, not another app/user.
        constexpr std::string_view user0 = "/data/user/0/"sv, data = "/data/data/"sv;
        if (canonical.compare(0, user0.size(), user0) == 0)
            aliases[2] = std::string(data) + canonical.substr(user0.size()) + aliasSuffix;
        else if (canonical.compare(0, data.size(), data) == 0)
            aliases[2] = std::string(user0) + canonical.substr(data.size()) + aliasSuffix;
        storage = storagePath;
        return storage.size() < kMaxPath && aliases[0].size() < kMaxPath &&
               aliases[1].size() < kMaxPath && aliases[2].size() < kMaxPath;
    }
    Route Map(const char* input, std::array<char, kMaxPath>& output) const {
        if (!input || storage.empty()) return Route::Original;
        const auto length = strnlen(input, kMaxPath);
        if (length == kMaxPath) return Route::Invalid;
        const std::string_view path(input, length);
        for (const auto& root : aliases) {
            if (root.empty() || !Within(path, root)) continue;
            // A trailing slash is valid for directory queries. Reject traversal
            // instead of letting a virtual pack read a sibling or saved world.
            auto checked = path;
            if (checked.back() == '/') checked.remove_suffix(1);
            if (!CleanAbsolute(checked)) return Route::Invalid;
            const auto suffix = path.substr(root.size());
            if (storage.size() + suffix.size() >= output.size()) return Route::Invalid;
            std::memcpy(output.data(), storage.data(), storage.size());
            std::memcpy(output.data() + storage.size(), suffix.data(), suffix.size());
            output[storage.size() + suffix.size()] = '\0';
            return Route::Redirect;
        }
        return Route::Original;
    }
};
} // namespace resource_mount
