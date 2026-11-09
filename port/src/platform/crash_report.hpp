#pragma once

namespace dc::platform {

/**
 * Has a crash write, to stderr, what it was, the address it happened at and the stack that led there, named as hitch
 * reports name them, after flushing the game's buffered stdout (whose last lines say what it was doing). The process
 * then dies as it would have: Windows reports it, Linux leaves its core dump. Any thread's fault, illegal instruction or
 * division trap counts, and an abort (a PS2_UNIMPLEMENTED stub, or an uncaught exception). An overflowed stack is reported
 * only on the installing thread, without names on Windows.
 */
void CrashReportInstall();

} // namespace dc::platform
