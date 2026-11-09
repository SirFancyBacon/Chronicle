#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dc::platform {

/**
 * Where the game's thread was while a tick ran long: a second thread looks at its stack every millisecond once a tick
 * has taken `threshold_ms`, and the hitch line is followed by the places it was found most. It names work no
 * load_profile kind times. On Windows the thread is stopped for a moment and its stack copied, so it's on by default;
 * on Linux a signal interrupts the thread's system calls, so it's on only when DC_HITCH_SAMPLE_MS asks. Elsewhere it
 * does nothing.
 */

/** The milliseconds a tick runs before it's sampled, from DC_HITCH_SAMPLE_MS ("off" for never), or 0 for never. */
int HitchSampleThreshold();

/** Starts sampling the calling thread, the game's, once its ticks run `threshold_ms`. Does nothing for 0. */
void HitchSamplerAttach(int threshold_ms);

/** The game's thread's tick work begins and ends, as hitches.cpp times it. */
void HitchSamplerTickBegins();
void HitchSamplerTickEnds();

/** Has the samples taken since the last report written to stderr, headed by `label` ("frame 1545"), from the sampler's thread. */
void HitchSamplerReport(std::string label);

/** Forgets the samples since the last report: their ticks were in time after all, the threshold being under a tick. */
void HitchSamplerDiscard();

/** Stops the sampler, once its report is written. */
void HitchSamplerStop();

/** Where reports go instead of stderr, one line a call: for tests. Empty for stderr again. */
void HitchSamplerSetSink(std::function<void(const std::string &)> sink);

/** A code address named as reports name it: the function, or the module and offset when it has no name to give. Any thread. */
std::string CodeAddressName(std::uintptr_t address);

/** The report's lines for samples whose stacks were found as these names, innermost first, over `span_ms`. */
std::vector<std::string> HitchSamplesDescribe(const std::string &label, const std::vector<std::vector<std::string>> &stacks, double span_ms);

} // namespace dc::platform
