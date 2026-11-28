# Flatpak

The Linux release of the PC port (`docs/PC.md`) is a Flatpak,
`org.themoonpeople.Chronicle`. It holds `darkcloud` and `dcdata` on the
freedesktop 26.08 runtime, whose SDL3 and Vulkan loader it uses. No game data is in it: the first start asks for
your own PAL disc and extracts it.

## Installing the bundle

CI (`.github/workflows/pc.yml`) uploads `chronicle.flatpak` with every
build. The runtime comes from Flathub:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user chronicle.flatpak
flatpak run org.themoonpeople.Chronicle
```

It also appears in the desktop's application list as Chronicle.

## The first start

The game is set up once from the player's own disc by the launcher, which the
Flatpak lists as **Chronicle Launcher** beside the game. Started with no game
data, or data an older version set up, the game says so in a message and
offers to open the launcher, then exits with status 3.

In the launcher's Setup page, **Disc image...** takes a `.iso` of the PAL or NTSC
disc and **Disc folder...** a folder holding `DATA.DAT` and `DATA.HD2`. Inside the
Flatpak these are the desktop's file-chooser portal, so the launcher reads the
chosen file without any permission to your files. **Set up** copies the disc's
files, converts the textures, models and sound, and packs it all into `data.zip`
(**Pack into one file**, on by default); about two minutes on a fast desktop.
The data is about 5 GB, and packing needs as much again while it runs. Stopped
part way, the next setup carries on from the stages it finished. **Play** then
starts the game.

### From a shell

The launcher does the same without a window. It needs read access to the folder
holding the image, granted for that one command:

```sh
flatpak run --filesystem="$HOME/Games:ro" --command=noktus-launcher org.themoonpeople.Chronicle \
    --setup "$HOME/Games/Dark Cloud (PAL).iso"
```

`--folder` after the image leaves the data a folder rather than packing it.

## Where things live

Inside the sandbox `XDG_DATA_HOME` is `~/.var/app/org.themoonpeople.Chronicle/data`,
so:

| | |
|---|---|
| game data | `~/.var/app/org.themoonpeople.Chronicle/data/chronicle/data.zip` (or `data/`, unpacked) |
| saves, `config.json` | `~/.var/app/org.themoonpeople.Chronicle/data/chronicle/save` |

`config.json` is described in `docs/PC.md`. `flatpak uninstall --delete-data
org.themoonpeople.Chronicle` removes both. The usual options still apply:
`flatpak run org.themoonpeople.Chronicle --width 1920 --height 1080`.

## Controls

Keyboard, mouse and gamepads as in `docs/PC.md`, "Keyboard and mouse".

## Permissions

| finish-arg | why |
|---|---|
| `--socket=wayland` | the window, on Wayland |
| `--socket=fallback-x11` | the window on an X11 session; ignored when Wayland is there |
| `--share=ipc` | X11 shared memory, without which fallback-x11 is slow or broken |
| `--device=dri` | the GPU, for Vulkan |
| `--socket=pulseaudio` | sound, through PipeWire's or PulseAudio's PulseAudio socket |
| `--device=all` | gamepads: `/dev/input`, and `/dev/hidraw*` for those SDL reads through HIDAPI `--device=input` covers only `/dev/input` |

There is no network and no filesystem access. The disc reaches the game
through the file-chooser portal, which every Flatpak may call without a
`--talk-name`.

## Building

```sh
port/flatpak.sh
```

builds `chronicle.flatpak` in the repository root from the working tree, with
a copy in `release/flatpak/`, installing the runtime, the SDK and the llvm22 extension from Flathub when
they are missing. `INSTALL=1` also installs the bundle for this user;
`CLEAN=1` discards flatpak-builder's cache first. By hand, leaving out that copy, the same is:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.freedesktop.Platform//26.08 org.freedesktop.Sdk//26.08 \
    org.freedesktop.Sdk.Extension.llvm22//26.08
flatpak-builder --repo=repo build-dir port/flatpak/org.themoonpeople.Chronicle.yml
flatpak build-bundle repo chronicle.flatpak org.themoonpeople.Chronicle
```

(`flatpak-builder --install-deps-from=flathub ...` installs the three for
you.) Run from the repository root; `build-dir/`, `repo/`, `.flatpak-builder/`
and `chronicle.flatpak` are ignored by git. `flatpak-builder --user --install
build-dir port/flatpak/org.themoonpeople.Chronicle.yml` installs the build
directly instead of through a bundle.

The manifest (`port/flatpak/org.themoonpeople.Chronicle.yml`) builds only the
port; SDL3 and the Vulkan headers come from the SDK, and nlohmann/json's header
from its release archive, for the build alone:

- **The port**, `PLATFORM=PC`, Release, with clang 22 from the
  `org.freedesktop.Sdk.Extension.llvm22` extension (the SDK's own compiler is
  GCC, and the port is built with clang everywhere). Shaders are compiled with
  the SDK's glslang, which targets `vulkan1.4`. The SDK's default compiler flags are
  replaced: they add `_GLIBCXX_ASSERTIONS`, `-Werror=format-security` and
  `-g`, none of which the game is built with elsewhere.

The app module's source is the working tree (`type: dir`); CI swaps it for a
`git` source at the commit under test. The working tree includes
`tools/kazarov` as it is checked out, so after pulling, `git submodule update
--init tools/kazarov` first, or the build mixes the new code with the old
Kazarov. Vulkan validation layers are not shipped. The bundle carries no Vulkan
driver of its own: the runtime's Mesa (`org.freedesktop.Platform.GL.default`)
or the host's NVIDIA driver extension provides it.

### Dependencies

The build sandbox has no network, so everything Kazarov's CMake would
download for the port is one of the app module's sources instead, at the hash
Kazarov declares, which flatpak-builder fetches and checks before the sandbox
starts:

- each single file `_kz_port_vendor` takes (`dr_wav.h`, `stb_truetype.h`...),
  in `audio_decoders/` or `stb_truetype/`, the `KZ_PORT_VENDOR_DIRS` of
  `port/CMakeLists.txt`;
- each library a `FetchContent_Declare` names in a Kazarov module Chronicle
  includes (`cmake/KazarovPort.cmake` for the game, `cmake/KazarovRust.cmake`
  for `dcdata`) or a module those include (simdjson and fastgltf, for
  `KazarovGltf.cmake`; Corrosion, for `KazarovRust.cmake`), as an archive in
  `flatpak-deps/<name>`, which `port/CMakeLists.txt` hands FetchContent as
  `FETCHCONTENT_SOURCE_DIR_<NAME>`. nlohmann/json, declared with
  `FIND_PACKAGE_ARGS`, is a module of its own and is found installed;
- every crate Kazarov's `Cargo.lock` names, for the Rust extractor `dcdata`
  links, from `port/flatpak/cargo-sources.json`. `python3
  port/flatpak/cargo_sources.py` writes it from the lock.

`dcdata` also needs a Rust toolchain, `cxxbridge` and FFmpeg, which are
modules of their own, for the build alone: Rust at the version
`tools/kazarov/rust-toolchain.toml` pins, `cxxbridge-cmd` built from the
vendored crates, and FFmpeg with the LGPL configuration of Kazarov's
`tools/ffmpeg/kz-ffmpeg`, linked statically. Kazarov installs an empty gameplay
seed, `bin/data.zip`, which the module's cleanup removes: a `data.zip` beside
`darkcloud` would make `/app/bin/data` the data root.

The module configures with `FETCHCONTENT_FULLY_DISCONNECTED`, so a library the
manifest leaves out stops the configure at once, naming it, instead of after
the download's retries. A Kazarov pin that adds a download or changes a hash
needs the manifest to follow: `python3 scripts/port/flatpak_sources.py`
compares the two, prints the entry to add for anything missing, checks
`cargo-sources.json` against `Cargo.lock`, and fails the Linux CI job until
they agree.
