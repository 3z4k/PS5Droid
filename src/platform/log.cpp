// PS5Droid - logging implementation (portable).
//
// Default sink writes to stderr. On PS5 payloads stderr is observable through
// the klogsrv payload or the ELF loader console; see docs/development.md.
#include "platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ps5droid {
namespace platform {

namespace {

LogSink g_sink = nullptr;
void* g_sink_user = nullptr;

void default_sink(void* /*user*/, LogLevel level, const char* tag, const char* message) {
    static const char* kNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    int idx = static_cast<int>(level);
    if (idx < 0 || idx > 3)
        idx = 3;
    fprintf(stderr, "[%s] %s: %s\n", kNames[idx], tag ? tag : "ps5droid", message ? message : "");
}

} // namespace

void set_log_sink(LogSink sink, void* user) {
    g_sink = sink;
    g_sink_user = user;
    if (g_sink == nullptr) {
        g_sink = default_sink;
        g_sink_user = nullptr;
    }
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    LogSink sink = g_sink ? g_sink : default_sink;
    void* user = g_sink ? g_sink_user : nullptr;
    sink(user, level, tag, buffer);
}

const char* backend_name() {
#if defined(PS5DROID_PLATFORM_PS5)
    return "ps5";
#else
    return "posix";
#endif
}

} // namespace platform
} // namespace ps5droid
