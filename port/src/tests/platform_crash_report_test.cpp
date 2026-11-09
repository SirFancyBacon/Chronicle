#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "platform/crash_report.hpp"
#include "platform/hitch_sampler.hpp"

namespace dc::test {

using namespace dc::platform;

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
namespace {

[[gnu::noinline]] void WriteThrough(volatile int *where) {
    *where = 1;
}

} // namespace

TEST(PlatformCrashReport, NamesAnExportedFunction) {
    EXPECT_EQ(CodeAddressName(reinterpret_cast<std::uintptr_t>(&std::abort) + 1), "abort");
}

TEST(PlatformCrashReport, AFaultSaysWhatWhereAndTheStackThenDies) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            CrashReportInstall();
            WriteThrough(reinterpret_cast<volatile int *>(16));
        },
        "crash: Segmentation fault at 0x[0-9a-f]+, touching 0x10, the executable loaded at 0x[0-9a-f]+\ncrash: stack( 0x[0-9a-f]+)+\ncrash:   .+\n");
}

TEST(PlatformCrashReport, AnAbortIsReportedToo) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            CrashReportInstall();
            std::abort();
        },
        "crash: Aborted at 0x[0-9a-f]+, the executable loaded at 0x[0-9a-f]+\ncrash: stack( 0x[0-9a-f]+)+\ncrash:   ");
}
#endif

} // namespace dc::test
