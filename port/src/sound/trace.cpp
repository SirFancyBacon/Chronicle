#include "sound/trace.hpp"

#include <cstdarg>
#include <cstdio>

#include "kazarov/log.hpp"
#include "platform/settings.hpp"

namespace dc::sound {

bool TraceOn() {
    static const bool on = [] {
        const char *asked = SettingsEnv("AUDIO_TRACE");
        return asked != nullptr && *asked != '\0' && *asked != '0';
    }();
    return on;
}

void Trace(const char *format, ...) {
    if (!TraceOn()) {
        return;
    }
    char    line[256];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    kz::Log(kz::LogLevel::Debug, "audio", line);
}

} // namespace dc::sound
