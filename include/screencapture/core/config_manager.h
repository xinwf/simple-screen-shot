#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <json/json.hpp>

namespace sc::core {

// Portable config strategy: prefer <exe_dir>/config.json if it already exists,
// otherwise fall back to ~/.config/screencapture/config.json (XDG).
class ConfigManager {
public:
    static ConfigManager* get();

    void init();
    void save();

    const std::filesystem::path& dataPath() const noexcept { return dataPath_; }
    const std::filesystem::path& configPath() const noexcept { return configPath_; }
    const std::filesystem::path& exeDir() const noexcept { return exeDir_; }

    bool getBool(const std::string& group, const std::string& key, bool def = false) const;
    int getInt(const std::string& group, const std::string& key, int def = 0) const;
    float getFloat(const std::string& group, const std::string& key, float def = 0.f) const;
    std::string getString(const std::string& group, const std::string& key,
                          const std::string& def = {}) const;

    void setBool(const std::string& group, const std::string& key, bool value);
    void setInt(const std::string& group, const std::string& key, int value);
    void setFloat(const std::string& group, const std::string& key, float value);
    void setString(const std::string& group, const std::string& key, const std::string& value);

    // Convenience for tool state stored under toolPin.<tool>.<key>
    std::string getToolString(const std::string& tool, const std::string& key,
                              const std::string& def = {}) const {
        return getString("toolPin." + tool, key, def);
    }
    void setToolString(const std::string& tool, const std::string& key, const std::string& value) {
        setString("toolPin." + tool, key, value);
    }
    bool getToolBool(const std::string& tool, const std::string& key, bool def = false) const {
        return getBool("toolPin." + tool, key, def);
    }
    void setToolBool(const std::string& tool, const std::string& key, bool value) {
        setBool("toolPin." + tool, key, value);
    }
    float getToolFloat(const std::string& tool, const std::string& key, float def = 0.f) const {
        return getFloat("toolPin." + tool, key, def);
    }
    void setToolFloat(const std::string& tool, const std::string& key, float value) {
        setFloat("toolPin." + tool, key, value);
    }

    nlohmann::json& raw() { return *obj_; }

    // language code shortcut
    std::string getLang() const { return getString("common", "language", "zh-CN"); }
    void setLang(const std::string& code) { setString("common", "language", code); }
    bool getAutoStart() const { return getBool("common", "autoStart", false); }
    void setAutoStart(bool v) { setBool("common", "autoStart", v); }
    std::string getShortcutKey(const std::string& name = "cap") const {
        return getString("shortcutKey", name, "Ctrl+Alt+A");
    }
    void setShortcutKey(const std::string& name, const std::string& value) {
        setString("shortcutKey", name, value);
    }

    // OCR plugin override; empty = bundled assets/plugins/ocr_rapidocr.py, then tesseract.
    std::string getOcrPlugin() const { return getString("ocr", "plugin", {}); }
    void setOcrPlugin(const std::string& path) { setString("ocr", "plugin", path); }

    // Startup reminder when RapidOCR is unavailable (falling back to tesseract).
    bool getOcrRapidocrHintHidden() const { return getBool("ocr", "rapidocrHintHidden", false); }
    void setOcrRapidocrHintHidden(bool v) { setBool("ocr", "rapidocrHintHidden", v); }

private:
    ConfigManager() = default;
    std::filesystem::path exeDir_;
    std::filesystem::path dataPath_;
    std::filesystem::path configPath_;
    std::unique_ptr<nlohmann::json> obj_;

    nlohmann::json* navigate(const std::string& dottedPath, bool create);
    const nlohmann::json* navigate(const std::string& dottedPath) const;
};

} // namespace sc::core
