#ifndef OPENCDC_UTIL_TEMP_FILE_H
#define OPENCDC_UTIL_TEMP_FILE_H

// Portable private-temp-file creation.
// POSIX: mkstemps (atomic, 0600) + fchmod hardening.
// Windows: _sopen_s with O_CREAT|O_EXCL loop (atomic, _S_IREAD|_S_IWRITE).

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Keep function-like min/max macros out of every TU (see socket_compat.h).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace opencdc::util {

inline char path_separator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

// Preferred scratch directory: XDG_RUNTIME_DIR when set (0700 on Linux),
// else the OS temp directory.
inline std::string temp_directory() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg)
        return std::string(xdg);
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = ::GetTempPathA(static_cast<DWORD>(sizeof(buf)), buf);
    if (n > 0 && n < sizeof(buf))
        return std::string(buf);
    return ".";
#else
    return "/tmp";
#endif
}

// Creates a uniquely-named private file from a mkstemps-style template
// ("...XXXXXX<suffix>"). Returns the open fd (caller must close) and sets
// out_path; returns -1 on failure. No process-global umask games.
inline int create_private_temp_file(const std::string& tmpl_str, int suffix_len,
                                    std::string& out_path) {
#ifdef _WIN32
    static const char alnum[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937_64 rng(rd());
    size_t pos = tmpl_str.find("XXXXXX");
    if (pos == std::string::npos)
        return -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string candidate = tmpl_str;
        for (int i = 0; i < 6; ++i)
            candidate[pos + i] = alnum[rng() % (sizeof(alnum) - 1)];
        (void)suffix_len;
        int fd = -1;
        errno_t err = ::_sopen_s(&fd, candidate.c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                                 _SH_DENYRW, _S_IREAD | _S_IWRITE);
        if (err == 0 && fd != -1) {
            out_path = candidate;
            return fd;
        }
        if (err != EEXIST)
            return -1;
    }
    return -1;
#else
    std::string tmpl = tmpl_str;
    tmpl.push_back('\0');
    // mkstemps creates 0600; harden with fchmod instead of process-global
    // umask(0077) which races with other threads creating files.
    int fd = ::mkstemps(tmpl.data(), suffix_len);
    if (fd < 0)
        return -1;
    (void)::fchmod(fd, S_IRUSR | S_IWUSR);
    out_path = tmpl.data();
    return fd;
#endif
}

inline void close_temp_fd(int fd) {
#ifdef _WIN32
    ::_close(fd);
#else
    ::close(fd);
#endif
}

inline FILE* fdopen_temp(int fd, const char* mode) {
#ifdef _WIN32
    return ::_fdopen(fd, mode);
#else
    return ::fdopen(fd, mode);
#endif
}

// Stable per-process id for temp names (where the pid is only a uniqueness
// aid, not a security boundary).
inline long current_process_id() {
#ifdef _WIN32
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

// Unique scratch path (file is NOT created). Combines temp dir, prefix, pid,
// and randomness so parallel test processes never collide.
inline std::string unique_temp_path(const std::string& prefix, const std::string& suffix = "") {
    static const char alnum[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::string name;
    for (int i = 0; i < 8; ++i)
        name += alnum[rng() % (sizeof(alnum) - 1)];
    std::string dir = temp_directory();
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += path_separator();
    return dir + prefix + std::to_string(current_process_id()) + "_" + name + suffix;
}

}  // namespace opencdc::util

#endif  // OPENCDC_UTIL_TEMP_FILE_H
