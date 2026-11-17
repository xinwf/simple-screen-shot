#include "screencapture/platform/single_instance.h"
#include <glib.h>
#include <signal.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <functional>

namespace sc::platform {

namespace {

std::string exePath() {
    gchar* p = g_file_read_link("/proc/self/exe", nullptr);
    std::string s = p ? p : "";
    g_free(p);
    return s;
}

// One instance file per binary path: the development build and the
// installed build can legitimately run side by side.
std::string pidFile() {
    size_t h = std::hash<std::string>{}(exePath());
    return std::string(g_get_home_dir()) + "/.config/screencapture/instance-" +
           std::to_string(h) + ".pid";
}

bool pidIsSameBinary(int pid) {
    GError* err = nullptr;
    gchar* p = g_file_read_link(("/proc/" + std::to_string(pid) + "/exe").c_str(), &err);
    std::string target = p ? p : "";
    if (err) g_error_free(err);
    g_free(p);
    constexpr std::string_view kDeleted = " (deleted)";
    if (target.size() > kDeleted.size() &&
        target.compare(target.size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0) {
        target.resize(target.size() - kDeleted.size());
    }
    return !target.empty() && target == exePath();
}

} // namespace

bool anotherInstanceRunning() {
    std::ifstream f(pidFile());
    int pid = 0;
    if (!(f >> pid) || pid <= 0) return false;
    if (pid == static_cast<int>(getpid())) return false;   // our own stale entry
    if (!pidIsSameBinary(pid)) return false;               // stale pid or another binary
    return kill(pid, SIGUSR1) == 0;                        // ask it to show itself
}

void publishInstancePid() {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(pidFile()).parent_path(), ec);
    std::ofstream f(pidFile(), std::ios::trunc);
    f << getpid();
}

void clearInstancePid() {
    std::error_code ec;
    std::filesystem::remove(pidFile(), ec);
}

} // namespace sc::platform
