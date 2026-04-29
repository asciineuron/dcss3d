#include <catch2/catch_all.hpp>
#include "WindowManager.hpp"

TEST_CASE("WindowManager defaults to Normal mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Normal);

    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);
    REQUIRE(wm.shouldRenderUI() == false);
    REQUIRE(wm.shouldProcessGameInput() == true);
    REQUIRE(wm.shouldUseRelativeMouse() == true);
}

TEST_CASE("WindowManager Overlay mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Overlay);

    REQUIRE(wm.getMode() == WindowManager::Mode::Overlay);
    REQUIRE(wm.shouldRenderUI() == true);
    REQUIRE(wm.shouldProcessGameInput() == false);
    REQUIRE(wm.shouldUseRelativeMouse() == false);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager Equipment mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Equipment);

    REQUIRE(wm.getMode() == WindowManager::Mode::Equipment);
    REQUIRE(wm.shouldRenderUI() == true);
    REQUIRE(wm.shouldProcessGameInput() == false);
    REQUIRE(wm.shouldUseRelativeMouse() == false);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager toggleOverlay flips Normal <-> Overlay", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Normal);

    wm.toggleOverlay();
    REQUIRE(wm.getMode() == WindowManager::Mode::Overlay);

    wm.toggleOverlay();
    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);

    wm.toggleOverlay();
    REQUIRE(wm.getMode() == WindowManager::Mode::Overlay);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager toggleEquipment flips Normal <-> Equipment", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Normal);

    wm.toggleEquipment();
    REQUIRE(wm.getMode() == WindowManager::Mode::Equipment);

    wm.toggleEquipment();
    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);

    wm.toggleEquipment();
    REQUIRE(wm.getMode() == WindowManager::Mode::Equipment);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager toggleOverlay from Equipment goes to Overlay", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Equipment);

    // toggleOverlay from Equipment should go to Overlay (not Normal)
    wm.toggleOverlay();
    REQUIRE(wm.getMode() == WindowManager::Mode::Overlay);

    wm.toggleOverlay();
    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager toggleEquipment from Overlay goes to Equipment", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Overlay);

    // toggleEquipment from Overlay should go to Equipment
    wm.toggleEquipment();
    REQUIRE(wm.getMode() == WindowManager::Mode::Equipment);

    wm.toggleEquipment();
    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager isVisible in Normal mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Normal);

    // In Normal mode, no windows are visible (pins handled separately)
    REQUIRE(wm.isVisible("player") == false);
    REQUIRE(wm.isVisible("map") == false);
    REQUIRE(wm.isVisible("equipment") == false);
    REQUIRE(wm.isVisible("settings") == false);
    REQUIRE(wm.isVisible("network") == false);
    REQUIRE(wm.isVisible("renderer") == false);
}

TEST_CASE("WindowManager isVisible in Overlay mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Overlay);

    // In Overlay mode, all windows are visible
    REQUIRE(wm.isVisible("player") == true);
    REQUIRE(wm.isVisible("map") == true);
    REQUIRE(wm.isVisible("equipment") == true);
    REQUIRE(wm.isVisible("settings") == true);
    REQUIRE(wm.isVisible("network") == true);
    REQUIRE(wm.isVisible("renderer") == true);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager isVisible in Equipment mode", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Equipment);

    // In Equipment mode, only the equipment window is visible
    REQUIRE(wm.isVisible("equipment") == true);
    REQUIRE(wm.isVisible("player") == false);
    REQUIRE(wm.isVisible("map") == false);
    REQUIRE(wm.isVisible("network") == false);
    REQUIRE(wm.isVisible("renderer") == false);
    REQUIRE(wm.isVisible("settings") == false);

    // Reset for other tests
    wm.setMode(WindowManager::Mode::Normal);
}

TEST_CASE("WindowManager setMode transitions", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Normal);

    // Direct mode transitions
    wm.setMode(WindowManager::Mode::Overlay);
    REQUIRE(wm.getMode() == WindowManager::Mode::Overlay);

    wm.setMode(WindowManager::Mode::Equipment);
    REQUIRE(wm.getMode() == WindowManager::Mode::Equipment);

    wm.setMode(WindowManager::Mode::Normal);
    REQUIRE(wm.getMode() == WindowManager::Mode::Normal);

    wm.setMode(WindowManager::Mode::Equipment);
    REQUIRE(wm.isVisible("equipment") == true);
    REQUIRE(wm.isVisible("player") == false);
}

TEST_CASE("WindowManager nullptr safety in isVisible", "[WindowManager]")
{
    WindowManager& wm = WindowManager::instance();
    wm.setMode(WindowManager::Mode::Equipment);

    // Should not crash with nullptr
    REQUIRE(wm.isVisible(nullptr) == false);

    wm.setMode(WindowManager::Mode::Normal);
}
