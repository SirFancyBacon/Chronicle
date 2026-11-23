#pragma once

#include "platform/config.hpp"
#include "render/noktus_mirror.hpp"

namespace dc::options {

/**
 * The Quality preset a first start picks for a GPU (the owner's table, 2026-11-23): Low for an integrated or a software GPU, and for any with
 * 4 GB of its own or less; a laptop's (named Laptop, Mobile or Max-Q, or a model number ending in M or S: RX 6800M, RX 7600S, Arc A770M)
 * Medium otherwise; a desktop's Medium under 7 GB, High under 11 GB and
 * Ultra from 11 GB, an RTX or a Radeon RX 6000 or later at least High.
 */
int QualityForGpu(const render::MirrorGpuFacts &gpu);

/** A stretch of play slower than this a frame on the GPU (milliseconds; a steady 60 has 16.7) steps the pick down. */
inline constexpr double kQualitySlowMs = 12.5;

/** How many stretches of play (each the mirror's few seconds' report) check the pick: 30 seconds. */
inline constexpr int kQualityCheckStretches = 6;

/**
 * One stretch of play's check of a first start's pick, the GPU taking gpu_ms a frame, *stretches the stretches checked before it: slow, a preset
 * down (none below Low), and settled; else counted, and settled once kQualityCheckStretches have been. A preset changed by hand meanwhile
 * (Custom) is left as it is, and settled. Returns whether config changed.
 */
bool QualityCheckStretch(platform::Config &config, double gpu_ms, int *stretches);

/** A change made by hand in Options, before to after: one that changes which preset the settings match ends a first start's check, as it is. */
void QualitySettleByHand(const platform::Config &before, platform::Config &after);

/**
 * At a first start (no config.json was read), with a window: the preset for the GPU Noktus draws on is picked at the first tick's end (by then
 * drawing, and the settings' change hooks in), and left to be checked in play.
 */
void QualityPickAtFirstTick();

/**
 * At each tick's end: the first start's pick, if one is waiting; then, while it's unsettled, each stretch of play as the mirror reports it. Those
 * with Options open over the scene, and the one Options closes in, aren't play's.
 */
void QualityCheckTick();

} // namespace dc::options
