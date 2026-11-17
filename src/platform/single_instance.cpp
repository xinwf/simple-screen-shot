#include "screencapture/platform/single_instance.h"
#include <glib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
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

// Held for the whole process lifetime: the kernel drops it automatically
// when we die (even on a kill -9), which is exactly the single-instance
// guarantee — no stale pid files can mislead a later launch.
int gLockFd = -1;

} // namespace

bool anotherInstanceRunning() {
    const std::string path = pidFile();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) return false;

    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        // Primary instance: record our pid (the second launch only ever
        // signals the process that provably holds the lock) and keep the
        // fd open until exit. Replaces the old read-check-write dance,
        // which let two simultaneous launches both pass the check.
        gLockFd = fd;
        if (::ftruncate(fd, 0) == 0) {
            const std::string pid = std::to_string(getpid());
            ssize_t n = ::write(fd, pid.data(), pid.size());
            (void)n;
        }
        return false;
    }

    // Lock failure proves a live holder — return true unconditionally.
    // The pid file may still be empty (holder just took the lock and has
    // not written its pid yet); gating the answer on reading it would let
    // a simultaneous second launch slip through, defeating the flock.
    // The pid check only guards the optional SIGUSR1 "show yourself" nudge.
    std::ifstream f(path);
    int pid = 0;
    if ((f >> pid) && pid > 0 && pidIsSameBinary(pid)) {
        kill(pid, SIGUSR1);            // ask it to show its delay dialog
    }
    ::close(fd);
    return true;
}

void publishInstancePid() {
    // The lock (and pid record) was already taken inside
    // anotherInstanceRunning(); kept as a separate entry point for callers
    // that skip the check, and as documentation of who owns the fd.
    if (gLockFd < 0) anotherInstanceRunning();
}

void clearInstancePid() {
    if (gLockFd >= 0) {
        std::error_code ec;
        std::filesystem::remove(pidFile(), ec);
        ::close(gLockFd);
        gLockFd = -1;
    }
}

} // namespace sc::platform
