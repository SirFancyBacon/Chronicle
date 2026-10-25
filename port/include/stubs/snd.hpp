#pragma once

// The sound publishing check (port/src/tests/sound_publish_test.cpp) reads every sound pack's .cfg
// as this unit does, through a global forwarder for its static LoadSoundInfo.

struct SND_INFO;

static void LoadSoundInfo(SND_INFO *info, char *script, int script_size);

void PortLoadSoundInfo(SND_INFO *info, char *script, int script_size) {
    LoadSoundInfo(info, script, script_size);
}
