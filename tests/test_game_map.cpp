#include <catch2/catch_all.hpp>
#include "GameMap.hpp"

TEST_CASE("MapType enum has expected values", "[GameMap]")
{
    REQUIRE(static_cast<int>(MapType::Wall) == 0);
    REQUIRE(static_cast<int>(MapType::Floor) == 1);
    REQUIRE(static_cast<int>(MapType::Unexplored) == 2);
    REQUIRE(static_cast<int>(MapType::Water) == 3);
    REQUIRE(static_cast<int>(MapType::Lava) == 4);
    REQUIRE(static_cast<int>(MapType::Other) == 5);
}

TEST_CASE("Tile default constructor sets Other type", "[GameMap]")
{
    Tile tile;
    REQUIRE(tile.type() == MapType::Other);
}

TEST_CASE("Tile constructor sets specified type", "[GameMap]")
{
    Tile tile(MapType::Floor);
    REQUIRE(tile.type() == MapType::Floor);
}

TEST_CASE("Pos2 supports comparison operators", "[GameMap]")
{
    Pos2<int> p1{1, 2};
    Pos2<int> p2{1, 2};
    Pos2<int> p3{2, 3};
    
    REQUIRE(p1 == p2);
    REQUIRE(p1 != p3);
}

// --- Monster tests ---

TEST_CASE("Monster default constructor creates empty monster", "[Monster]")
{
    Monster m;
    REQUIRE(m.id() == 0);
    REQUIRE(m.type() == 0);
    REQUIRE(m.att() == 0);
    REQUIRE(m.threat() == 0);
    REQUIRE(m.name().empty());
    REQUIRE(m.plural().empty());
    REQUIRE_FALSE(m.hasBtype());
    REQUIRE_FALSE(m.hasTypedata());
    REQUIRE_FALSE(m.hasClientid());
}

TEST_CASE("Monster::merge parses all fields from full JSON", "[Monster]")
{
    json monJson = {
        {"id", 42},
        {"type", 123},
        {"att", 0},
        {"threat", 2},
        {"name", "Kikubaa Kocho"},
        {"plural", "Kikubaa Kocho"},
        {"btype", 100},
        {"clientid", 42},
        {"typedata", {
            {"avghp", 100},
            {"no_exp", false}
        }}
    };

    Monster m;
    m.merge(monJson);

    REQUIRE(m.id() == 42);
    REQUIRE(m.type() == 123);
    REQUIRE(m.att() == 0);
    REQUIRE(m.threat() == 2);
    REQUIRE(m.name() == "Kikubaa Kocho");
    REQUIRE(m.plural() == "Kikubaa Kocho");
    REQUIRE(m.hasBtype());
    REQUIRE(m.btype() == 100);
    REQUIRE(m.hasClientid());
    REQUIRE(m.clientid() == 42);
    REQUIRE(m.hasTypedata());
    REQUIRE(m.typedataAvghp() == 100);
    REQUIRE(m.typedataNoExp() == false);
}

TEST_CASE("Monster::merge handles partial updates", "[Monster]")
{
    // First, create a full monster
    json fullJson = {
        {"id", 99},
        {"type", 200},
        {"att", 4},
        {"threat", 1},
        {"name", "Imp"},
        {"plural", "Imps"}
    };

    Monster m;
    m.merge(fullJson);

    // Now send a partial update with only att changed
    json partialJson = {
        {"id", 99},
        {"att", 0}  // became hostile
    };

    m.merge(partialJson);

    // Verify att was updated
    REQUIRE(m.att() == 0);
    // Verify other fields preserved
    REQUIRE(m.type() == 200);
    REQUIRE(m.name() == "Imp");
    REQUIRE(m.plural() == "Imps");
    REQUIRE(m.threat() == 1);
}

TEST_CASE("Monster::merge sets btype only when present", "[Monster]")
{
    json withoutBtype = {
        {"id", 1},
        {"type", 50}
    };
    Monster m;
    m.merge(withoutBtype);
    REQUIRE_FALSE(m.hasBtype());

    json withBtype = {
        {"id", 1},
        {"btype", 75}
    };
    m.merge(withBtype);
    REQUIRE(m.hasBtype());
    REQUIRE(m.btype() == 75);
}

TEST_CASE("GameMap::getMonsterAt returns nullopt for empty map", "[GameMap]")
{
    GameMap map;
    REQUIRE_FALSE(map.getMonsterAt(0, 0).has_value());
    REQUIRE_FALSE(map.getMonsterAt(5, 10).has_value());
}

TEST_CASE("GameMap parses monster from map message cell", "[GameMap]")
{
    GameMap map;
    
    json mapMsg = {
        {"msg", "map"},
        {"cells", json::array({
            {
                {"x", 5},
                {"y", 3},
                {"mf", 1},
                {"mon", {
                    {"id", 100},
                    {"type", 50},
                    {"name", "Goblin"},
                    {"att", 0},
                    {"threat", 0}
                }}
            }
        })}
    };

    map.handleMessage(mapMsg);

    // Check tile was created
    auto tileOpt = map.getTileAt(5, 3);
    REQUIRE(tileOpt.has_value());
    REQUIRE(*tileOpt == MapType::Floor);

    // Check monster was stored
    auto monOpt = map.getMonsterAt(5, 3);
    REQUIRE(monOpt.has_value());
    const Monster& mon = monOpt->get();
    REQUIRE(mon.id() == 100);
    REQUIRE(mon.name() == "Goblin");
    REQUIRE(mon.type() == 50);
    REQUIRE(mon.att() == 0);
    REQUIRE(mon.threat() == 0);
}

TEST_CASE("GameMap removes monster from cell on null mon", "[GameMap]")
{
    GameMap map;
    
    // Add a monster at (2,2)
    json addMsg = {
        {"msg", "map"},
        {"cells", json::array({
            {
                {"x", 2},
                {"y", 2},
                {"mf", 1},
                {"mon", {
                    {"id", 42},
                    {"type", 10},
                    {"name", "Rat"}
                }}
            }
        })}
    };
    map.handleMessage(addMsg);
    REQUIRE(map.getMonsterAt(2, 2).has_value());
    REQUIRE(map.monsterTable().size() == 1);

    // Remove it with mon:null
    json removeMsg = {
        {"msg", "map"},
        {"cells", json::array({
            {
                {"x", 2},
                {"y", 2},
                {"mf", 1},
                {"mon", nullptr}
            }
        })}
    };
    map.handleMessage(removeMsg);

    REQUIRE_FALSE(map.getMonsterAt(2, 2).has_value());
    // After cleanup, the monster table should be empty
    REQUIRE(map.monsterTable().empty());
}

TEST_CASE("GameMap merges partial monster updates via global table", "[GameMap]")
{
    GameMap map;
    
    // Cell 1: monster id=1 appears
    json msg1 = {
        {"msg", "map"},
        {"cells", json::array({
            {
                {"x", 1},
                {"y", 1},
                {"mf", 1},
                {"mon", {
                    {"id", 1},
                    {"type", 30},
                    {"name", "Orc"},
                    {"att", 0},
                    {"threat", 1}
                }}
            }
        })}
    };
    map.handleMessage(msg1);

    // Cell 2: same monster id=1 but partial update (only threat changed)
    json msg2 = {
        {"msg", "map"},
        {"cells", json::array({
            {
                {"x", 2},
                {"y", 2},
                {"mf", 1},
                {"mon", {
                    {"id", 1},
                    {"threat", 3}
                }}
            }
        })}
    };
    map.handleMessage(msg2);

    // Monster should now be at (2,2) with merged data
    REQUIRE_FALSE(map.getMonsterAt(1, 1).has_value());
    auto monOpt = map.getMonsterAt(2, 2);
    REQUIRE(monOpt.has_value());
    const Monster& mon = monOpt->get();
    REQUIRE(mon.id() == 1);
    REQUIRE(mon.name() == "Orc");      // preserved from original
    REQUIRE(mon.type() == 30);         // preserved
    REQUIRE(mon.att() == 0);           // preserved
    REQUIRE(mon.threat() == 3);        // updated
}

TEST_CASE("GameMap shift moves monsters with tiles", "[GameMap]")
{
    GameMap map;
    
    json msg = {
        {"msg", "map"},
        {"cells", json::array({
            {{"x", 0}, {"y", 0}, {"mf", 1}, {"mon", {{"id", 1}, {"type", 10}, {"name", "Rat"}}}},
            {{"x", 1}, {"y", 0}, {"mf", 2}},
            {{"x", 0}, {"y", 1}, {"mf", 1}},
            {{"x", 1}, {"y", 1}, {"mf", 1}}
        })}
    };
    map.handleMessage(msg);

    REQUIRE(map.getMonsterAt(0, 0).has_value());
    REQUIRE(map.getTileAt(0, 0).has_value());
    REQUIRE(*map.getTileAt(0, 0) == MapType::Floor);
    REQUIRE(*map.getTileAt(1, 0) == MapType::Wall);

    // Shift north
    map.shift(North);

    // Monster and tiles should have moved
    REQUIRE_FALSE(map.getMonsterAt(0, 0).has_value());
    REQUIRE(map.getMonsterAt(0, 1).has_value());
    REQUIRE(map.getTileAt(0, 1).has_value());
    REQUIRE(*map.getTileAt(0, 1) == MapType::Floor);
    REQUIRE(*map.getTileAt(1, 1) == MapType::Wall);
}

TEST_CASE("GameMap clear also clears monsters", "[GameMap]")
{
    GameMap map;
    
    json msg = {
        {"msg", "map"},
        {"clear", true},
        {"cells", json::array({
            {{"x", 0}, {"y", 0}, {"mf", 1}, {"mon", {{"id", 5}, {"name", "Troll"}}}}
        })}
    };
    map.handleMessage(msg);
    REQUIRE(map.getMonsterAt(0, 0).has_value());
    REQUIRE(map.monsterTable().size() == 1);

    // Second clear should wipe everything
    json clearMsg = {
        {"msg", "map"},
        {"clear", true},
        {"cells", json::array({
            {{"x", 0}, {"y", 0}, {"mf", 1}}
        })}
    };
    map.handleMessage(clearMsg);

    REQUIRE_FALSE(map.getMonsterAt(0, 0).has_value());
    REQUIRE(map.monsterTable().empty());
    REQUIRE(map.monsterPositions().empty());
}
