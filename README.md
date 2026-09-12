# mradio

An internet radio player for Linux with no window of its own. It lives in the
system tray and on D-Bus: pick a station from the tray menu, or drive it with
the media keys, a panel applet or `playerctl`.

No GUI toolkit is linked. The tray icon and its menu are pure D-Bus protocols
(StatusNotifierItem and dbusmenu), so the panel draws both itself. Audio is
libmpv.

## Building

Dependencies, all from apt:

```sh
sudo apt install libmpv-dev libsdbus-c++-dev libsdbus-c++-bin catch2
```

`libsdbus-c++-bin` holds the D-Bus code generator and is only *suggested* by
`-dev`, so it has to be named explicitly.

```sh
make            # configure and build into build/
make test       # build, then run the test suite
make help       # list every target
```

The binary lands at `build/packages/app/mradio`.

## Stations

There is no configuration file, and nothing is remembered between runs. The one
thing mradio reads — once, at startup — is

```
$XDG_CONFIG_HOME/mradio/playlist.m3u
```

falling back to `~/.config/mradio/playlist.m3u`. Create it yourself:

```sh
mkdir -p ~/.config/mradio
cat > ~/.config/mradio/playlist.m3u <<'EOF'
#EXTM3U
#EXTINF:-1,SomaFM Groove Salad
https://ice1.somafm.com/groovesalad-128-mp3
#EXTINF:-1,Radio Jazz
http://example.org/jazz
EOF
```

A bare URL with no `#EXTINF` line works too, and is named after its host. A
line mradio cannot make sense of is a warning on stderr, not a refusal to
start — it comes up with whatever stations it did understand. Edit the file and
restart to pick changes up.

## Running

```
mradio [--with-tray] [--without-mpris]
```

| flags | what you get |
|---|---|
| *none* | MPRIS only |
| `--with-tray` | MPRIS and the tray icon |
| `--with-tray --without-mpris` | the tray icon alone |
| `-h`, `--help` | this list, on stdout |

MPRIS costs nothing to publish and is what the rest of the desktop already
knows how to talk to, so it is on until `--without-mpris` turns it off. The
tray icon is a visible thing on somebody's panel, so it waits to be asked for.
One of the two has to remain: with neither there would be no way to pick a
station or to quit, so mradio says so and exits 2 — as it does for an argument
it does not know.

One environment variable, `MRADIO_AUDIO_OUTPUT`, overrides mpv's choice of
audio output. Set it to `null` on a machine with no sound card.

```sh
mradio --with-tray                        # the usual way
MRADIO_AUDIO_OUTPUT=null mradio           # headless, MPRIS only
```

The `.desktop` file installed with mradio runs `mradio --with-tray`.

## Controlling it

The tray menu lists every station, then **Stop** and **Quit**. Whichever
station is on carries a `▶`.

Over D-Bus, anything that speaks `org.mpris.MediaPlayer2` works without knowing
anything about mradio — the media keys, panel applets, `playerctl`:

```sh
playerctl -p mradio play-pause
playerctl -p mradio next
playerctl -p mradio volume 0.5      # mradio itself offers no volume gesture
playerctl -p mradio metadata
```

The station list also goes out as an MPRIS *track list*, which is how a client
sees every station and starts a particular one:

```sh
S=org.mpris.MediaPlayer2.mradio
P=/org/mpris/MediaPlayer2
I=org.mpris.MediaPlayer2.TrackList

busctl --user call $S $P $I GetTracksMetadata ao 0                    # every station
busctl --user call $S $P $I GoTo o /org/mpris/MediaPlayer2/mradio/station/1
```

There is no pause. Pausing a live stream only throws away buffered audio, so
there is play and stop and nothing in between; `PlayPause` maps onto
play-or-stop and `CanPause` says `false`.

A stream that drops is reconnected to on a doubling delay, with the tray and
MPRIS still saying the station is on. A URL that is simply wrong is given up on
after about two minutes.

## License

MIT — see [LICENSE.md](LICENSE.md).
