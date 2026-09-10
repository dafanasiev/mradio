# mradio

Internet radio player for Linux. No window: it lives in the system tray and is
driven from the tray menu or over D-Bus.

Pick a station from the menu and it plays, with a `▶` in front of its name.
The menu also has **Стоп** and **Выйти**. The wheel over the tray icon changes
the volume. MPRIS is published, so media keys, panel applets and `playerctl`
work without knowing anything about mradio.

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

Dependencies, all from apt: `libmpv-dev`, `libsdbus-c++-dev`,
**`libsdbus-c++-bin`**, `catch2`. The `-bin` package holds the code generator
and is attached to `-dev` only through `Suggests`, so it has to be named
explicitly; `mradio_require_xml2cpp()` fails the configure step with that
advice if it is missing.

Running it headless (no sound card) needs `MRADIO_AUDIO_OUTPUT=null`. That
environment variable is the program's only knob.

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
| `mpris` | `org.mpris.MediaPlayer2` view |
| `tray` | `org.kde.StatusNotifierItem` + `com.canonical.dbusmenu` view |
| `app` | the only executable; wires everything and runs the loop |

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
