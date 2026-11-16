#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sc::core {

// Two-level key lookup: "group.key" -> langObj[group][key], falling back to
// the built-in en-US dictionary, then the key path itself.
class I18n {
public:
    static I18n* get();

    void init(const std::string& langCode = {});
    void setLanguage(const std::string& langCode);

    std::string get(const std::string& keyPath) const;
    const std::string& language() const noexcept { return langCode_; }

private:
    I18n() = default;
    std::string langCode_{"zh-CN"};
    std::unordered_map<std::string, std::string> dict_;
    std::unordered_map<std::string, std::string> fallback_;
    std::filesystem::path langDir_;

    bool loadFile(const std::filesystem::path& path,
                  std::unordered_map<std::string, std::string>& out);
};

} // namespace sc::core
