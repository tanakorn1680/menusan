#pragma once
// Breadcrumbs.  When the game dies, the Android crash log only says WHERE in the code it died,
// not WHAT the menu was doing.  Crumb() keeps the last few steps on disk (rewritten on every
// call, so it is complete even if the process is killed a moment later):
//     Android/data/com.rockstargames.gtasa/files/ProMenu/last_action.txt
// Send that file together with the crash log.
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mod/logger.h>

namespace Diag {

static const int kLines = 14, kLen = 120;
static char g_lines[kLines][kLen];
static int  g_n = 0;                                  // total lines ever written
static const char* kDirs[2] = {
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/ProMenu",
    "/sdcard/Android/data/com.rockstargames.gtasa/files/ProMenu",
};

// Call once when the game starts: keeps the previous session's file as previous_session.txt, so the
// breadcrumbs of a crashed run are still there after the game has been restarted.
static void KeepPreviousSession() {
    for (int d = 0; d < 2; d++) {
        char from[256], to[256];
        snprintf(from, sizeof(from), "%s/last_action.txt", kDirs[d]);
        snprintf(to, sizeof(to), "%s/previous_session.txt", kDirs[d]);
        if (rename(from, to) == 0) return;
    }
}

static void Flush() {
    for (int d = 0; d < 2; d++) {
        mkdir(kDirs[d], 0777);
        char path[256];
        snprintf(path, sizeof(path), "%s/last_action.txt", kDirs[d]);
        const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) continue;
        const int first = g_n > kLines ? g_n - kLines : 0;
        for (int i = first; i < g_n; i++) {
            const char* s = g_lines[i % kLines];
            if (write(fd, s, strlen(s)) < 0) break;
        }
        close(fd);
        return;
    }
}

static void Crumb(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Crumb(const char* fmt, ...) {
    char* line = g_lines[g_n % kLines];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, kLen - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n > kLen - 2) n = kLen - 2;
    line[n] = '\n'; line[n + 1] = 0;
    logger->Info("%.*s", n, line);
    g_n++;
    Flush();
}

} // namespace Diag
