#pragma once

// The sound's trace: what the game asks of its sound, line by line, where DC_AUDIO_TRACE=1 asks for it (docs/PC.md).

namespace dc::sound {

/** Whether the trace is on. */
bool TraceOn();

/** A line of the trace, printf's way, at most 255 characters. Nothing where it's off. */
[[gnu::format(printf, 1, 2)]] void Trace(const char *format, ...);

} // namespace dc::sound
