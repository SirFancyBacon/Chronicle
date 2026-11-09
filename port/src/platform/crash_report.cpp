#include "platform/crash_report.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "platform/hitch_sampler.hpp"

#if defined(_WIN32) && defined(__x86_64__)
#define DC_CRASH_REPORT_WINDOWS 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <csignal>
#elif defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define DC_CRASH_REPORT_LINUX 1
#include <csignal>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#endif

namespace dc::platform {

namespace {

constexpr std::size_t kFrames = 48;

using Frames = std::array<std::uintptr_t, kFrames>;

// Names come after the raw addresses: naming reads the executable, which a crash in the heap may not survive.
void WriteNames(const Frames &frames, std::size_t count) {
    for (std::size_t i = 0; i < count; i++) {
        std::fprintf(stderr, "crash:   %s\n", CodeAddressName(frames[i]).c_str());
    }
    std::fflush(stderr);
}

#if DC_CRASH_REPORT_WINDOWS

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
std::atomic<DWORD>           g_reporter = 0; // the thread writing the report

// The module an address is in and the offset into it, without the heap.
void WriteRaw(std::uintptr_t address) {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address),
                           &module) &&
        module != nullptr && GetModuleFileNameW(module, path, MAX_PATH) != 0) {
        const wchar_t *file = path;
        for (const wchar_t *at = path; *at != L'\0'; at++) {
            file = *at == L'\\' || *at == L'/' ? at + 1 : file;
        }
        std::fprintf(stderr, " %ls+0x%llx", file, static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
    } else {
        std::fprintf(stderr, " 0x%llx", static_cast<unsigned long long>(address));
    }
}

// Claims the report for this thread. Another thread crashing meanwhile waits for it a while, so that its own death
// doesn't cut the report short; a crash in the report itself (this thread again) goes straight on.
bool Claim() {
    const DWORD me = GetCurrentThreadId();
    DWORD       held = 0;
    if (g_reporter.compare_exchange_strong(held, me)) {
        return true;
    }
    if (held != me) {
        for (int i = 0; i < 500 && g_reporter.load() != 0; i++) {
            Sleep(10);
        }
    }
    return false;
}

// The stack from the context on, unwound on the stack itself as far as it stays within the thread's, raw and then
// named (unless the stack overflowed: it has no room left to read the executable's names in).
void WriteStack(CONTEXT context, bool name) {
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    const auto  low = reinterpret_cast<DWORD64>(tib->StackLimit);
    const auto  high = reinterpret_cast<DWORD64>(tib->StackBase);
    Frames      frames{};
    std::size_t count = 0;
    while (count < kFrames && context.Rip != 0 && context.Rsp >= low && context.Rsp + 8 <= high) {
        frames[count++] = context.Rip;
        DWORD64           base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (function == nullptr) {
            // A leaf, or a call to nowhere: nothing moved the stack, which holds the return address.
            context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
            context.Rsp += 8;
        } else {
            PVOID   handler_data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
        }
    }
    std::fprintf(stderr, "crash: stack");
    for (std::size_t i = 0; i < count; i++) {
        WriteRaw(frames[i]);
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    if (name) {
        WriteNames(frames, count);
    }
}

LONG WINAPI Report(EXCEPTION_POINTERS *info) {
    if (Claim()) {
        std::fflush(stdout);
        const EXCEPTION_RECORD &record = *info->ExceptionRecord;
        std::fprintf(stderr, "crash: exception 0x%08lx on thread %lu at", static_cast<unsigned long>(record.ExceptionCode), GetCurrentThreadId());
        WriteRaw(reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
            const ULONG_PTR how = record.ExceptionInformation[0];
            std::fprintf(stderr, ", %s 0x%llx", how == 0 ? "reading" : how == 1 ? "writing" : "executing",
                         static_cast<unsigned long long>(record.ExceptionInformation[1]));
        }
        std::fprintf(stderr, "\n");
        WriteStack(*info->ContextRecord, record.ExceptionCode != EXCEPTION_STACK_OVERFLOW);
        g_reporter.store(0);
    }
    return g_previous != nullptr ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

// abort() (a PS2_UNIMPLEMENTED stub, or std::terminate's) never reaches the exception filter: the CRT raises SIGABRT
// and ends the process itself once its handler returns.
void ReportAbort(int) {
    if (Claim()) {
        std::fflush(stdout);
        std::fprintf(stderr, "crash: abort on thread %lu\n", GetCurrentThreadId());
        CONTEXT context{};
        RtlCaptureContext(&context);
        WriteStack(context, true);
        g_reporter.store(0);
    }
}

void Install() {
    // Room on the installing thread's stack for the report when the stack itself overflowed.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    g_previous = SetUnhandledExceptionFilter(Report);
    std::signal(SIGABRT, ReportAbort);
}

#elif DC_CRASH_REPORT_LINUX

constexpr std::array kSignals = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

// For the installing thread, so that its overflowed stack still has somewhere to report from.
alignas(16) std::array<unsigned char, 64 * 1024> g_signal_stack;

// The signal being reported, for the watchdog to end the process with.
volatile std::sig_atomic_t g_signal = 0;

const char *SignalName(int signal) {
    switch (signal) {
        case SIGSEGV: return "Segmentation fault";
        case SIGBUS: return "Bus error";
        case SIGFPE: return "Floating point exception";
        case SIGILL: return "Illegal instruction";
        case SIGABRT: return "Aborted";
        default: return "Signal";
    }
}

// Formatted on the stack and written straight to the file: the heap and stdio's locks may be what crashed.
void WriteSafe(const char *text) {
    const std::size_t size = std::strlen(text);
    for (std::size_t done = 0; done < size;) {
        const ssize_t wrote = write(STDERR_FILENO, text + done, size - done);
        if (wrote <= 0) {
            return;
        }
        done += static_cast<std::size_t>(wrote);
    }
}

void AppendHex(char *&at, std::uintptr_t value) {
    char digits[2 * sizeof(value)];
    int  count = 0;
    do {
        digits[count++] = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value != 0);
    *at++ = ' ';
    *at++ = '0';
    *at++ = 'x';
    while (count > 0) {
        *at++ = digits[--count];
    }
}

// The thread writing the report, and whether it has; the executable's load address, for the raw frames.
std::atomic<pid_t>          g_reporter = 0;
std::atomic<bool>           g_reported = false;
std::uintptr_t              g_base = 0;

// The signal back to its default, then the death it would have been: a fault the kernel raised happens again on return,
// as itself, core dump and all; one sent (an abort) is sent again.
void Die(int signal, const siginfo_t *info) {
    struct sigaction fallback{};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(signal, &fallback, nullptr);
    if (info->si_code > 0 && signal != SIGABRT) {
        return;
    }
    raise(signal);
}

// Naming may wait forever on a lock the crash left held (the allocator's, after heap corruption): the process dies of
// its signal a few seconds on, on whatever the report has got to. Any thread may take the alarm; the signal kills them all.
void Watchdog(int) {
    struct sigaction fallback{};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(g_signal, &fallback, nullptr);
    raise(g_signal);
    _exit(128 + g_signal);
}

struct Unwinding {
    Frames        *frames;
    std::size_t    count;
    std::uintptr_t pc;
    bool           found;
};

_Unwind_Reason_Code Frame(_Unwind_Context *context, void *argument) {
    auto                *unwinding = static_cast<Unwinding *>(argument);
    const std::uintptr_t ip = _Unwind_GetIP(context);
    // The handler's own frames and the signal's come first.
    unwinding->found = unwinding->found || ip == unwinding->pc;
    if (unwinding->found) {
        (*unwinding->frames)[unwinding->count++] = ip;
    }
    return unwinding->count < kFrames ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void Handle(int signal, siginfo_t *info, void *context) {
    const auto me = static_cast<pid_t>(syscall(SYS_gettid));
    pid_t      held = 0;
    if (!g_reporter.compare_exchange_strong(held, me)) {
        // Another thread's report is being written: it's let finish. A crash in the report itself dies at once.
        for (int i = 0; held != me && i < 500 && !g_reported.load(); i++) {
            const timespec wait{.tv_sec = 0, .tv_nsec = 10'000'000};
            nanosleep(&wait, nullptr);
        }
        Die(signal, info);
        return;
    }
    g_signal = signal;
    struct sigaction watchdog{};
    watchdog.sa_handler = Watchdog;
    sigemptyset(&watchdog.sa_mask);
    sigaction(SIGALRM, &watchdog, nullptr);
    alarm(5);
    const auto *machine = &static_cast<ucontext_t *>(context)->uc_mcontext;
#if defined(__x86_64__)
    const auto pc = static_cast<std::uintptr_t>(machine->gregs[REG_RIP]);
#else
    const auto pc = static_cast<std::uintptr_t>(machine->pc);
#endif
    char  line[96 + 20 * kFrames];
    char *at = line;
    at = stpcpy(at, "crash: ");
    at = stpcpy(at, SignalName(signal));
    at = stpcpy(at, " at");
    AppendHex(at, pc);
    if (signal == SIGSEGV || signal == SIGBUS) {
        at = stpcpy(at, ", touching");
        AppendHex(at, reinterpret_cast<std::uintptr_t>(info->si_addr));
    }
    // addr2line takes the executable's addresses less this.
    at = stpcpy(at, ", the executable loaded at");
    AppendHex(at, g_base);
    *at++ = '\n';
    *at = '\0';
    WriteSafe(line);
    Frames    frames{};
    Unwinding unwinding{&frames, 0, pc, false};
    _Unwind_Backtrace(Frame, &unwinding);
    if (!unwinding.found) {
        frames[0] = pc;
        unwinding.count = 1;
    }
    at = stpcpy(line, "crash: stack");
    for (std::size_t i = 0; i < unwinding.count; i++) {
        AppendHex(at, frames[i]);
    }
    *at++ = '\n';
    *at = '\0';
    WriteSafe(line);
    std::fflush(stdout);
    WriteNames(frames, unwinding.count);
    alarm(0);
    g_reported = true;
    Die(signal, info);
}

void Install() {
    stack_t stack{};
    stack.ss_sp = g_signal_stack.data();
    stack.ss_size = g_signal_stack.size();
    sigaltstack(&stack, nullptr);
    Dl_info self{};
    if (dladdr(reinterpret_cast<void *>(&Install), &self) != 0) {
        g_base = reinterpret_cast<std::uintptr_t>(self.dli_fbase);
    }
    // Not reset as it runs: a second thread crashing meanwhile waits for the report rather than ending it.
    struct sigaction action{};
    action.sa_sigaction = Handle;
    action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : kSignals) {
        sigaction(signal, &action, nullptr);
    }
}

#else

void Install() {}

#endif

} // namespace

void CrashReportInstall() {
    static bool installed = false;
    if (!installed) {
        installed = true;
        Install();
    }
}

} // namespace dc::platform
