#include "screencapture/core/config_manager.h"
#include <json/json.hpp>
#include <fstream>
#include <unistd.h>
#include <limits.h>

namespace sc::core {

namespace {
std::filesystem::path currentExeDir() {
    char buf[PATH_MAX]{};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path homeDir() {
    const char* h = std::getenv("HOME");
    return h ? std::filesystem::path(h) : std::filesystem::path();
}
} // namespace

ConfigManager* ConfigManager::get() { static ConfigManager instance; return &instance; }

void ConfigManager::init() {
    exeDir_ = currentExeDir();
    dataPath_ = homeDir() / ".config" / "screencapture";
    std::error_code ec;
    std::filesystem::create_directories(dataPath_, ec);

    auto portable = exeDir_ / "config.json";
    configPath_ = std::filesystem::exists(portable) ? portable : (dataPath_ / "config.json");

    obj_ = std::make_unique<nlohmann::json>();
    *obj_ = {
        {"common", {{"autoStart", false}, {"language", "zh-CN"}}},
        {"shortcutKey", {{"cap", "Ctrl+Alt+A"}}},
    };

    std::ifstream ifs(configPath_);
    if (ifs.good()) {
        try {
            nlohmann::json parsed;
            ifs >> parsed;
            if (parsed.is_object()) {
                // merge: defaults for missing keys
                for (auto& [k, v] : parsed.items()) {
                    if ((*obj_).contains(k) && (*obj_)[k].is_object() && v.is_object()) {
                        for (auto& [k2, v2] : v.items()) (*obj_)[k][k2] = v2;
                    } else {
                        (*obj_)[k] = v;
                    }
                }
            }
        } catch (const std::exception&) {
            // keep defaults on parse error
        }
    }
}

void ConfigManager::save() {
    if (!obj_) return;
    std::error_code ec;
    std::filesystem::create_directories(configPath_.parent_path(), ec);
    auto tmpPath = configPath_;
    tmpPath += ".tmp";
    {
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (!ofs.good()) return;
        ofs << obj_->dump(2);
        ofs.flush();
        if (!ofs.good()) return;
    }
    std::filesystem::rename(tmpPath, configPath_, ec);
}

nlohmann::json* ConfigManager::navigate(const std::string& dottedPath, bool create) {
    if (!obj_) return nullptr;
    nlohmann::json* cur = obj_.get();
    size_t start = 0;
    while (start <= dottedPath.size()) {
        size_t dot = dottedPath.find('.', start);
        std::string seg = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (seg.empty()) break;
        if (!cur->is_object()) return nullptr;
        if (!cur->contains(seg)) {
            if (!create) return nullptr;
            (*cur)[seg] = nlohmann::json::object();
        }
        cur = &(*cur)[seg];
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

const nlohmann::json* ConfigManager::navigate(const std::string& dottedPath) const {
    return const_cast<ConfigManager*>(this)->navigate(dottedPath, false);
}

bool ConfigManager::getBool(const std::string& g, const std::string& k, bool def) const {
    const auto* node = navigate(g + "." + k);
    return node && node->is_boolean() ? node->get<bool>() : def;
}
int ConfigManager::getInt(const std::string& g, const std::string& k, int def) const {
    const auto* node = navigate(g + "." + k);
    return node && node->is_number() ? node->get<int>() : def;
}
float ConfigManager::getFloat(const std::string& g, const std::string& k, float def) const {
    const auto* node = navigate(g + "." + k);
    return node && node->is_number() ? node->get<float>() : def;
}
std::string ConfigManager::getString(const std::string& g, const std::string& k,
                                     const std::string& def) const {
    const auto* node = navigate(g + "." + k);
    return node && node->is_string() ? node->get<std::string>() : def;
}
void ConfigManager::setBool(const std::string& g, const std::string& k, bool v) {
    if (auto* n = navigate(g + "." + k, true)) *n = v;
    save();
}
void ConfigManager::setInt(const std::string& g, const std::string& k, int v) {
    if (auto* n = navigate(g + "." + k, true)) *n = v;
    save();
}
void ConfigManager::setFloat(const std::string& g, const std::string& k, float v) {
    if (auto* n = navigate(g + "." + k, true)) *n = v;
    save();
}
void ConfigManager::setString(const std::string& g, const std::string& k, const std::string& v) {
    if (auto* n = navigate(g + "." + k, true)) *n = v;
    save();
}

} // namespace sc::core
