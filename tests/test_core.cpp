#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include <cassert>
#include <cstdio>
#include <filesystem>

using sc::core::ConfigManager;
using sc::core::I18n;

void testConfigDefaults() {
    auto* cfg = ConfigManager::get();
    cfg->init();

    // Default values
    assert(cfg->getShortcutKey("cap") == "Ctrl+Alt+A");
    assert(!cfg->getLang().empty());
    assert(!cfg->getOcrRapidocrHintHidden());
}

void testConfigReadWrite() {
    auto* cfg = ConfigManager::get();
    cfg->setBool("test_group", "bool_key", true);
    assert(cfg->getBool("test_group", "bool_key", false) == true);

    cfg->setInt("test_group", "int_key", 42);
    assert(cfg->getInt("test_group", "int_key", 0) == 42);

    cfg->setString("test_group", "str_key", "hello_world");
    assert(cfg->getString("test_group", "str_key") == "hello_world");

    // Missing key with fallback
    assert(cfg->getString("test_group", "non_existing", "fallback") == "fallback");
    assert(cfg->getInt("test_group", "non_existing_int", 99) == 99);
}

void testI18n() {
    auto* i18n = I18n::get();
    i18n->init("zh-CN");
    assert(i18n->get("tray.capture") == "截图");
    assert(i18n->get("about.sysTip") == "系统提示");

    i18n->setLanguage("en-US");
    assert(i18n->get("tray.capture") == "Capture");
    assert(i18n->get("about.sysTip") == "Information");

    // Missing key fallback to key name
    assert(i18n->get("some.unknown.key") == "some.unknown.key");
}

int main() {
    // Isolate unit tests with a private temporary HOME
    std::string tmpHome = (std::filesystem::temp_directory_path() / "sc_test_home_XXXXXX").string();
    std::vector<char> buf(tmpHome.begin(), tmpHome.end());
    buf.push_back('\0');
    if (char* created = mkdtemp(buf.data())) {
        setenv("HOME", created, 1);
    }

    testConfigDefaults();
    testConfigReadWrite();
    testI18n();

    if (!tmpHome.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(buf.data(), ec);
    }

    std::printf("core tests: all tests passed\n");
    return 0;
}
