#include "screencapture/core/i18n.h"
#include "screencapture/core/config_manager.h"
#include <json/json.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace sc::core {

namespace {
void populateBuiltinZhCN(std::unordered_map<std::string, std::string>& m) {
    m = {
        {"cap.mark", "图像标记"},
        {"cap.long", "截长图"},
        {"cap.video", "录屏"},
        {"cap.ocr", "文字识别"},
        {"cap.qrcode", "二维码识别"},
        {"cap.qrcodeEmpty", "未识别到二维码"},
        {"cap.qrcodeCopy", "二维码内容已写入剪贴板"},
        {"cap.pin", "钉在桌面"},
        {"tray.about", "关于"},
        {"tray.delayN", "延迟 {n} 秒"},
        {"tray.delayCustom", "自定义延迟…"},
        {"tray.exit", "退出"},
        {"tool.rect", "矩形"},
        {"tool.ellipse", "圆形"},
        {"tool.arrow", "箭头"},
        {"tool.number", "标号"},
        {"tool.line", "画笔/线条"},
        {"tool.text", "文本"},
        {"tool.mosaic", "马赛克"},
        {"tool.eraser", "橡皮擦"},
        {"tool.undo", "撤销 (Ctrl+Z)"},
        {"tool.redo", "重做 (Ctrl+Y)"},
        {"tool.pin", "钉在桌面"},
        {"tool.clipboard", "复制到剪贴板 (Ctrl+C)"},
        {"tool.save", "保存文件 (Ctrl+S)"},
        {"tool.close", "退出 (Esc)"},
        {"tool.rectFill", "矩形填充"},
        {"tool.ellipseFill", "圆形填充"},
        {"tool.arrowFill", "箭头填充"},
        {"tool.numberFill", "标号填充"},
        {"tool.semiTransparent", "半透明荧光笔"},
        {"tool.bold", "粗体"},
        {"tool.italic", "斜体"},
        {"color.red", "红"},
        {"color.yellow", "黄"},
        {"color.green", "绿"},
        {"color.cyan", "青"},
        {"color.blue", "蓝"},
        {"color.purple", "紫"},
        {"color.pink", "粉"},
        {"color.black", "黑"},
        {"color.white", "白"},
        {"video.outputMp4", "输出MP4格式文件"},
        {"video.outputGif", "输出GIF格式文件"},
        {"video.recordSystem", "录制系统声音"},
        {"video.recordMic", "录制麦克风声音"},
        {"video.startRecord", "开始录制"},
        {"video.exit", "退出"},
        {"video.stopClipboard", "停止录制，并保存到剪贴板"},
        {"video.stopFile", "停止录制，并保存到文件"},
        {"video.stopExit", "停止录制，并退出"},
        {"video.recordFailed", "屏幕录制失败：请确认已安装 ffmpeg"},
        {"setting.title", "ScreenCapture设置"},
        {"about.version", "版本："},
        {"about.author", "作者："},
        {"about.language", "界面语言："},
        {"about.hotkey", "截图快捷键："},
        {"about.copySuccess", "复制成功"},
        {"about.sysTip", "系统提示"},
        {"setting.common", "通用设置"},
        {"setting.shortcut", "快捷键"},
        {"setting.about", "关于"},
        {"setting.autoStart", "开机自启动："},
        {"setting.language", "语言："},
        {"setting.getMoreLang", "获取更多语言"},
        {"shortcut.cap", "启动应用："},
        {"shortcut.pressKey", "请按键..."},
        {"long.start", "开始"},
        {"long.reachedBottom", "已触底，截图停止"},
        {"long.tooLong", "图像过长，截图停止"},
        {"util.file", "文件"},
        {"delay.title", "延迟截图"},
        {"delay.seconds", "延迟秒数(D)"},
        {"delay.start", "截图"},
        {"delay.countdown", "{n} 秒后截图"},
        {"cap.ocrSuccess", "文字识别成功（已复制到剪贴板）："},
        {"cap.ocrEmpty", "未识别到文字（请确认选区包含清晰文字）。"},
        {"cap.ocrMissing", "未检测到 OCR 工具 tesseract：\n请安装 tesseract-ocr 与中文语言包：\nsudo apt install tesseract-ocr tesseract-ocr-chi-sim"},
        {"save.cancel", "取消(C)"},
        {"save.copy", "复制到剪贴板(O)"},
        {"save.save", "保存(S)"},
        {"save.name", "名称(N)："},
        {"save.folder", "目录(F)："},
        {"save.browse", "浏览…"},
        {"save.screenshotName", "屏幕截图"},
    };
}

void populateBuiltinEnUS(std::unordered_map<std::string, std::string>& m) {
    m = {
        {"cap.mark", "Annotate"},
        {"cap.long", "Scrolling Screenshot"},
        {"cap.video", "Screen Recording"},
        {"cap.ocr", "OCR Text Recognition"},
        {"cap.qrcode", "Scan QR Code"},
        {"cap.qrcodeEmpty", "No QR code detected"},
        {"cap.qrcodeCopy", "QR code content copied to clipboard"},
        {"cap.pin", "Pin to Screen"},
        {"tray.about", "About"},
        {"tray.delayN", "Delay {n} s"},
        {"tray.delayCustom", "Custom delay…"},
        {"tray.exit", "Exit"},
        {"tool.rect", "Rectangle"},
        {"tool.ellipse", "Ellipse"},
        {"tool.arrow", "Arrow"},
        {"tool.number", "Number Tag"},
        {"tool.line", "Pen / Highlighter"},
        {"tool.text", "Text"},
        {"tool.mosaic", "Mosaic"},
        {"tool.eraser", "Eraser"},
        {"tool.undo", "Undo (Ctrl+Z)"},
        {"tool.redo", "Redo (Ctrl+Y)"},
        {"tool.pin", "Pin to Screen"},
        {"tool.clipboard", "Copy to Clipboard (Ctrl+C)"},
        {"tool.save", "Save to File (Ctrl+S)"},
        {"tool.close", "Close (Esc)"},
        {"tool.rectFill", "Filled Rectangle"},
        {"tool.ellipseFill", "Filled Ellipse"},
        {"tool.arrowFill", "Filled Arrow"},
        {"tool.numberFill", "Filled Number"},
        {"tool.semiTransparent", "Semi-transparent"},
        {"tool.bold", "Bold"},
        {"tool.italic", "Italic"},
        {"color.red", "Red"},
        {"color.yellow", "Yellow"},
        {"color.green", "Green"},
        {"color.cyan", "Cyan"},
        {"color.blue", "Blue"},
        {"color.purple", "Purple"},
        {"color.pink", "Pink"},
        {"color.black", "Black"},
        {"color.white", "White"},
        {"video.outputMp4", "Output MP4 video"},
        {"video.outputGif", "Output GIF animation"},
        {"video.recordSystem", "Record system audio"},
        {"video.recordMic", "Record microphone"},
        {"video.startRecord", "Start recording"},
        {"video.exit", "Exit"},
        {"video.stopClipboard", "Stop recording & copy to clipboard"},
        {"video.stopFile", "Stop recording & save to file"},
        {"video.stopExit", "Stop recording & discard"},
        {"video.recordFailed", "Screen recording failed: please check ffmpeg"},
        {"setting.title", "ScreenCapture Settings"},
        {"setting.common", "General"},
        {"setting.shortcut", "Shortcuts"},
        {"setting.about", "About"},
        {"setting.autoStart", "Launch on startup:"},
        {"setting.language", "Language:"},
        {"setting.getMoreLang", "More languages"},
        {"about.version", "Version:"},
        {"about.author", "Author:"},
        {"about.language", "UI language: "},
        {"about.hotkey", "Capture hotkey: "},
        {"about.copySuccess", "Copied"},
        {"about.sysTip", "Notice"},
        {"shortcut.cap", "Start Capture:"},
        {"shortcut.pressKey", "Press key..."},
        {"long.start", "Start"},
        {"long.reachedBottom", "Reached bottom, stopped"},
        {"long.tooLong", "Image too tall, stopped"},
        {"util.file", "File"},
        {"delay.title", "Delayed Screenshot"},
        {"delay.seconds", "Delay seconds (D)"},
        {"delay.start", "Capture"},
        {"delay.countdown", "Capturing in {n}s"},
        {"cap.ocrSuccess", "OCR succeeded (copied to clipboard):"},
        {"cap.ocrEmpty", "No text recognized (make sure the selection contains clear text)."},
        {"cap.ocrMissing", "OCR tool tesseract not found:\nInstall it with the Chinese pack:\nsudo apt install tesseract-ocr tesseract-ocr-chi-sim"},
        {"save.cancel", "Cancel (C)"},
        {"save.copy", "Copy to clipboard (O)"},
        {"save.save", "Save (S)"},
        {"save.name", "Name: "},
        {"save.folder", "Folder: "},
        {"save.browse", "Browse…"},
        {"save.screenshotName", "Screenshot"},
    };
}
} // namespace

I18n* I18n::get() { static I18n instance; return &instance; }

void I18n::init(const std::string& langCode) {
    auto cfg = ConfigManager::get();
    std::vector<std::filesystem::path> candidates = {
        cfg->exeDir() / "assets" / "lang",
        cfg->exeDir().parent_path() / "assets" / "lang",
        cfg->exeDir().parent_path() / "share" / "simplescreenshot" / "assets" / "lang",
        std::filesystem::current_path() / "assets" / "lang",
        cfg->dataPath() / "assets" / "lang"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::is_directory(c)) {
            langDir_ = c;
            break;
        }
    }
    setLanguage(langCode.empty() ? cfg->getLang() : langCode);
    // Persist the normalized code so stale values (e.g. removed languages)
    // don't linger in the config file.
    if (langCode_ != cfg->getLang()) cfg->setLang(langCode_);
}

bool I18n::loadFile(const std::filesystem::path& path,
                    std::unordered_map<std::string, std::string>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) return false;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty()) return false;

    try {
        nlohmann::json root = nlohmann::json::parse(raw);
        if (!root.is_object()) return false;
        for (auto& [group, obj] : root.items()) {
            if (!obj.is_object()) continue;
            for (auto& [key, val] : obj.items()) {
                if (val.is_string()) {
                    out[group + "." + key] = val.get<std::string>();
                }
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void I18n::setLanguage(const std::string& langCode) {
    // Only zh-CN and en-US are supported; unknown codes fall back to zh-CN.
    langCode_ = (langCode == "en-US") ? "en-US" : "zh-CN";
    dict_.clear();
    fallback_.clear();

    populateBuiltinEnUS(fallback_);
    if (langCode_ == "zh-CN") {
        populateBuiltinZhCN(dict_);
    } else {
        dict_ = fallback_;
    }

    if (!langDir_.empty() && std::filesystem::is_directory(langDir_)) {
        loadFile(langDir_ / (langCode_ + ".json"), dict_);
    }

    // Ensure "cap.pin" and "tool.pin" always exist
    if (dict_.find("cap.pin") == dict_.end() && dict_.find("tool.pin") != dict_.end()) {
        dict_["cap.pin"] = dict_["tool.pin"];
    }
}

std::string I18n::get(const std::string& keyPath) const {
    if (auto it = dict_.find(keyPath); it != dict_.end() && !it->second.empty()) {
        return it->second;
    }
    if (auto it = fallback_.find(keyPath); it != fallback_.end() && !it->second.empty()) {
        return it->second;
    }
    return keyPath;
}

} // namespace sc::core
