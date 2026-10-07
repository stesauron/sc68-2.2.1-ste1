# sc68 2.2.1 for Apple Silicon macOS

This is sc68, Benjamin Gerard's Atari ST and Amiga music player, from 2003.
It has been patched so it builds and runs on modern arm64 macOS, and it adds
`sc68play`, a small player that plays sc68 and SNDH files straight to your
speakers through CoreAudio.

The original upstream README is kept as [`README`](README).

## Prerequisites

You only need the Xcode Command Line Tools:

```sh
xcode-select --install
```

You don't need Homebrew. The only libraries are zlib, AudioToolbox, and
CoreFoundation, and they all come with macOS.

## Building

```sh
make -f Makefile.arm64
```

This produces the following in `build/`:

| Binary     | Purpose                                                    |
|------------|------------------------------------------------------------|
| `sc68play` | Plays a file through the speakers (CoreAudio)              |
| `sc68`     | Upstream CLI; writes raw 16-bit stereo 44.1 kHz PCM to stdout |
| `info68`   | Prints metadata (title, author, tracks) for a music file   |

To build for Intel Macs, use `make -f Makefile.arm64 ARCH=x86_64`.

> **Note:** don't use the original `./configure && make`. The 2003-era
> autoconf/libtool doesn't recognise arm64. `Makefile.arm64` replaces it.

## Playing music (without installing)

The `./sc68play` wrapper script in the top directory runs the player from the
source tree. It points the player at the bundled replay routines in `data/`:

```sh
./sc68play data/Sample/About-Intro.sc68
./sc68play --track=3 mytune.sndh
```

Options:

```
--track=N    play track N (default: first track)
--seconds=N  stop after N seconds
--loop       keep looping instead of stopping at the end
--quiet      do not print track information
```

Press Ctrl-C to stop.

You can find SNDH files at <https://sndh.atari.org/>.

## Installing

```sh
sudo make -f Makefile.arm64 install                 # into /usr/local
make -f Makefile.arm64 install PREFIX=$HOME/.local  # or anywhere you like
```

This installs `sc68`, `info68`, and `sc68play` into `$PREFIX/bin`, and the
replay data into `$PREFIX/share/sc68`. The data path is compiled in, so if you
change `PREFIX`, build and install with the same value. Once installed, run
`sc68play` directly, without the wrapper.

## What changed from upstream

- `Makefile.arm64`: a standalone build that doesn't use autoconf/libtool.
- `macos/sc68play.c` and `sc68play`: a new CoreAudio player and its wrapper script.
- 64-bit and modern-compiler fixes in `api68`, `file68`, `io68`, `sc68`, and
  the pre-generated `config_*.h` headers.

The other tools in the upstream tree (`as68`, `debug68`, `sourcer68`, and so
on) aren't built by `Makefile.arm64`.

## License

GNU General Public License v2 or later. See [`COPYING`](COPYING).
Copyright (C) 1998-2003 Benjamin Gerard.
