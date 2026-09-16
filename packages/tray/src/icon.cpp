#include "mradio/tray/icon.hpp"

#include "mradio/ipc/bus.hpp"
#include "mradio/tray/menu_model.hpp"

#include "dbusmenu_adaptor.h"
#include "sni_adaptor.h"
#include "watcher_proxy.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mradio::tray {
namespace {

constexpr const char* kItemPath = "/StatusNotifierItem";
constexpr const char* kMenuPath = "/MenuBar";
constexpr const char* kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr const char* kWatcherPath = "/StatusNotifierWatcher";

// From the freedesktop icon-naming specification, so every icon theme has it.
// This is the one line to change for a different look in the panel.
constexpr const char* kIconName = "multimedia-player";

// The recursive dbusmenu layout type, (ia{sv}av). The recursion closes through
// type erasure: every child is a Variant that again holds one of these.
using MenuItem = sdbus::Struct<std::int32_t,
                               std::map<std::string, sdbus::Variant>,
                               std::vector<sdbus::Variant>>;

using ItemProperties = sdbus::Struct<std::int32_t, std::map<std::string, sdbus::Variant>>;

using Pixmap = sdbus::Struct<std::int32_t, std::int32_t, std::vector<std::uint8_t>>;
using Pixmaps = std::vector<Pixmap>;
using ToolTipValue = sdbus::Struct<std::string, Pixmaps, std::string, std::string>;

std::map<std::string, sdbus::Variant> properties_of(const MenuEntry& entry)
{
    std::map<std::string, sdbus::Variant> properties;

    if (entry.is_separator) {
        properties.emplace("type", sdbus::Variant{std::string{"separator"}});
        return properties;
    }

    properties.emplace("label", sdbus::Variant{entry.label});
    properties.emplace("enabled", sdbus::Variant{true});
    properties.emplace("visible", sdbus::Variant{true});
    return properties;
}

// ---------------------------------------------------------------- the menu

class MenuObject final : public sdbus::AdaptorInterfaces<com::canonical::dbusmenu_adaptor> {
public:
    MenuObject(sdbus::IConnection& connection,
               vm::PlayerViewModel& view_model,
               std::function<void()> quit,
               bool offer_sort)
        : AdaptorInterfaces(connection, sdbus::ObjectPath{kMenuPath}),
          view_model_(view_model),
          model_(view_model.stations(), offer_sort),
          quit_(std::move(quit))
    {
        registerAdaptor();
    }

    ~MenuObject() { unregisterAdaptor(); }

    MenuObject(const MenuObject&) = delete;
    MenuObject& operator=(const MenuObject&) = delete;

    // The play glyph moved, or the order did: tell the host its copy of the
    // layout is stale.
    void refresh()
    {
        const std::uint32_t revision = revision_.fetch_add(1) + 1;
        try {
            emitLayoutUpdated(revision, MenuModel::root_id());
        }
        catch (const sdbus::Error&) {
            // A dead bus must not take playback down with it.
        }
    }

private:
    [[nodiscard]] std::vector<MenuEntry> current_entries() const
    {
        return model_.entries(view_model_.view_state().station);
    }

    void dispatch(const MenuAction& action)
    {
        switch (action.kind) {
            case MenuAction::Kind::play:
                // A dead stream reports itself through the view state; there
                // is nothing useful to do with the return value here.
                static_cast<void>(view_model_.play(action.station));
                break;
            case MenuAction::Kind::stop:
                view_model_.stop();
                break;
            case MenuAction::Kind::quit:
                if (quit_) {
                    quit_();
                }
                break;
            case MenuAction::Kind::toggle_sort:
                // Order is a property of this menu and of nothing else, so
                // whatever is playing keeps playing; only the layout is stale.
                model_.toggle_sort_by_name();
                refresh();
                break;
            case MenuAction::Kind::none:
                break;
        }
    }

    std::tuple<std::uint32_t, MenuItem> GetLayout(
        const std::int32_t& parentId,
        const std::int32_t& /*recursionDepth*/,
        const std::vector<std::string>& /*propertyNames*/) override
    {
        // The menu is a single flat level, so recursionDepth changes nothing.
        // propertyNames is only a host-side optimisation, and answering with
        // more than was asked for is allowed, so it is ignored as well.
        const std::vector<MenuEntry> entries = current_entries();
        const std::uint32_t revision = revision_.load();

        if (parentId != MenuModel::root_id()) {
            for (const MenuEntry& entry : entries) {
                if (entry.id == parentId) {
                    return {revision, MenuItem{entry.id, properties_of(entry), {}}};
                }
            }
            return {revision, MenuItem{parentId, {}, {}}};
        }

        std::vector<sdbus::Variant> children;
        children.reserve(entries.size());
        for (const MenuEntry& entry : entries) {
            children.emplace_back(MenuItem{entry.id, properties_of(entry), {}});
        }

        std::map<std::string, sdbus::Variant> root;
        root.emplace("children-display", sdbus::Variant{std::string{"submenu"}});

        return {revision, MenuItem{MenuModel::root_id(), std::move(root), std::move(children)}};
    }

    std::vector<ItemProperties> GetGroupProperties(
        const std::vector<std::int32_t>& ids,
        const std::vector<std::string>& /*propertyNames*/) override
    {
        std::vector<ItemProperties> result;

        for (const MenuEntry& entry : current_entries()) {
            // An empty id list means "every item", per the dbusmenu spec.
            const bool wanted =
                ids.empty() || std::find(ids.begin(), ids.end(), entry.id) != ids.end();
            if (wanted) {
                result.emplace_back(entry.id, properties_of(entry));
            }
        }

        return result;
    }

    sdbus::Variant GetProperty(const std::int32_t& id, const std::string& name) override
    {
        for (const MenuEntry& entry : current_entries()) {
            if (entry.id != id) {
                continue;
            }
            const std::map<std::string, sdbus::Variant> properties = properties_of(entry);
            if (const auto found = properties.find(name); found != properties.end()) {
                return found->second;
            }
        }

        // Reported as an empty string rather than an empty Variant: a Variant
        // with no type cannot be put on the wire.
        return sdbus::Variant{std::string{}};
    }

    void Event(const std::int32_t& id,
               const std::string& eventId,
               const sdbus::Variant& /*data*/,
               const std::uint32_t& /*timestamp*/) override
    {
        if (eventId == "clicked") {
            dispatch(model_.action_of(id));
        }
    }

    std::vector<std::int32_t> EventGroup(
        const std::vector<sdbus::Struct<std::int32_t, std::string, sdbus::Variant, std::uint32_t>>&
            events) override
    {
        for (const auto& event : events) {
            if (event.get<1>() == "clicked") {
                dispatch(model_.action_of(event.get<0>()));
            }
        }

        // The return value lists ids that could not be handled; every id is
        // meaningful here, including the ones that mean "do nothing".
        return {};
    }

    bool AboutToShow(const std::int32_t& /*id*/) override
    {
        // True means "re-read the layout before showing it". The play glyph
        // has to be right at the moment the menu opens, and rebuilding a menu
        // of a handful of entries costs nothing.
        return true;
    }

    std::tuple<std::vector<std::int32_t>, std::vector<std::int32_t>> AboutToShowGroup(
        const std::vector<std::int32_t>& ids) override
    {
        return {ids, {}};  // all updated, none in error
    }

    std::uint32_t Version() override { return 3; }
    std::string TextDirection() override { return "ltr"; }
    std::string Status() override { return "normal"; }
    std::vector<std::string> IconThemePath() override { return {}; }

    vm::PlayerViewModel& view_model_;
    MenuModel model_;
    std::function<void()> quit_;
    std::atomic<std::uint32_t> revision_{1};
};

// ---------------------------------------------------------------- the icon

class ItemObject final : public sdbus::AdaptorInterfaces<org::kde::StatusNotifierItem_adaptor> {
public:
    ItemObject(sdbus::IConnection& connection, vm::PlayerViewModel& view_model)
        : AdaptorInterfaces(connection, sdbus::ObjectPath{kItemPath}), view_model_(view_model)
    {
        registerAdaptor();
    }

    ~ItemObject() { unregisterAdaptor(); }

    ItemObject(const ItemObject&) = delete;
    ItemObject& operator=(const ItemObject&) = delete;

    void refresh()
    {
        try {
            emitNewToolTip();
        }
        catch (const sdbus::Error&) {
        }
    }

private:
    // The host opens the menu itself, because ItemIsMenu is true.
    void ContextMenu(const std::int32_t& /*x*/, const std::int32_t& /*y*/) override {}
    void Activate(const std::int32_t& /*x*/, const std::int32_t& /*y*/) override {}

    void SecondaryActivate(const std::int32_t& /*x*/, const std::int32_t& /*y*/) override
    {
        // Middle click: the one gesture that is free, and start/stop is the
        // only thing worth binding it to.
        view_model_.toggle_play_stop();
    }

    // The wheel is deliberately unbound. Hosts disagree about whether they
    // deliver this event at all and about what a delta means, so the volume is
    // left to the mixer and to MPRIS, which every panel already drives.
    void Scroll(const std::int32_t& /*delta*/, const std::string& /*orientation*/) override {}

    void ProvideXdgActivationToken(const std::string& /*token*/) override {}

    std::string Category() override { return "Multimedia"; }
    std::string Id() override { return "mradio"; }
    std::string Title() override { return "mradio"; }
    std::string Status() override { return "Active"; }
    std::string IconThemePath() override { return {}; }
    std::string IconName() override { return kIconName; }
    std::string OverlayIconName() override { return {}; }
    std::string AttentionIconName() override { return {}; }
    std::string AttentionMovieName() override { return {}; }

    std::int32_t WindowId() override { return 0; }

    // True so that a left click opens the menu straight away: there is no
    // window to show and no other primary action worth having.
    bool ItemIsMenu() override { return true; }

    sdbus::ObjectPath Menu() override { return sdbus::ObjectPath{kMenuPath}; }

    // Named icons only; shipping raw pixmaps would mean carrying image data
    // this program has no other use for.
    Pixmaps IconPixmap() override { return {}; }
    Pixmaps OverlayIconPixmap() override { return {}; }
    Pixmaps AttentionIconPixmap() override { return {}; }

    ToolTipValue ToolTip() override
    {
        const vm::ViewState state = view_model_.view_state();
        const std::string title =
            state.is_playing ? state.station_name : std::string{"mradio"};

        return ToolTipValue{std::string{kIconName}, Pixmaps{}, title, state.track.display()};
    }

    vm::PlayerViewModel& view_model_;
};

// ------------------------------------------------------------- the watcher

class WatcherClient final : public sdbus::ProxyInterfaces<org::kde::StatusNotifierWatcher_proxy> {
public:
    explicit WatcherClient(sdbus::IConnection& connection)
        : ProxyInterfaces(connection,
                          sdbus::ServiceName{kWatcherService},
                          sdbus::ObjectPath{kWatcherPath})
    {
        registerProxy();
    }

    ~WatcherClient() { unregisterProxy(); }

    WatcherClient(const WatcherClient&) = delete;
    WatcherClient& operator=(const WatcherClient&) = delete;

    void register_item(const std::string& service) { RegisterStatusNotifierItem(service); }

private:
    // Nothing to do with any of these: this program registers one item and
    // never inspects what else is on the bus.
    void onStatusNotifierItemRegistered(const std::string& /*service*/) override {}
    void onStatusNotifierItemUnregistered(const std::string& /*service*/) override {}
    void onStatusNotifierHostRegistered() override {}
    void onStatusNotifierHostUnregistered() override {}
};

// ------------------------------------------------------------------ facade

class TrayIcon final : public Icon, public vm::IViewStateListener {
public:
    TrayIcon(sdbus::IConnection& connection,
             vm::PlayerViewModel& view_model,
             std::function<void()> quit,
             const IconOptions& options)
        : connection_(connection),
          view_model_(view_model),
          menu_(connection, view_model, std::move(quit), options.offer_sort_by_name),
          item_(connection, view_model),
          watcher_(connection)
    {
        view_model_.add_listener(this);
        watch_for_watcher();
        register_with_watcher();
    }

    ~TrayIcon() override { view_model_.remove_listener(this); }

    void on_view_state_changed(const vm::ViewState& /*state*/) override
    {
        menu_.refresh();
        item_.refresh();
    }

private:
    void register_with_watcher()
    {
        try {
            watcher_.register_item(std::string{connection_.getUniqueName()});
        }
        catch (const sdbus::Error&) {
            // No watcher on the bus: a panel that has not started yet, or a
            // desktop with no tray support. watch_for_watcher() will retry.
        }
    }

    // Registers again whenever a StatusNotifierWatcher appears. Without this,
    // starting before the panel does - or outliving a panel restart - would
    // leave the icon invisible for the rest of the session.
    void watch_for_watcher()
    {
        name_watch_ = sdbus::createProxy(connection_,
                                         sdbus::ServiceName{"org.freedesktop.DBus"},
                                         sdbus::ObjectPath{"/org/freedesktop/DBus"});

        name_watch_->uponSignal(sdbus::SignalName{"NameOwnerChanged"})
            .onInterface(sdbus::InterfaceName{"org.freedesktop.DBus"})
            .call([this](const std::string& name,
                         const std::string& /*previous_owner*/,
                         const std::string& new_owner) {
                if (name == kWatcherService && !new_owner.empty()) {
                    register_with_watcher();
                }
            });
    }

    sdbus::IConnection& connection_;
    vm::PlayerViewModel& view_model_;

    MenuObject menu_;
    ItemObject item_;
    WatcherClient watcher_;
    std::unique_ptr<sdbus::IProxy> name_watch_;
};

}  // namespace

core::Result<std::unique_ptr<Icon>> Icon::start(sdbus::IConnection& connection,
                                                vm::PlayerViewModel& view_model,
                                                std::function<void()> quit,
                                                IconOptions options)
{
    try {
        return std::unique_ptr<Icon>{
            new TrayIcon{connection, view_model, std::move(quit), options}};
    }
    catch (const sdbus::Error& error) {
        return std::unexpected(ipc::from_sdbus(error));
    }
}

}  // namespace mradio::tray
