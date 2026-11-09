#include "platform/hitch_sampler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include <cxxabi.h>

#if defined(_WIN32) && defined(__x86_64__)
#define DC_HITCH_SAMPLER_WINDOWS 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#elif defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define DC_HITCH_SAMPLER_LINUX 1
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#endif

namespace dc::platform {

namespace {

constexpr int         kStackShown = 6;  // frames of a stack the report names, innermost first
constexpr std::size_t kStacksShown = 4; // the stacks found most
constexpr std::size_t kNamesShown = 10; // and the functions found most anywhere on the stack

std::string Percent(std::size_t part, std::size_t whole) {
    return std::format("{}%", (part * 100 + whole / 2) / whole);
}

} // namespace

std::vector<std::string> HitchSamplesDescribe(const std::string &label, const std::vector<std::vector<std::string>> &stacks, double span_ms) {
    std::vector<std::string> lines;
    if (stacks.empty()) {
        return lines;
    }
    lines.push_back(std::format("hitch: {}, the game's thread looked at {} times in {:.0f} ms, found most in (innermost first):", label, stacks.size(), span_ms));
    std::map<std::string, std::size_t> by_stack;
    struct Found {
        std::size_t count = 0;
        double      depth = 0.0; // summed distance from the stack's root, which ranks a caller's callee first among equals
    };
    std::unordered_map<std::string, Found> by_name;
    for (const std::vector<std::string> &stack : stacks) {
        std::string joined;
        for (std::size_t i = 0; i < stack.size() && i < kStackShown; i++) {
            joined += (i == 0 ? "" : " < ") + stack[i];
        }
        by_stack[joined.empty() ? "?" : joined]++;
        std::vector<std::string_view> seen;
        for (std::size_t i = 0; i < stack.size(); i++) {
            if (std::ranges::find(seen, stack[i]) != seen.end()) {
                continue;
            }
            seen.push_back(stack[i]);
            Found &found = by_name[stack[i]];
            found.count++;
            found.depth += static_cast<double>(stack.size() - i);
        }
    }
    std::vector<std::pair<std::string, std::size_t>> most(by_stack.begin(), by_stack.end());
    std::ranges::stable_sort(most, [](const auto &a, const auto &b) { return a.second > b.second; });
    for (std::size_t i = 0; i < most.size() && i < kStacksShown; i++) {
        lines.push_back(std::format("hitch:   {} {}", Percent(most[i].second, stacks.size()), most[i].first));
    }
    std::vector<std::pair<std::string, Found>> names(by_name.begin(), by_name.end());
    std::ranges::sort(names, [](const auto &a, const auto &b) {
        if (a.second.count != b.second.count) {
            return a.second.count > b.second.count;
        }
        const double depth_a = a.second.depth / static_cast<double>(a.second.count);
        const double depth_b = b.second.depth / static_cast<double>(b.second.count);
        return depth_a != depth_b ? depth_a > depth_b : a.first < b.first;
    });
    std::string anywhere = "hitch:   anywhere on the stack:";
    for (std::size_t i = 0; i < names.size() && i < kNamesShown; i++) {
        anywhere += std::format("{} {} {}", i == 0 ? "" : ",", Percent(names[i].second.count, stacks.size()), names[i].first);
    }
    lines.push_back(anywhere);
    return lines;
}

int HitchSampleThreshold() {
    const char *asked = std::getenv("DC_HITCH_SAMPLE_MS");
    if (asked != nullptr && *asked != '\0') {
        if (std::string_view(asked) == "off") {
            return 0;
        }
        return std::max(0, std::atoi(asked));
    }
#if DC_HITCH_SAMPLER_WINDOWS
    return 25;
#else
    return 0;
#endif
}

namespace {

constexpr std::size_t kFrames = 48;
constexpr std::size_t kSamples = 1024; // a report's; a longer tick keeps an even spread of them (reservoir sampling)

struct Sample {
    std::array<std::uintptr_t, kFrames> frames{};
    std::size_t                         count = 0;
    std::int64_t                        ns = 0;
};

struct Buffer {
    std::vector<Sample> samples = std::vector<Sample>(kSamples);
    std::size_t         kept = 0;
    std::uint64_t       seen = 0;
};

using Clock = std::chrono::steady_clock;

std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// A demangled C++ name without its parameters, which say more than a report needs.
std::string ShortName(const char *mangled) {
    int                                     status = 0;
    std::unique_ptr<char, decltype(&std::free)> demangled(abi::__cxa_demangle(mangled, nullptr, nullptr, &status), &std::free);
    std::string                             name = status == 0 && demangled != nullptr ? demangled.get() : mangled;
    for (const std::string_view tail : {" const", " &&", " &", " noexcept"}) {
        while (name.size() > tail.size() && name.ends_with(tail)) {
            name.erase(name.size() - tail.size());
        }
    }
    if (!name.empty() && name.back() == ')') {
        int depth = 0;
        for (std::size_t i = name.size(); i-- > 0;) {
            depth += name[i] == ')' ? 1 : name[i] == '(' ? -1 : 0;
            if (depth == 0) {
                name.erase(i);
                break;
            }
        }
    }
    return name.empty() ? mangled : name;
}

#if DC_HITCH_SAMPLER_WINDOWS

constexpr std::size_t kStackCopy = 512 * 1024;
constexpr std::size_t kStackSlack = 64 * 1024; // zeroes past the copy, for a frame that reaches beyond it
constexpr std::size_t kStackLimit = 64u << 20;  // beyond any thread's stack: a stack pointer further down isn't the stack's

struct Target {
    HANDLE    thread = nullptr;
    ULONG_PTR high = 0; // the stack's top: from the thread's stack pointer up to here is in use, and so readable
};

std::string Utf8(const wchar_t *wide) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Each Register of a context that points into the copied stack's original is moved to the copy, so that unwinding
// reads only the copy.
void Rebase(CONTEXT &context, DWORD64 from, DWORD64 size, DWORD64 to) {
    for (DWORD64 *value : {&context.Rax, &context.Rcx, &context.Rdx, &context.Rbx, &context.Rsp, &context.Rbp, &context.Rsi, &context.Rdi, &context.R8,
                           &context.R9, &context.R10, &context.R11, &context.R12, &context.R13, &context.R14, &context.R15}) {
        if (*value >= from && *value < from + size) {
            *value = *value - from + to;
        }
    }
}

class Stacks {
public:
    Stacks() : copy_(kStackCopy + kStackSlack) {}

    void Attach(Target target) { target_ = target; }

    // The thread is only stopped for its registers and a copy of its stack: unwinding takes locks (the loader's) it
    // could hold, and allocating would wait on the heap's.
    bool Capture(Sample &sample) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (SuspendThread(target_.thread) == static_cast<DWORD>(-1)) {
            return false;
        }
        const bool  got = GetThreadContext(target_.thread, &context) != 0;
        std::size_t copied = 0;
        if (got && context.Rsp < target_.high && target_.high - context.Rsp <= kStackLimit) {
            copied = std::min<std::size_t>(target_.high - context.Rsp, kStackCopy);
            std::memcpy(copy_.data(), reinterpret_cast<const void *>(context.Rsp), copied);
        }
        ResumeThread(target_.thread);
        if (copied == 0) {
            return false;
        }
        std::memset(copy_.data() + copied, 0, kStackSlack);
        const DWORD64 from = context.Rsp;
        const DWORD64 to = reinterpret_cast<DWORD64>(copy_.data());
        const DWORD64 end = to + copied;
        Rebase(context, from, copied, to);
        sample.count = 0;
        while (sample.count < kFrames && context.Rip != 0) {
            sample.frames[sample.count++] = context.Rip;
            if (context.Rsp < to || context.Rsp + 8 > end) {
                break;
            }
            DWORD64           base = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
            if (function == nullptr) {
                // A leaf: nothing moved the stack, which holds the return address.
                context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
                context.Rsp += 8;
            } else {
                PVOID   handler_data = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
            }
            Rebase(context, from, copied, to);
            if (context.Rsp < to || context.Rsp >= end) {
                break;
            }
        }
        return sample.count != 0;
    }

    std::string Name(std::uintptr_t address) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address),
                                &module) ||
            module == nullptr) {
            return std::format("0x{:x}", address);
        }
        Module                 &found = Load(module);
        const auto              base = reinterpret_cast<std::uintptr_t>(module);
        const std::uint32_t     rva = static_cast<std::uint32_t>(address - base);
        const auto              after = std::ranges::upper_bound(found.names, rva, {}, &std::pair<std::uint32_t, std::string>::first);
        if (after == found.names.begin()) {
            return std::format("{}+0x{:x}", found.file, rva);
        }
        const auto &[start, name] = *std::prev(after);
        return found.exports ? std::format("{}!{}+0x{:x}", found.file, name, rva - start) : name;
    }

private:
    struct Module {
        std::string                                     file;
        bool                                            exports = false; // names are only the nearest export before
        std::vector<std::pair<std::uint32_t, std::string>> names;
    };

    Module &Load(HMODULE module) {
        if (const auto found = modules_.find(module); found != modules_.end()) {
            return found->second;
        }
        Module  &loaded = modules_[module];
        wchar_t  path[MAX_PATH * 2] = {};
        GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        const std::string full = Utf8(path);
        loaded.file = full.substr(full.find_last_of("\\/") + 1);
        if (module == GetModuleHandleW(nullptr)) {
            LoadSymbolTable(std::filesystem::path(path), loaded);
        }
        if (loaded.names.empty()) {
            LoadExports(module, loaded);
        }
        std::ranges::sort(loaded.names);
        return loaded;
    }

    // The game's own functions, from the COFF symbol table lld leaves in an executable it didn't strip.
    static void LoadSymbolTable(const std::filesystem::path &path, Module &module) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto read = [&](std::size_t at, auto &value) {
            if (at + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + at, sizeof(value));
            return true;
        };
        std::uint32_t pe = 0;
        std::uint16_t sections = 0;
        std::uint32_t table = 0;
        std::uint32_t symbols = 0;
        std::uint16_t optional = 0;
        if (!read(0x3C, pe) || !read(pe + 6, sections) || !read(pe + 12, table) || !read(pe + 16, symbols) || !read(pe + 20, optional) || table == 0) {
            return;
        }
        std::vector<std::pair<std::uint32_t, bool>> section_rva(sections); // its address and whether it holds code
        for (std::uint16_t i = 0; i < sections; i++) {
            const std::size_t at = pe + 24 + optional + i * 40u;
            std::uint32_t     address = 0;
            std::uint32_t     flags = 0;
            if (!read(at + 12, address) || !read(at + 36, flags)) {
                return;
            }
            section_rva[i] = {address, (flags & IMAGE_SCN_MEM_EXECUTE) != 0};
        }
        const std::size_t strings = table + static_cast<std::size_t>(symbols) * 18;
        for (std::uint32_t i = 0; i < symbols; i++) {
            const std::size_t at = table + static_cast<std::size_t>(i) * 18;
            std::uint32_t     value = 0;
            std::int16_t      section = 0;
            std::uint8_t      storage = 0;
            std::uint8_t      aux = 0;
            if (!read(at + 8, value) || !read(at + 12, section) || !read(at + 16, storage) || !read(at + 17, aux)) {
                return;
            }
            i += aux;
            if (section <= 0 || section > sections || !section_rva[section - 1].second ||
                (storage != IMAGE_SYM_CLASS_EXTERNAL && storage != IMAGE_SYM_CLASS_STATIC)) {
                continue;
            }
            std::string name;
            std::uint32_t zeroes = 0;
            std::uint32_t offset = 0;
            read(at, zeroes);
            if (zeroes == 0 && read(at + 4, offset) && strings + offset < bytes.size()) {
                name.assign(bytes.data() + strings + offset, strnlen(bytes.data() + strings + offset, bytes.size() - strings - offset));
            } else {
                name.assign(bytes.data() + at, strnlen(bytes.data() + at, 8));
            }
            if (!name.empty() && name.front() != '.') {
                module.names.emplace_back(section_rva[section - 1].first + value, ShortName(name.c_str()));
            }
        }
    }

    static void LoadExports(HMODULE module, Module &loaded) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(module);
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes);
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(bytes + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory.VirtualAddress == 0) {
            return;
        }
        const auto *exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(bytes + directory.VirtualAddress);
        const auto *functions = reinterpret_cast<const DWORD *>(bytes + exports->AddressOfFunctions);
        const auto *names = reinterpret_cast<const DWORD *>(bytes + exports->AddressOfNames);
        const auto *ordinals = reinterpret_cast<const WORD *>(bytes + exports->AddressOfNameOrdinals);
        for (DWORD i = 0; i < exports->NumberOfNames; i++) {
            const DWORD rva = functions[ordinals[i]];
            if (rva >= directory.VirtualAddress && rva < directory.VirtualAddress + directory.Size) {
                continue; // forwarded elsewhere
            }
            loaded.names.emplace_back(rva, reinterpret_cast<const char *>(bytes + names[i]));
        }
        loaded.exports = true;
    }

    Target                             target_;
    std::vector<std::uint8_t>          copy_;
    std::unordered_map<HMODULE, Module> modules_;
};

Target ThisThread() {
    Target target;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target.thread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    // The thread's own information block, which Windows versions before 8 (without GetCurrentThreadStackLimits) have too.
    target.high = reinterpret_cast<ULONG_PTR>(reinterpret_cast<const NT_TIB *>(NtCurrentTeb())->StackBase);
    return target;
}

#elif DC_HITCH_SAMPLER_LINUX

// The signal's handler unwinds on the game's thread into the slot the sampler names, then says it's done.
std::atomic<Sample *> g_slot = nullptr;
std::atomic<bool>     g_done = true;

struct Unwinding {
    Sample        *sample;
    std::uintptr_t pc;
    bool           found;
};

_Unwind_Reason_Code Frame(_Unwind_Context *context, void *argument) {
    auto              *unwinding = static_cast<Unwinding *>(argument);
    const std::uintptr_t ip = _Unwind_GetIP(context);
    if (!unwinding->found) {
        // The handler's own frames and the signal's come first.
        if (ip != unwinding->pc) {
            return _URC_NO_REASON;
        }
        unwinding->found = true;
    }
    unwinding->sample->frames[unwinding->sample->count++] = ip;
    return unwinding->sample->count < kFrames ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void Handle(int, siginfo_t *, void *context) {
    const int saved = errno;
    if (Sample *sample = g_slot.exchange(nullptr); sample != nullptr) {
        const auto *machine = &static_cast<ucontext_t *>(context)->uc_mcontext;
#if defined(__x86_64__)
        const auto pc = static_cast<std::uintptr_t>(machine->gregs[REG_RIP]);
#else
        const auto pc = static_cast<std::uintptr_t>(machine->pc);
#endif
        Unwinding unwinding{sample, pc, false};
        sample->count = 0;
        _Unwind_Backtrace(Frame, &unwinding);
        if (!unwinding.found) {
            sample->frames[0] = pc;
            sample->count = 1;
        }
        g_done.store(true, std::memory_order_release);
    }
    errno = saved;
}

struct Target {
    pid_t process = 0;
    pid_t thread = 0;
};

class Stacks {
public:
    void Attach(Target target) {
        target_ = target;
        struct sigaction action{};
        action.sa_sigaction = Handle;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&action.sa_mask);
        sigaction(SIGPROF, &action, nullptr);
    }

    // Unwound into a slot of its own, which a handler still running after the wait gives up keeps writing to.
    bool Capture(Sample &sample) {
        if (!g_done.load(std::memory_order_acquire)) {
            return false; // the last signal hasn't been handled yet
        }
        slot_.count = 0;
        g_done.store(false);
        g_slot.store(&slot_);
        if (syscall(SYS_tgkill, target_.process, target_.thread, SIGPROF) != 0) {
            g_slot.store(nullptr);
            g_done.store(true);
            return false;
        }
        const auto give_up = Clock::now() + std::chrono::milliseconds(100);
        while (!g_done.load(std::memory_order_acquire)) {
            if (Clock::now() > give_up) {
                // The handler may still write to it, so the sample is dropped, not reused.
                if (g_slot.exchange(nullptr) != nullptr) {
                    g_done.store(true);
                }
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
        sample.frames = slot_.frames;
        sample.count = slot_.count;
        return sample.count != 0;
    }

    std::string Name(std::uintptr_t address) {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void *>(address), &info) == 0) {
            return std::format("0x{:x}", address);
        }
        if (info.dli_sname != nullptr) {
            return ShortName(info.dli_sname);
        }
        // The game's own functions aren't exported: addr2line names them from the offset.
        const std::string_view file = info.dli_fname != nullptr ? info.dli_fname : "?";
        return std::format("{}+0x{:x}", file.substr(file.find_last_of('/') + 1), address - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
    }

private:
    Target target_;
    Sample slot_;
};

Target ThisThread() {
    return {getpid(), static_cast<pid_t>(syscall(SYS_gettid))};
}

#else

struct Target {};

class Stacks {
public:
    void        Attach(Target) {}
    bool        Capture(Sample &) { return false; }
    std::string Name(std::uintptr_t address) { return std::format("0x{:x}", address); }
};

Target ThisThread() {
    return {};
}

#endif

class Sampler {
public:
    explicit Sampler(int threshold_ms) : threshold_ns_(static_cast<std::int64_t>(threshold_ms) * 1'000'000) {
        stacks_.Attach(ThisThread());
        thread_ = std::thread([this] { Run(); });
    }

    void TickBegins() {
        woke_ns_.store(Now(), std::memory_order_relaxed);
        tick_.fetch_add(1, std::memory_order_release);
        tick_.notify_all();
    }

    void TickEnds() { tick_.fetch_add(1, std::memory_order_release); }

    void Report(std::string label) {
        {
            const std::scoped_lock hold(lock_);
            if (buffers_[active_].kept == 0 || report_.has_value()) {
                return;
            }
            report_ = {std::move(label), active_};
            active_ ^= 1;
            buffers_[active_].kept = 0;
            buffers_[active_].seen = 0;
        }
        tick_.fetch_add(2, std::memory_order_release); // stays as working or not as it was
        tick_.notify_all();
    }

    void Discard() {
        const std::scoped_lock hold(lock_);
        buffers_[active_].kept = 0;
        buffers_[active_].seen = 0;
    }

    void Stop() {
        stopping_ = true;
        tick_.fetch_add(2, std::memory_order_release);
        tick_.notify_all();
        thread_.join();
    }

private:
    void Run() {
        while (true) {
            // Read before the report is written: a report asked for before stopping is then written.
            const bool stopping = stopping_;
            WriteReport();
            if (stopping) {
                return;
            }
            const std::uint64_t tick = tick_.load(std::memory_order_acquire);
            if (tick % 2 == 0) {
                tick_.wait(tick);
                continue;
            }
            const std::int64_t due = woke_ns_.load(std::memory_order_relaxed) + threshold_ns_;
            if (const std::int64_t now = Now(); now < due) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(due - now));
            }
            while (!stopping_ && tick_.load(std::memory_order_acquire) == tick) {
                Take(tick);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    void Take(std::uint64_t tick) {
        const std::scoped_lock hold(lock_);
        if (tick_.load(std::memory_order_acquire) != tick) {
            return;
        }
        Buffer     &buffer = buffers_[active_];
        std::size_t slot = buffer.kept;
        if (buffer.kept == kSamples) {
            const std::uint64_t pick = std::uniform_int_distribution<std::uint64_t>(0, buffer.seen)(random_);
            if (pick >= kSamples) {
                buffer.seen++;
                return;
            }
            slot = static_cast<std::size_t>(pick);
        }
        Sample taken;
        taken.ns = Now();
        if (stacks_.Capture(taken)) {
            buffer.samples[slot] = taken;
            buffer.kept = std::max(buffer.kept, slot + 1);
            buffer.seen++;
        }
    }

    void WriteReport() {
        std::optional<std::pair<std::string, int>> report;
        {
            const std::scoped_lock hold(lock_);
            report = report_;
        }
        if (!report.has_value()) {
            return;
        }
        Buffer                               &buffer = buffers_[report->second];
        std::vector<std::vector<std::string>> stacks;
        std::int64_t                          first = INT64_MAX;
        std::int64_t                          last = INT64_MIN;
        for (std::size_t i = 0; i < buffer.kept; i++) {
            const Sample            &sample = buffer.samples[i];
            std::vector<std::string> names;
            for (std::size_t frame = 0; frame < sample.count; frame++) {
                // A caller's frame holds the address after its call, which may be the next function's.
                const std::uintptr_t address = sample.frames[frame] - (frame == 0 ? 0 : 1);
                auto [known, added] = names_.try_emplace(address);
                if (added) {
                    known->second = stacks_.Name(address);
                }
                names.push_back(known->second);
            }
            stacks.push_back(std::move(names));
            first = std::min(first, sample.ns);
            last = std::max(last, sample.ns);
        }
        const double span_ms = stacks.empty() ? 0.0 : static_cast<double>(last - first) / 1e6;
        for (const std::string &line : HitchSamplesDescribe(report->first, stacks, span_ms)) {
            Write(line);
        }
        const std::scoped_lock hold(lock_);
        buffer.kept = 0;
        buffer.seen = 0;
        report_.reset();
    }

    void Write(const std::string &line) {
        std::function<void(const std::string &)> sink;
        {
            const std::scoped_lock hold(sink_lock_);
            sink = sink_;
        }
        if (sink) {
            sink(line);
        } else {
            std::fprintf(stderr, "%s\n", line.c_str());
        }
    }

public:
    std::mutex                               sink_lock_;
    std::function<void(const std::string &)> sink_;

private:
    const std::int64_t                          threshold_ns_;
    Stacks                                      stacks_;
    std::atomic<std::uint64_t>                  tick_ = 0; // odd while a tick's work runs
    std::atomic<std::int64_t>                   woke_ns_ = 0;
    std::atomic<bool>                           stopping_ = false;
    std::mutex                                  lock_;
    std::array<Buffer, 2>                       buffers_;
    int                                         active_ = 0;
    std::optional<std::pair<std::string, int>>  report_;
    std::mt19937_64                             random_{12345};
    std::unordered_map<std::uintptr_t, std::string> names_;
    std::thread                                 thread_;
};

// Never destroyed: a thread still joinable at exit would end the program.
Sampler                                 *g_sampler = nullptr;
std::function<void(const std::string &)> g_sink;

} // namespace

std::string CodeAddressName(std::uintptr_t address) {
    // Its own, since the sampler's thread names with the sampler's. Never destroyed: a crash can be reported during exit.
    static std::mutex lock;
    static Stacks    *stacks = new Stacks;
    const std::scoped_lock hold(lock);
    return stacks->Name(address);
}

void HitchSamplerAttach(int threshold_ms) {
    if (threshold_ms <= 0 || g_sampler != nullptr) {
        return;
    }
    g_sampler = new Sampler(threshold_ms);
    g_sampler->sink_ = g_sink;
}

void HitchSamplerTickBegins() {
    if (g_sampler != nullptr) {
        g_sampler->TickBegins();
    }
}

void HitchSamplerTickEnds() {
    if (g_sampler != nullptr) {
        g_sampler->TickEnds();
    }
}

void HitchSamplerReport(std::string label) {
    if (g_sampler != nullptr) {
        g_sampler->Report(std::move(label));
    }
}

void HitchSamplerDiscard() {
    if (g_sampler != nullptr) {
        g_sampler->Discard();
    }
}

void HitchSamplerStop() {
    if (g_sampler != nullptr) {
        g_sampler->Stop();
        delete g_sampler;
        g_sampler = nullptr;
    }
}

void HitchSamplerSetSink(std::function<void(const std::string &)> sink) {
    g_sink = sink;
    if (g_sampler != nullptr) {
        const std::scoped_lock hold(g_sampler->sink_lock_);
        g_sampler->sink_ = std::move(sink);
    }
}

} // namespace dc::platform
