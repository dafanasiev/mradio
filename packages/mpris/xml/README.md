# MPRIS D-Bus introspection data

`mpris.xml` is the D-Bus introspection description of mradio's MPRIS2 API. It is the input to
`sdbus-c++-xml2cpp`, which generates the server-side adaptor (and, for tests, the client-side
proxy) classes. The XML is the single source of truth for the D-Bus surface: nothing about
methods, signatures or property flags should be hand-written in C++.

## Where the specification comes from

The contents are taken verbatim from the machine-readable MPRIS 2.2 specification files:

- `spec/org.mpris.MediaPlayer2.xml`
- `spec/org.mpris.MediaPlayer2.Player.xml`

published by freedesktop.org (rendered at <https://specifications.freedesktop.org/mpris-spec/latest/>,
sources mirrored at <https://github.com/freedesktop-unofficial-mirror/xdg__mpris-spec>).

Member names, D-Bus signatures, argument names/directions and the
`org.freedesktop.DBus.Property.EmitsChangedSignal` annotations are copied unchanged. What was
removed: the `tp:` Telepathy documentation vocabulary (docstrings, `tp:type` enum references,
`tp:name-for-bindings`), which is documentation only and is not part of the wire API. What was
added: comments describing how each member behaves for an internet-radio player.

The object is exported at `/org/mpris/MediaPlayer2` under the well-known name
`org.mpris.MediaPlayer2.mradio`, as the specification requires.

## What xml2cpp generates

```sh
sdbus-c++-xml2cpp --adaptor=mpris_adaptor.h --proxy=mpris_proxy.h mpris.xml
```

The namespace is derived from the interface name: all components but the last become nested
namespaces, and the last component becomes the class name with an `_adaptor` / `_proxy` suffix.

| Interface | Adaptor class | Proxy class |
|---|---|---|
| `org.mpris.MediaPlayer2` | `org::mpris::MediaPlayer2_adaptor` | `org::mpris::MediaPlayer2_proxy` |
| `org.mpris.MediaPlayer2.Player` | `org::mpris::MediaPlayer2::Player_adaptor` | `org::mpris::MediaPlayer2::Player_proxy` |

Note that `org::mpris::MediaPlayer2` is both a class-name prefix (`MediaPlayer2_adaptor`) and a
namespace (holding `Player_adaptor`). These are distinct identifiers, so they coexist fine.

Each adaptor declares its methods and property accessors as **private pure virtual** functions and
provides a `registerAdaptor()` that builds the vtable. The implementation class inherits from
`sdbus::AdaptorInterfaces<...>` and overrides them (overriding a private virtual is legal C++);
`sdbus::Properties_adaptor` is mixed in to obtain `emitPropertiesChangedSignal()`.

The `EmitsChangedSignal` annotation is translated into
`.withUpdateBehavior(sdbus::Flags::EMITS_CHANGE_SIGNAL)` for `"true"` and
`sdbus::Flags::EMITS_NO_SIGNAL` for `"false"`. Emitting `PropertiesChanged` for a property
registered as `EMITS_NO_SIGNAL` throws `sdbus::Error` (`System.Error.EDOM`) and sends nothing —
and one such name poisons the whole batch, so `Position` and `CanControl` must never appear in a
property list passed to `emitPropertiesChangedSignal()`.

## Deliberate omissions

**`org.mpris.MediaPlayer2.TrackList` and `org.mpris.MediaPlayer2.Playlists`** — both are optional
in the specification. mradio plays a flat list of stations, not a queue of tracks with
`mpris:trackid` identities, and it has no playlist concept to expose. `HasTrackList` is therefore
`false`, which is exactly how a client is told not to look for the TrackList interface.

**`Fullscreen` and `CanSetFullscreen`** (root interface) — marked optional in the specification
and only meaningful for a player with a window. mradio is a headless daemon with a tray icon, so
there is nothing to make fullscreen. Clients treat their absence as "not supported"; declaring
`CanSetFullscreen=false` would be equivalent but implies a UI that does not exist.

**`org.freedesktop.DBus.Properties`, `.Introspectable`, `.Peer`** — not listed because sdbus-c++
(via sd-bus) implements them automatically for every exported object. Adding them here would
generate a second, conflicting registration. They show up in `busctl introspect` output regardless.

## Kept despite being no-ops for us

These are mandatory members of the specification, so they stay in the XML even though mradio's
implementation does nothing useful with them. Their "unsupported" status is advertised through the
corresponding `Can*` property, which is the mechanism the specification defines for this.

| Member | Why it is a no-op | Advertised by |
|---|---|---|
| `Raise()` | headless, no window to raise | `CanRaise = false` |
| `Seek()`, `SetPosition()` | live streams are not seekable | `CanSeek = false` |
| `Position` | always `0` | `CanSeek = false` |
| `Rate`, `MinimumRate`, `MaximumRate` | always `1.0`; writes to `Rate` are ignored | equal min/max |
| `Seeked` | never emitted (nothing seeks) | `CanSeek = false` |

`Next()` and `Previous()` are *not* no-ops: they step through the station list.
`Volume` is read-write and genuinely changes playback volume. `Quit()` is wired to the tray menu's
"Quit", hence `CanQuit = true`.

## Regenerating

The generated headers are build artefacts and are not checked in; the package's CMakeLists runs
`sdbus-c++-xml2cpp` at build time. After editing this XML, rebuild and re-check the exported API
with:

```sh
busctl --user introspect org.mpris.MediaPlayer2.mradio /org/mpris/MediaPlayer2
```
