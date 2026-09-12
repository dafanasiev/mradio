# mradio

Internet radio player for Linux. No window: it lives in the system tray and is
driven from the tray menu or over D-Bus.

It has two faces, and each has a flag for the side it is not on by default:

```bash
mradio                              # MPRIS only
mradio --with-tray                  # MPRIS and the tray icon, as the .desktop file runs it
mradio --with-tray --without-mpris  # the tray icon alone
mradio --help                       # the same list, on stdout
```

MPRIS costs nothing to publish and is what everything else on the desktop
already knows how to talk to, so it stays on until `--without-mpris`. The tray
icon is a visible thing on somebody's panel, so it waits to be asked for.

One of the two has to remain: `--without-mpris` on its own leaves no way to
pick a station or to quit, so it prints that and exits 2 - as does an argument
the program does not know. These two flags and `MRADIO_AUDIO_OUTPUT` are the
whole of the program's interface to the outside; there is still no config file.

Pick a station from the menu and it plays, with a `▶` in front of its name.
The menu also has **Стоп** and **Выйти**. MPRIS, unless it was turned off, is
published too, so media keys, panel applets and `playerctl` work without
knowing anything about mradio - including the volume, which mradio itself
offers no gesture for. The station list goes out as an MPRIS *track list*, so
a client can also see every station and start a particular one:

```bash
busctl --user call org.mpris.MediaPlayer2.mradio /org/mpris/MediaPlayer2 \
    org.mpris.MediaPlayer2.TrackList GoTo o \
    /org/mpris/MediaPlayer2/mradio/station/1
```

There is no configuration file and no saved state. The only thing read is
`$XDG_CONFIG_HOME/mradio/playlist.m3u` (falling back to
`~/.config/mradio/playlist.m3u`), at startup:

```
#EXTM3U
#EXTINF:-1,SomaFM Groove Salad
https://ice1.somafm.com/groovesalad-128-mp3
#EXTINF:-1,Радио Джаз
http://example.org/jazz
```

A bare URL with no `#EXTINF` works too and is named after its host.

## Building

```bash
make            # configure and build into build/
make test       # build, then run the whole suite
make help       # list every target
make clean      # remove build/ and every build-*/
```

The Makefile only ever shells out to CMake; the plain invocation works just as
well and is what the Makefile runs:

```bash
cmake -S . -B build -G Ninja -DMRADIO_WERROR=ON
cmake --build build
ctest --test-dir build
```

Sanitizers get a build directory each, so switching between them costs no
rebuild. Underscores in the target name become commas, since `-fsanitize` takes
a list and a comma cannot appear in a make target:

```bash
make sanitize_address_undefined   # -fsanitize=address,undefined
make sanitize_thread              # -fsanitize=thread
```

Both configurations are expected to stay clean, and the suite is run under them
before anything is called done. (`address` and `thread` cannot be combined -
that is the sanitizers' limitation, not the Makefile's.)

### Static linking

```bash
make static_cxx   # -DMRADIO_STATIC_CXX=ON, into build-static-cxx/
```

This links libstdc++ and libgcc into the binary and stops there, taking it from
five dynamic dependencies to four and from 1.9 MB to 3.8 MB. That is the half
of "static" worth having cheaply: libstdc++ is the dependency most likely to be
too old on another machine, whereas glibc is forward-compatible. The target
prints what the binary still needs when it finishes.

Going further is not a matter of flags. The distribution ships no `libmpv.a`,
and libmpv `dlopen`s its audio outputs (`libpulse`, `libpipewire`, `libasound`)
at runtime, which a fully static binary cannot do; statically linked glibc also
cannot resolve hostnames, since NSS itself needs `dlopen`. A genuinely static
build therefore means replacing `packages/audio` with an implementation over
the ffmpeg static libraries the distribution does ship - which is the reason
`IAudioEngine` exists, and the reason that change would touch one package and
no others.

Dependencies, all from apt: `libmpv-dev`, `libsdbus-c++-dev`,
**`libsdbus-c++-bin`**, `catch2`. The `-bin` package holds the code generator
and is attached to `-dev` only through `Suggests`, so it has to be named
explicitly; `mradio_require_xml2cpp()` fails the configure step with that
advice if it is missing.

Running it headless (no sound card) needs `MRADIO_AUDIO_OUTPUT=null`. That
environment variable is the program's only knob beyond `--with-tray` and
`--without-mpris`.

## Architecture

MVVM. One view model holds the behaviour; the tray and MPRIS are two passive
views bound to it, so a station started from either shows up in both.

```
       model                     view model                 views
  ┌──────────────┐          ┌──────────────────┐      ┌──────────────┐
  │ m3uparser    │          │                  │  ──► │ tray         │
  │ stations     │  ──────► │ PlayerViewModel  │      │  SNI+dbusmenu│
  │ core         │          │  ViewState       │      ├──────────────┤
  │ audio (mpv)  │  ◄────── │  commands        │  ──► │ mpris        │
  └──────────────┘          └──────────────────┘      └──────────────┘
                                     ▲
                                     │  ipc: session bus + I/O loop
                                     └──────────── app: composition root
```

Every directory under `packages/` is a standalone CMake project: configuring
one on its own works and resolves its siblings through `find_package`. That is
what stops a package from quietly depending on a target the aggregator happened
to define earlier.

| package | role |
|---|---|
| `core` | domain types: `Station`, `Volume`, `TrackInfo`, `IAudioEngine`. No dependencies at all |
| `m3uparser` | lenient extended-M3U parser, no dependencies |
| `stations` | resolves the XDG path and turns playlist text into a `StationList` |
| `audio` | `IAudioEngine` on libmpv |
| `viewmodel` | `PlayerViewModel`: behaviour plus the observable `ViewState` |
| `ipc` | session-bus connection and the I/O loop |
| `mpris` | `org.mpris.MediaPlayer2` view, including the `TrackList` the stations go out as |
| `tray` | `org.kde.StatusNotifierItem` + `com.canonical.dbusmenu` view |
| `app` | the only executable; parses the two flags, wires up what they ask for and runs the loop |

No GUI toolkit is linked. StatusNotifierItem and dbusmenu are pure D-Bus
protocols, so the panel draws the icon and the menu from published properties.

### Threads

Three: the main thread runs the D-Bus loop, mpv runs its own, and one more sits
in `sigwait` turning SIGINT/SIGTERM into a normal function call. `IConnection`
is thread-aware but *not* thread-safe, so it is only touched from the main
thread; emitting signals through `IObject` *is* documented thread-safe, which is
what lets the mpv thread publish `PropertiesChanged` directly.

## Conventions

- C++23, GCC. `-Wconversion -Wsign-conversion -Wold-style-cast` are on
  deliberately: this code juggles two volume scales and several integer widths.
- Errors are `core::Result<T>` (`std::expected`), never exceptions across a
  package boundary. sdbus throws; `ipc::from_sdbus` converts at the edge.
- Generated D-Bus headers go in the build tree, are included `SYSTEM PRIVATE`,
  and never appear in a public header - both D-Bus services are opaque handles.
- Tests are Catch2 and run without a network, a sound card or a real bus: the
  audio tests generate a WAV, the ipc tests start a private `dbus-daemon`.

## Things worth knowing before changing this

- **No pause.** Pausing a live stream only discards buffered audio, so there is
  play and stop and nothing between. MPRIS reports `CanPause=false` and maps
  `PlayPause` onto play-or-stop.
- **Nothing is remembered.** Not favourites, not the last station, not the
  volume. `stop()` clears the current station outright.
- **Over MPRIS, `mpris:trackid` names the station, not the song.** The spec
  wants the Player's `mpris:trackid` to be a track id of the TrackList, so it
  is one: `/org/mpris/MediaPlayer2/mradio/station/<index>`, the station's place
  in the playlist. It therefore stays put while a station plays one song after
  another. It used to be a counter bumped on every ICY title change, which is
  how some clients tell songs apart; `TrackMetadataChanged` carries that now,
  and `PropertiesChanged` on `Metadata` carries the new `xesam:title` either
  way. The index is the id because a `StationId` keeps the non-ASCII bytes of
  the station's name and an object path may hold nothing but `[A-Za-z0-9_]`
  between its slashes.
- **`--without-mpris` gives up the single-instance check too.** It was never a
  check of its own: it is `RequestName` on `org.mpris.MediaPlayer2.mradio`
  failing for the second copy. StatusNotifierItem needs no well-known name -
  the watcher knows a connection by its unique name - so a tray-only instance
  claims nothing and two of them can run. Claiming the MPRIS name anyway,
  without publishing the object under it, would be worse: `playerctl` and the
  panel applets would find a player that answers nothing.
- **A drop is retried, a wrong URL is not - for long.** ffmpeg reconnects
  underneath mpv (the `stream-lavf-o` options in `MpvEngine::create`), and it
  is kept on a short leash there on purpose: its reconnecting is invisible from
  our side. What it cannot paper over arrives as an mpv end-of-file error, and
  `MpvEngine` then re-issues `loadfile` on a doubling delay while reporting
  `connecting`, so the tray keeps its `▶` and MPRIS keeps saying `Playing`.
  `RetryPolicy::max_attempts` is a budget per stretch of bad luck, not per
  station: it is restored the moment audio flows again, which is what retries a
  flaky connection indefinitely while giving up on a dead URL in about two
  minutes. libmpv reports no HTTP status, so "temporarily down" and "gone for
  good" cannot be told apart any other way. The tests set it to 0.
- **mpv is brought up with its config and its Lua scripts off.** `config=no`
  keeps a stray `~/.config/mpv/mpv.conf` from silently becoming this
  program's settings, and the `load-*`/`ytdl` block turns off the built-in
  scripts - the OSD console, the stats overlay, the youtube-dl hook, the
  select, positioning and context menus - which libmpv otherwise runs on a
  thread each, for a player with no OSD, no key bindings and no video site to
  resolve. There is no single switch for them; each has its own, and mpv
  refuses an option it does not know, so listing one a given build lacks is
  harmless.
- **`block_termination_signals()` is the first line of `main()`, and has to
  stay there.** A thread inherits the signal mask of whoever created it, and a
  process-directed signal goes to any one thread that does not block it, so a
  single unblocked thread is enough for SIGTERM to kill the process by default
  disposition - no unwinding, no `Bus::stop()`, exit 143. libmpv brings up
  several threads of its own, which is how this went wrong when the blocking
  still lived in `SignalWaiter`'s constructor: the waiter is built last, by
  which time it was far too late. A signal arriving between the blocking and
  the waiter is not lost, only pending.
- `emitPropertiesChangedSignal` **throws and drops the whole batch** if any
  named property is declared `EmitsChangedSignal="false"`. For MPRIS that means
  `Position` and `CanControl` must never appear in the list.
- In CMake a quote that starts mid-token is a literal character:
  `--adaptor="${dir}/x.h"` passes the quotes to the generator. Quote the whole
  argument instead.
- The upstream m3u8 parser this project started from was dropped: it silently
  discarded URL lines that had no `#EXTINF`, threw on `#EXTINF:,Name`, indexed
  an empty vector on `#EXTINF:`, left `\r` on URLs from CRLF files, truncated
  titles containing commas, and pulled in nlohmann/json for what is a list of
  name/URL pairs. `packages/m3uparser` replaces it in under 200 lines.
