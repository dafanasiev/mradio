# D-Bus introspection XML for the mradio tray

These three files are the *input* to `sdbus-c++-xml2cpp`. Nothing here is compiled
directly; the generator turns each file into a single C++ header that our own
classes inherit from.

| File | Interface | Role | Generate with |
|---|---|---|---|
| `status_notifier_item.xml` | `org.kde.StatusNotifierItem` | our object | `--adaptor` |
| `dbusmenu.xml` | `com.canonical.dbusmenu` | our object | `--adaptor` |
| `status_notifier_watcher.xml` | `org.kde.StatusNotifierWatcher` | the shell's object | `--proxy` |

```sh
sdbus-c++-xml2cpp --adaptor=sni_adaptor.h      status_notifier_item.xml
sdbus-c++-xml2cpp --adaptor=dbusmenu_adaptor.h dbusmenu.xml
sdbus-c++-xml2cpp --proxy=watcher_proxy.h      status_notifier_watcher.xml
```

Verified against sdbus-c++ 2.2.1 / g++ 15.2.0 / CMake 4.4.3, compiled with
`-std=c++23 -Wall -Wextra -Werror` and exercised live on a private session bus.

## Where the specs come from

* `status_notifier_item.xml`, `status_notifier_watcher.xml` — KDE Frameworks
  **`kstatusnotifieritem`**, `src/org.kde.StatusNotifierItem.xml` and
  `src/org.kde.StatusNotifierWatcher.xml`. That module is the reference
  implementation of the freedesktop *StatusNotifierItem* specification and is what
  Plasma's system tray actually talks to; Ayatana AppIndicator speaks the same
  wire protocol under the `org.ayatana.*` names.
* `dbusmenu.xml` — Canonical/Ayatana **`libdbusmenu`**, `libdbusmenu-glib/dbus-menu.xml`.

Both upstream files were fetched verbatim and then edited only as described
below. The prose in the XML comments is condensed from the upstream
documentation (`<dox:d>` blocks in the libdbusmenu file).

> Note: `www.freedesktop.org` and `invent.kde.org` are blocked by the sandbox
> network policy ("no matching allow rule — blocked by default deny policy"), so
> the upstream sources were taken from their official Git mirrors on GitHub
> (`KDE/kstatusnotifieritem`, `deepin-community/libdbusmenu`), which carry the
> same files.

## What was changed relative to upstream, and why

1. **Qt annotations dropped.** Upstream carries
   `<annotation name="org.qtproject.QtDBus.QtTypeName*" .../>` on `IconPixmap`,
   `ToolTip`, `RegisteredStatusNotifierItems`, etc. Those only steer Qt's
   `qdbusxml2cpp` towards `KDbusImageVector` / `QStringList`; `sdbus-c++-xml2cpp`
   ignores them and derives its own STL types from the signature.
2. **`direction="out"` removed from signal arguments.** The libdbusmenu file puts
   `direction="out"` on the args of `ItemsPropertiesUpdated`, `LayoutUpdated` and
   `ItemActivationRequested`. Signal arguments have no direction in D-Bus.
   `xml2cpp` tolerates it, stricter parsers do not.
3. **`<dox:d>` documentation elements and the `xmlns:dox` declaration removed**
   from the dbusmenu file. They make it unreadable and mean nothing to `xml2cpp`.
4. **`<!DOCTYPE>` removed.** Harmless, but it points at an external DTD on
   `freedesktop.org` and buys us nothing.

Everything else — every method, signal, property, argument name and signature —
is byte-for-byte the upstream contract.

## What is deliberately *not* here

* **`org.freedesktop.DBus.Properties` / `.Introspectable` / `.Peer`.** sdbus-c++
  installs these automatically for every exported object. Listing them would
  produce a duplicate vtable registration.
* **`org.kde.StatusNotifierWatcher` adaptor.** We are a tray *item*, never a tray
  *host*. `RegisterStatusNotifierHost` is kept in the XML only so the generated
  proxy can be reused for diagnostics; mradio never calls it.
* **A dbusmenu *proxy*.** Nobody calls our menu but the shell.
* **`com.canonical.dbusmenu`'s deprecated v1 bits** (`ItemPropertyUpdated`,
  `ItemUpdated`). They are not in the current upstream file either.

## What xml2cpp produces

Namespaces come from the interface name minus its last component; the class name
is the last component with `_adaptor` / `_proxy` appended — so the dbusmenu class
really is spelled lowercase.

| XML | Class |
|---|---|
| `status_notifier_item.xml` | `org::kde::StatusNotifierItem_adaptor` |
| `status_notifier_watcher.xml` | `org::kde::StatusNotifierWatcher_proxy` |
| `dbusmenu.xml` | `com::canonical::dbusmenu_adaptor` |

Each exposes `static constexpr const char* INTERFACE_NAME`.

Usage is the standard sdbus-c++ shape: inherit
`sdbus::AdaptorInterfaces<...>`, call `registerAdaptor()` in the constructor and
`unregisterAdaptor()` in the destructor, and implement the pure virtuals. Note
that the generated pure virtuals are declared **`private`** — implementing them as
private `override`s in the derived class is correct and compiles.

### The recursive dbusmenu layout type

`GetLayout`'s `(ia{sv}av)` out-argument becomes:

```cpp
using MenuItem = sdbus::Struct<int32_t,                              // id
                               std::map<std::string, sdbus::Variant>,// properties
                               std::vector<sdbus::Variant>>;         // children
```

The recursion terminates because each child is *type-erased into a
`sdbus::Variant`* that again holds a `MenuItem`. This is a complete and working
representation — no workaround, no hand-written `sdbus::MethodCall` needed.
Build a tree like this:

```cpp
static MenuItem makeItem(int32_t id,
                         std::map<std::string, sdbus::Variant> props,
                         std::vector<MenuItem> children = {})
{
    std::vector<sdbus::Variant> kids;
    kids.reserve(children.size());
    for (const auto& c : children)
        kids.emplace_back(c);          // v <- (ia{sv}av)
    return MenuItem{id, std::move(props), std::move(kids)};
}
```

and read one back with `item.get<0>()` / `get<1>()` / `get<2>()`, unwrapping each
child with `variant.get<MenuItem>()`.

`GetLayout` itself returns `std::tuple<uint32_t, MenuItem>` (revision, layout),
because sdbus-c++ packs multiple out-arguments into a tuple. Same for
`AboutToShowGroup`, which returns
`std::tuple<std::vector<int32_t>, std::vector<int32_t>>`.

### Signal emitters

The adaptors expose `emit` + the signal name:

* SNI: `emitNewTitle()`, `emitNewIcon()`, `emitNewAttentionIcon()`,
  `emitNewOverlayIcon()`, `emitNewToolTip()`, `emitNewMenu()`,
  `emitNewStatus(const std::string&)`
* dbusmenu: `emitLayoutUpdated(uint32_t revision, int32_t parent)`,
  `emitItemsPropertiesUpdated(...)`, `emitItemActivationRequested(int32_t id, uint32_t timestamp)`

## Traps

* **`--` inside an XML comment is a hard parse error.** A separator line like
  `<!-- ---- main icon ---- -->` makes `xml2cpp` fail with
  `Parsing error: line N, column M: not well-formed (invalid token)`. This bit us
  once already; the comments here are written to avoid it.
* **One C++ class cannot implement both adaptors.** `StatusNotifierItem_adaptor`
  and `dbusmenu_adaptor` both declare `IconThemePath()`, returning `std::string`
  and `std::vector<std::string>` respectively — they differ only in return type,
  so a combined class is a hard compile error ("cannot be overloaded"). Both also
  declare `std::string Status()` with unrelated meanings ("Active" vs "normal").
  Use **two classes on two object paths**, which is what the protocol wants
  anyway (`/StatusNotifierItem` and `/MenuBar`).
* **Generated headers include `<string>` and `<tuple>` but not `<vector>`/`<map>`**,
  which they use. It compiles only because `<sdbus-c++/sdbus-c++.h>` pulls those
  in transitively. Don't rely on the generated header standing alone.
* **The include guard embeds the absolute output path**
  (`__sdbuscpp___tmp_tray_check_sni_adaptor_h__adaptor__H__`), so generate into
  the build tree and never commit the result.
* **Properties are advertised `emits-change` by default**, but the SNI spec
  signals property changes via the `NewXxx` signals, not
  `PropertiesChanged`. If you want the introspection to say so, add
  `<annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="false"/>`
  to a property — verified: `xml2cpp` turns it into
  `.withUpdateBehavior(sdbus::Flags::EMITS_NO_SIGNAL)`. Left at the default here
  so that hosts which *do* watch `PropertiesChanged` keep working.
* **`sdbus-c++`'s async event loop calls `std::terminate` if the bus dies.**
  `enterEventLoopAsync()` throws `[System.Error.ENOTCONN] Failed to get bus poll
  data` from its own thread when the session bus goes away. A long-lived daemon
  must handle that rather than let it abort.
* There is no `StatusNotifierWatcher` outside a real desktop session, so
  `RegisterStatusNotifierItem` fails with
  `org.freedesktop.DBus.Error.ServiceUnknown`. Treat it as "no tray available
  yet", and re-register when `StatusNotifierHostRegistered` arrives (or when the
  name shows up via `NameOwnerChanged`).
