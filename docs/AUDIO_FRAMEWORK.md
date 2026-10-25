# Audio framework (proposal, r2)

A plan for Chronicle's sound: real surround (5.1 and 7.1) with every positioned sound placed where it
is, sound that matches the PlayStation 2 rather than the port as it is today, and audio extracted
into the formats ChronicleTwo already uses, so that no Sony format is parsed while the game runs.
Everything that isn't particular to Dark Cloud goes into Kazarov, so ChronicleTwo gets the same
surround output and the same fidelity work, and each game keeps only a thin layer that connects its
own sound calls to Kazarov.

Nothing here is built yet. Sections 2 and 3 describe what exists and what was measured; the rest is
the proposal.

---

## 1. Goals

- **Surround that places sounds.** Mono, stereo, 5.1 and 7.1 output, chosen from the device (or by
  the player). A monster behind the camera is heard behind the player, a chest to the left from the
  left surround speaker. Today's "Surround" option only spreads the finished stereo mix over 5.1.
- **The PS2 is the reference.** The target is the sound of the original hardware: the SPU2's
  voices, envelopes, effects and reverb, simulated as closely as possible. A departure from the
  hardware is accepted only where it is an improvement. Where today's synth differs from the PS2
  (section 2), that difference is work to do, and it is done once, in Kazarov, for both games.
- **Stereo stays the PS2's.** Surround is added without changing what a stereo player hears: until
  the fidelity work (section 5), stereo output is tested to stay sample for sample what it is today,
  and that work only moves it closer to the hardware.
- **Extracted, open data.** The disc's banks, samples and sequences are converted at extraction into
  ChronicleTwo's documented formats (its `docs/PORT_AUDIO_FORMAT.md`): bank JSON, PCM samples,
  sequence JSON. The running game reads only those, through Kazarov's loaders.
- **Shared scaffolding.** Kazarov holds the engine, the loaders, the extraction converters, the
  output and the spatial mixing. A game supplies its file layout, its sound tables, and the hooks
  from its retail sound functions.
- **Every platform.** Linux, Windows and macOS, the Switch docked to a receiver, and Android over
  HDMI. iOS is not a Chronicle target.

---

## 2. Today

**The engine.** `port/src/sound.cpp` replaces retail's `CSound` on Kazarov's `audio::Mixer`
(`tools/kazarov/include/audio/mixer.hpp`):

- 16 ports of 16 MIDI channels each, a 48-voice synth (two cores of 24, as the SPU2 has), the SPU2
  ADSR envelope run at 48 kHz, 4-point interpolation, and a sequencer that plays SQ songs with
  Sony's NRPN loop markers.
- One reverb per core: a Freeverb-style comb and all-pass network with a shape per SPU2 mode
  (`reverb.cpp`), fed by the voices whose sample attributes set the effect-send bits.
- The banks and sequences are Sony's own HD, BD and SQ, parsed while the game runs
  (`raw_audio.cpp`, `vag.cpp`).

**The output.** `tools/kazarov/src/cpp/platform/audio.cpp` opens an SDL3 stream of 32-bit float at
48 kHz and pulls the mixer in blocks of 1,024 frames on SDL's audio thread. The mixer renders
stereo only. With Surround on, the stereo result is upmixed to 5.1 afterwards: the fronts keep the
stereo, the centre takes the middle, the LFE a 120 Hz low-pass of it, and the surrounds a delayed,
filtered side signal. Nothing in the mixer carries a position: a voice has a 7-bit pan and nothing
else.

**Where the port differs from the PS2** (`docs/PC.md`, "Audio", and the code):

- The reverb is a generic room, not the SPU2's effect programs and work area.
- Core 0's output does not pass through core 1, as it does on the hardware.
- Envelope key follow, LFOs, velocity crossfades and per-key pan follow are not applied.
- Effect messages resolve their tone by key range, then split index.
- Mono output is a plain average of left and right, and the output is hard-clipped.

**The game's side.** Retail reaches sound through 67 functions in `snd.cpp`, almost all of them
still retail code on top of the replaced `CSound`:

| Group | Functions | Most used |
|---|---|---|
| System and listener | `SndInit`, `SndInitialize`, `SndExit`, `SndStep`, `SndSetCamera` (2), `SndSetReadBuffer`, `SndSyncBG`, `SndInitSeTable` | `SndStep` (13 calls), `SndSetCamera` (6) |
| Music | `SndBgm*` (17), `SndCheckFade`, `SndGetNowSetNo` | `SndBgmStop` (12), `SndBgmPlay` (12) |
| Effects | `SndSePlay` (3 forms), `SndSeStop`, `SndSetSeVol(f)`, `SndSetSePanf`, `SndStopAllSe`, `SndGetVolf`, `SndGetPanf`, `SndSoundLoad*` | `SndSePlay` (189 calls) |
| Positioned effects | `SndGetVolPan`, `SndSePlay(se, position, near, far)`, `SndPlayFootSound` | `SndGetVolPan` (12) |
| Effect sequences | `SndSeSeqInit`, `SndSeSeqPlayStop`, `SndSeSeqStep`, `SndSeSeqAllStop` | `SndSeSeqPlayStop` (7) |
| Ambient loops | `SndAmbient*` (6) | `SndAmbientStop` (5) |
| Voices | `SndVoiceLoad*` (3) | `SndVoiceLoad` (3) |
| Special effect sets | `SndSPSe*` (8) | `SndSPSePlay` (9) |

Call counts are of retail's sources outside `snd.cpp`. The title and opening also call `CSnd.SE_Play`
and `CSnd.SetReverb` directly (5 and 18 calls).

**Positions exist, and are thrown away.** Positioned sounds give `SndGetVolPan` a world position and
a near and far distance; it measures the distance to the camera for the volume, then keeps only how
far the sound is to the left or right: `pan = 0.7 × side⁴`. Whether the sound is in front or behind
is computed and discarded. Its callers are dungeon objects (5), monsters (2), towns (`edit.cpp`,
`editloop3.cpp`) and the opening; `SndSePlay(se, position, near, far)` adds the rush movie's impacts
and fishing's cast, sink and splash; `SndPlayFootSound` adds footsteps (4 calls); and
`SndSetCamera` sets the listener. Every positioned sound found so far passes through these three;
phase 4 audits the remaining callers that pass a pan of their own (`SndSePlay(se, volume, pan,
voice)`, `SndSetSPSePanf`).

**ChronicleTwo.** Its extraction already publishes `*.sound.json` bundles of bank JSON, WAV samples,
sequence JSON and effect sequences, and its runtime plays them through the same `audio::Mixer`.
Kazarov already has the loaders it uses: `Bank::CreateFromJson` (`hdbank.cpp`), which decodes each
sample with `LoadPcmTrack`, and `SqFile::CreateFromJson` (`sq.cpp`). The rest of its loader (bundle
validation, port binding, sound tables, scene reverb routes) is in ChronicleTwo's port code,
mixed with Dark Cloud 2's own limits and port layout. Its effect sequences are played by retail's
decompiled player, and its DualSense speaker and haptics are separate `Mixer` instances.

---

## 3. Measured: Dark Cloud's sound data

Counted on the PAL disc's extracted data (`data/sound`):

| | |
|---|---|
| Sound packs (`.snd`) | 217 |
| Bank references in them | 561 |
| Unique banks | 206 |
| Unique samples | 2,482, about 42 minutes of audio in all |
| Unique sequence files | 108, 0.66 MB |
| Sound tables in the packs | 145 `.cfg`, 220 `.txt` |
| Sample data, PS2 ADPCM (unique) | 48.2 MB |
| The samples as 16-bit WAV files | 168.8 MB |
| The samples as FLAC files | 98.2 MB, 58% of WAV |

Two things follow.

- **Banks repeat.** The 561 bank references hold only 206 different banks. Published once per pack,
  as ChronicleTwo does today, the samples would take about 2.7 times the sizes above. A bundle names
  its bank by path, so bundles can share one published bank without changing the format.
- **Samples stay WAV.** FLAC would save about 70 MB but add decoding to every bank load (entering a
  town or a dungeon); the space isn't worth the load time. Samples are published as WAV, as
  ChronicleTwo publishes them, so its format and Kazarov's loader are used unchanged.

---

## 4. Extraction

`dcdata` converts the sound packs into ChronicleTwo's formats, through Kazarov's Rust converter
(`tools/kazarov/src/rust/audio.rs`), which turns HD and BD into bank JSON and WAV samples, SQ into
sequence JSON, and decodes the ADPCM. Its format work is used as it is. Kazarov exposes two bridge
functions over it, `audio_publish_bank` and `audio_publish_sequence`, which convert one bank or one
sequence at a path a game chooses; walking the packs is the game's part, since only the game knows
its layout.

Dark Cloud's packs (`sound/bgm`, `sound/set`, `sound/voice`, `sound/special`, and the opening's
`opdat/dungeon/o1bbgm.snd`) each carry one or more load lists, their `.txt` members. A list names
the banks and sequences to load in order; the sixth letter of a member's name picks its driver slot
(`e01a_c.hd` loads into slot c), and a `.cfg` of the list's name sets core 0's reverb and the effect
table. `tools/dcdata/sound_data.hpp` reads these as the game does, and `dcdata` publishes:

| Disc | Published |
|---|---|
| a pack, `sound/set/snd0.snd` | `sound/set/snd0.sound.json` (`kind: "sound_pack"`): each list in order, the slot and published bank or sequence of each member it loads, and its `.cfg` commands |
| its banks (`.hd` and `.bd`) | `sound/banks/<member>-<hash>.sound/bank.json` and `samples/N.wav`, once per distinct bank |
| its sequences (`.sq`) | `sound/set/snd0.sound/sequences/<member>.json` |
| `sound/tbl/sqtbl.txt`, `setbl.txt` | `sound/tbl/sqtbl.json` (`sequence_volumes`), `setbl.json` (`effect_volumes`) |

Each bank is published once, under its first member's name and a hash of its contents, and every
pack that uses it names it: the 497 bank loads of the PAL disc's 218 packs come to 223 banks (186
MB of WAV). Empty members, the dummy banks some packs carry, are left out, as the game skips them.
The original packs stay in the data until the game plays the published files (phase 2). Nothing
references `sound/sound_set` or the loose files of `sound/battle`; the former is published like the
rest, the latter not.

**Checks.** `SoundPublishRealData` (with `DC_DATA`) loads every published bank and sequence through
Kazarov's JSON loaders and the original through its raw reader, and compares what the synth would
play: every program, note and velocity resolves to the same samples, PCM and loops, and every
sequence to the same events. Each `.cfg` is compared with retail's own `LoadSoundInfo`, and the
effect table with the port's reader. On both the PAL and NTSC discs, all 497 bank loads, 193
sequence loads and 145 `.cfg` files match. The one difference a sequence keeps is cosmetic: the raw
reader leaves the end of a track its meta number, `0x2F`, which nothing reads.

---|---|
| `sound/bgm/bgm27.snd` | `sound/bgm/bgm27.sound.json` (`kind: "sound_bundle"`) |
| its `.hd` and `.bd` | a bank: `sound/banks/<id>/bank.json` and `samples/N.wav`, one per unique bank |
| its `.sq` | `sound/bgm/bgm27.sound/sequences/<name>.json` |
| its `.cfg` and `.txt` tables | sound table documents (`sound_config`, `sound_volume`, and Dark Cloud's own kinds where its tables differ) |
| `sound/voice`, `sound/tbl` | voices as WAV, tables as documents |

Each bank is published once, under an ID from its contents, and every bundle that uses it names
it. The sharing is within Dark Cloud's own data: the same bank sits in many of its packs. The original packs are left out of `data.zip`, as ChronicleTwo leaves out its own.

**Checks.** Every bank's samples and loop points decode to the same PCM as Kazarov's raw reader does
today, and every sequence to the same events, the comparison `audio_raw_compat_test.cpp` already
makes for single files, run over the whole disc.

---

## 5. The engine (Kazarov)

The synth stays: the music is notes, and the PS2 played it as notes. What changes is what feeds the
synth, what it renders into, and how a game drives it.

**Data.** Banks and sequences come from the extracted documents through `Bank::CreateFromJson` and
`SqFile::CreateFromJson`. The bundle loader moves into Kazarov from ChronicleTwo's port code, as a
general one: a game passes its limits and how bundles map to its ports. The raw Sony readers stay
in Kazarov for the extractor and the tests, but no game links them.

**Rendering into any layout.** The mixer renders into the device's own channel layout instead of
stereo:

| Layout | Channels (SDL3's order) |
|---|---|
| Mono | 1 |
| Stereo | FL FR |
| 5.1 | FL FR FC LFE BL BR |
| 7.1 | FL FR FC LFE BL BR SL SR |

Each voice is rendered as now and then placed by one of two kinds of position:

- **A pan,** as today, for everything the game places by pan alone (music, menu sounds, effects
  played with a pan). In stereo this is exactly today's pan law. In surround it places the voice
  across the front speakers.
- **An emitter:** a direction and distance from the listener. A positioned voice is panned across
  all the layout's speakers by amplitude panning between the two speakers either side of its
  direction (VBAP), so a sound sweeps smoothly round the room as the camera turns. The volume is
  still the game's own distance curve, so levels match the PS2. In stereo an emitter falls back to
  the retail pan law: stereo output stays what the PS2 played.

**Buses.** Voices belong to a bus, whose volume the game sets and whose placement suits it: music
(below), effects (by pan or emitter), ambient loops (spread wide, so the space surrounds the
player), voices and menus (front). The reverb returns go to every speaker, decorrelated between
front and surrounds, for envelopment. The LFE carries the bass of the mix, low-passed, at a level
set once and tuned on real speakers.

**Music in surround** envelops the player, as receivers' all-channel stereo does and as many games
and films spread their scores: the left channel plays from the whole left side (front, side and back
left), the right channel from the whole right side, the bass from the LFE, and the centre stays out
of it. The side and back copies are delayed a few milliseconds and decorrelated, so the music
surrounds the room instead of collapsing to a point between the speakers. A Front setting keeps the
music on the front pair only, as the game was mixed. In stereo, music is as authored either way.

**Listener.** One position and facing, which the game sets from its camera.

**Cost.** The voices cost what they cost now; only the final placement scales with the channel
count, a handful of multiplies per voice per sample.

**Fidelity to the PS2.** The SPU2's effects and reverb are simulated as closely as the hardware is
documented, each item a change of its own, kept only where it brings the sound closer to the PS2 or
is otherwise an improvement:

1. The SPU2's reverb: its effect programs (room, studio, hall, space, echo, delay, pipe) running on
   its work area as the hardware does, in place of today's Freeverb approximation.
2. Core 0 mixed through core 1. The SPU2 is two halves of 24 voices, each with its own reverb, and on
   the hardware core 0's output (its voices and its reverb) is an input of core 1, which can send it
   through its own reverb too and applies the master volume to everything. Today both go straight to
   the output, so a core 0 sound can never pick up core 1's reverb and core 1's master volume misses
   it. Whether Dark Cloud routes core 0 through core 1's reverb decides how audible this is. The
   routing is set not by the game's code but by its IOP sound driver, `MODULES/EZMIDI.IRX` on the
   disc (with `MODHSYN.IRX`, `MODMIDI.IRX` and `LIBSD.IRX`), so it is read there: where the driver
   writes the SPU2's mix registers, the engine is set to match.
3. The bank features the IOP synth applies: envelope key follow, LFOs, velocity crossfades,
   per-key pan follow.
4. Effect tones resolved as the driver resolves them; mono as the hardware mixes it; no hard clip.

PCSX2 may be read to understand the hardware; nothing is copied from it.

---

## 6. The game layer (Chronicle)

`snd.cpp`'s 67 functions are replaced in `port/src/snd.cpp`, each a few lines calling Kazarov:
music, effects, effect sequences, ambient loops, voices and the special effect sets. The title's
direct `CSnd` calls (5 effects, 18 reverb settings, all in the title units) are replaced where they
are. `CSound`'s replacement (`port/src/sound.cpp`) and
the `ezMidi` answers in `gameutil.cpp` then have no callers and go.

**Positions reach the mixer** through the three places every positioned sound passes:

- `SndSePlay(se, position, near, far)` and `SndPlayFootSound(kind, foot, position)` carry the
  position: the voice they start becomes an emitter.
- `SndGetVolPan` still returns retail's volume and pan, but also keeps the position it was given.
  Every caller passes the result straight to `SndSetSeVolf` and `SndSetSePanf` for one effect, so
  those make that effect's voice an emitter at the kept position, while stereo still gets retail's
  pan. A monster's sound follows the monster as it and the camera move, as its caller updates it
  every frame.
- `SndSetCamera` sets the listener.

**Sound tables** (effect numbers to bank, program and key; sequence tables; volumes) come from the
extracted documents instead of the `.cfg` and `.txt` files.

---

## 7. Options and settings

`audio.sound` in `config.json` takes `auto` (the default: the device's layout), `mono`, `stereo`,
`5.1` and `7.1`; the earlier `surround` reads as `5.1`. `audio.music_surround` takes `surround` (the
default) or `front` (section 5). The Options screen's Sound row gains the same choices, and a Music
row the two placements, their strings under `options.audio.sound.*` and
`options.audio.music_surround.*`. A change applies at once by reopening the
stream's format, as Surround does today. Today's 5.1 upmix goes, since the mix itself is now
surround.

---

## 8. Platforms

Every platform outputs through Kazarov's SDL3 stream, so the layout comes from what SDL reports.

- **Linux, Windows, macOS:** SDL3 plays 6 and 8 channels on PipeWire and PulseAudio, WASAPI and
  Core Audio. A receiver needs HDMI (or an OS encoder such as Dolby Digital Live) for more than two
  channels of PCM: optical (S/PDIF) carries only stereo PCM.
- **Switch:** docked, through the console's HDMI output, with its own SDL backend. The number of
  channels it offers has to be checked on hardware; whatever it reports is used.
- **Android:** through SDL3's AAudio backend, multichannel over HDMI where the device offers it,
  stereo otherwise.

---

## 9. What lives where

| Kazarov (shared) | Chronicle | ChronicleTwo |
|---|---|---|
| Sony format conversion at extraction | its pack layout and table kinds | its layout (existing) |
| Bank, sequence and bundle loading | its limits, port map, tables | its limits, port map, tables |
| Synth, sequencer, reverb, PS2 fidelity | | |
| Output layouts, buses, emitters, listener, panning | hooks from `snd.cpp` | hooks from its `snd_mngr.cpp` |
| Options of the layout, config key | the Options row | its Options row |

---

## 10. Phases

Each phase is its own pull request, working and tested on its own, on top of #220.

1. **Extraction.** Dark Cloud's sound packs published in ChronicleTwo's formats through Kazarov's
   converter with Dark Cloud's layout added, banks shared, the tables as documents, checked against
   the raw reader over the whole disc. The game still plays the raw files.
2. **Loading the extracted data.** Chronicle plays the published banks and sequences through
   Kazarov's loaders; the general bundle loader moves into Kazarov. A render of every sequence
   matches today's bit for bit.
3. **Surround output in Kazarov.** Layouts, buses, emitters and the listener; stereo output stays
   bit for bit what it is now.
4. **Chronicle's sound layer.** `snd.cpp` replaced on the new API, positions passed through,
   `CSound` gone, the Options row and the config key.
5. **PS2 fidelity,** item by item (section 5).
6. **ChronicleTwo on the new output,** its hooks attached to the same API.

---

## 11. Tests

- Extraction: every bank's samples and loops and every sequence's events equal the raw reader's;
  shared banks are published once; tables round-trip.
- Loading: every published bundle loads; a render of each sequence through the published data
  equals the render through the raw files.
- Output: music in surround reaches the whole left and right sides and never the centre, and in
  Front only FL and FR; a voice panned left lands only on FL (stereo, 5.1, 7.1); an emitter directly behind lands
  on BL and BR (5.1) or the rears (7.1) at equal gain; an emitter sweeping round the listener keeps
  constant power; stereo with emitters equals retail's pan law; mono equals the hardware's mix.
- The game layer: each `Snd*` function's effect on a fake mixer; positions from `SndGetVolPan`
  reach the voice that `SndSetSePanf` names; the camera sets the listener.
- Fidelity: each item against figures from the hardware documentation.

As elsewhere in `port/src/tests/`, fixtures are made in code; the whole-disc checks skip without
`DC_DATA`.

---

## 12. Decided, and still open

Decided (r2):

- **Samples are WAV,** as ChronicleTwo's format has them; FLAC's 70 MB saving isn't worth the load
  time.
- **Banks are shared within Dark Cloud's data:** each unique bank is published once.
- **Effect sequences are game-specific,** as each game's own player has them.
- **Music envelops in surround** by default (section 5), with a Front setting.
- **The SPU2 is simulated** as closely as possible; departures only as improvements.

Open:

- **How audible core 0 through core 1 is,** which depends on how Dark Cloud's driver sets the cores'
  mix registers; read from `EZMIDI.IRX` in phase 5.
- **The Switch's channel count** over HDMI is left for now: a Switch player can choose a layout by
  hand.
