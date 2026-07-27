#include <catch2/catch_all.hpp>
#include <SDL3/SDL_scancode.h>
#include "UIManager.hpp"

using json = nlohmann::json;

// ── Stack push/pop ──────────────────────────────────────────────

TEST_CASE("UIManager: empty on construction", "[UIManager]") {
    UIManager mgr;
    REQUIRE(mgr.empty());
    REQUIRE(mgr.top() == nullptr);
    REQUIRE_FALSE(mgr.shouldBlockGameInput());
    REQUIRE_FALSE(mgr.shouldForwardKeysToServer());
    REQUIRE_FALSE(mgr.isMenuActive());
}

TEST_CASE("UIManager: push menu, query state", "[UIManager]") {
    UIManager mgr;
    json menuMsg = {{"msg","menu"},{"tag","use_item"},{"title","Quaff which item?"}};
    mgr.handleMessage(menuMsg);
    REQUIRE_FALSE(mgr.empty());
    REQUIRE(mgr.top() != nullptr);
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Menu);
    REQUIRE(mgr.top()->tag == "use_item");
    REQUIRE(mgr.isMenuActive());
    REQUIRE(mgr.shouldBlockGameInput());
    REQUIRE(mgr.shouldForwardKeysToServer());
}

TEST_CASE("UIManager: push then pop returns to empty", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","close_menu"}});
    REQUIRE(mgr.empty());
    REQUIRE_FALSE(mgr.shouldBlockGameInput());
}

TEST_CASE("UIManager: clear empties stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","ui-push"},{"type","describe-item"}});
    REQUIRE_FALSE(mgr.empty());
    mgr.handleMessage({{"msg","close_all_menus"}});
    REQUIRE(mgr.empty());
}

// ── Stacked menu + overlay ──────────────────────────────────────

TEST_CASE("UIManager: overlay stacks on top of menu", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","ui-push"},{"type","describe-item"}});
    // Top is the overlay, menu is underneath
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Overlay);
    REQUIRE(mgr.top()->tag == "describe-item");
    REQUIRE(mgr.isMenuActive() == false);  // top is overlay, not menu
    REQUIRE(mgr.hasOverlay("describe-item"));
    // Pop overlay, menu reappears
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Menu);
    REQUIRE(mgr.isMenuActive());
}

// ── hasOverlay / hasMenu lookups ─────────────────────────────────

TEST_CASE("UIManager: hasOverlay finds type anywhere in stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"}});
    REQUIRE(mgr.hasOverlay("progress-bar"));
    REQUIRE_FALSE(mgr.hasOverlay("describe-item"));
}

TEST_CASE("UIManager: hasMenu finds tag anywhere in stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"}});
    REQUIRE(mgr.hasMenu("inventory"));
    REQUIRE_FALSE(mgr.hasMenu("use_item"));
}

// ── update_menu metadata merge ───────────────────────────────────

TEST_CASE("UIManager: update_menu merges title and flags", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},
        {"title","Quaff which item?"},{"flags",5},{"total_items",2}});
    mgr.handleMessage({{"msg","update_menu"},
        {"title","Quaff which potion?"},{"total_items",3}});
    // Title updated, flags preserved, total_items updated
    REQUIRE(mgr.top()->data["title"] == "Quaff which potion?");
    REQUIRE(mgr.top()->data["flags"] == 5);
    REQUIRE(mgr.top()->data["total_items"] == 3);
}

// ── update_menu_items chunk merging ──────────────────────────────

TEST_CASE("UIManager: update_menu_items merges chunk at offset", "[UIManager]") {
    UIManager mgr;
    json menuMsg = {{"msg","menu"},{"tag","use_item"},
        {"total_items",3},{"chunk_start",0},
        {"items", json::array({
            json::object({{"text","a - potion of healing"}}),
            json::object({{"text","b - potion of might"}})
        })}};
    mgr.handleMessage(menuMsg);
    // Second chunk covers index 2
    json update = {{"msg","update_menu_items"},{"chunk_start",2},
        {"items", json::array({
            json::object({{"text","c - potion of brilliance"}})
        })}};
    mgr.handleMessage(update);
    auto& items = mgr.top()->data["items"];
    REQUIRE(items.size() == 3);
    REQUIRE(items[2]["text"] == "c - potion of brilliance");
}

// ── ui-state with generation_id ──────────────────────────────────

TEST_CASE("UIManager: ui-state updates top overlay", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"},
        {"generation_id",1},{"status","Loading..."}});
    mgr.handleMessage({{"msg","ui-state"},{"type","progress-bar"},
        {"generation_id",1},{"status","Saving..."}});
    REQUIRE(mgr.top()->data["status"] == "Saving...");
}

TEST_CASE("UIManager: ui-state with wrong generation_id is ignored", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"},
        {"generation_id",1},{"status","Loading..."}});
    mgr.handleMessage({{"msg","ui-state"},{"type","progress-bar"},
        {"generation_id",99},{"status","Stale update"}});
    // Should NOT update — generation_id mismatch
    REQUIRE(mgr.top()->data["status"] == "Loading...");
}

// ── ui-stack (spectator sync) ────────────────────────────────────

TEST_CASE("UIManager: ui-stack replaces entire stack", "[UIManager]") {
    UIManager mgr;
    json stackMsg = {{"msg","ui-stack"},{"items", json::array({
        {{"msg","ui-push"},{"type","newgame-choice"}},
        {{"msg","menu"},{"tag","use_item"}}
    })}};
    mgr.handleMessage(stackMsg);
    REQUIRE_FALSE(mgr.empty());
    // Two entries pushed in order
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE_FALSE(mgr.empty());
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE(mgr.empty());
}

// ── title_prompt merge ───────────────────────────────────────────

TEST_CASE("UIManager: title_prompt merges into top menu", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"}});
    mgr.handleMessage({{"msg","title_prompt"},{"prompt","Search:"}});
    REQUIRE(mgr.top()->data["prompt"] == "Search:");
    REQUIRE(mgr.top()->data.value("raw", false) == false);
}

// ── menu_scroll ──────────────────────────────────────────────────

TEST_CASE("UIManager: menu_scroll updates first/last/hover", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"total_items",10}});
    mgr.handleMessage({{"msg","menu_scroll"},{"first",5},{"last",8},{"hover",6}});
    REQUIRE(mgr.top()->data["first"] == 5);
    REQUIRE(mgr.top()->data["last"] == 8);
    REQUIRE(mgr.top()->data["last_hovered"] == 6);
}

// ── Menu navigation keys ─────────────────────────────────────────

TEST_CASE("UIManager: handleMenuNavigationKey returns false when not menu", "[UIManager]") {
    UIManager mgr;
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
}

TEST_CASE("UIManager: arrow up when ARROWS_SELECT flag", "[UIManager]") {
    UIManager mgr;
    // ARROWS_SELECT = 0x40000
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0x40000},
        {"total_items",3},
        {"items", json::array({
            json::object({{"text","a - item1"},{"level",2}}),
            json::object({{"text","b - item2"},{"level",2}}),
            json::object({{"text","c - item3"},{"level",2}})
        })}});
    // Initial hover is -1; down arrow should select first
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 0);
    // Another down arrow
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 1);
    // Up arrow back
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 0);
}

TEST_CASE("UIManager: pgup/pgdn are consumed", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_PAGEUP, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_PAGEDOWN, false));
}

TEST_CASE("UIManager: home/end are consumed", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_HOME, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_END, false));
}

TEST_CASE("UIManager: space in menu is consumed (page down)", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_SPACE, false));
}

TEST_CASE("UIManager: minus in menu depends on tag", "[UIManager]") {
    UIManager mgr;
    // use_item uses '-' as custom key (unwield), so we DON'T consume it
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_MINUS, false));
    // A generic menu without custom dash DOES consume it (page up)
    mgr.handleMessage({{"msg","menu"},{"tag","actions"},{"flags",0},{"total_items",20}});
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_MINUS, false));
}

TEST_CASE("UIManager: arrow keys without ARROWS_SELECT do line scroll", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"},{"flags",0},{"total_items",20}});
    // Arrow keys should still be consumed (line up/down scroll)
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
}
