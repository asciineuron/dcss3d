#include <catch2/catch_all.hpp>
#include "SpriteManager.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============================================================
// Test Suite 1: AnimationClip
// ============================================================

TEST_CASE("AnimationClip: total duration is sum of frame durations", "[AnimationClip]")
{
    AnimationClip clip;
    clip.name = "test";
    clip.loop = true;
    clip.frames = {
        { 0.0f, 0.0f, 0.25f, 0.5f, 100 },
        { 0.25f, 0.0f, 0.25f, 0.5f, 200 },
        { 0.5f, 0.0f, 0.25f, 0.5f, 150 },
    };

    REQUIRE(clip.totalDurationMs() == 450);
}

TEST_CASE("AnimationClip: frame lookup for looping clip", "[AnimationClip]")
{
    AnimationClip clip;
    clip.name = "test";
    clip.loop = true;
    clip.frames = {
        { 0.0f, 0.0f, 0.25f, 0.5f, 100 },  // frame 0:   0–100ms
        { 0.25f, 0.0f, 0.25f, 0.5f, 100 },  // frame 1: 100–200ms
        { 0.5f, 0.0f, 0.25f, 0.5f, 100 },  // frame 2: 200–300ms
    };

    REQUIRE(clip.getFrameIndex(0) == 0);
    REQUIRE(clip.getFrameIndex(50) == 0);
    REQUIRE(clip.getFrameIndex(100) == 1);
    REQUIRE(clip.getFrameIndex(150) == 1);
    REQUIRE(clip.getFrameIndex(200) == 2);
    REQUIRE(clip.getFrameIndex(250) == 2);
    REQUIRE(clip.getFrameIndex(300) == 0);   // wraps
    REQUIRE(clip.getFrameIndex(350) == 0);
    // 550 % 300 = 250, 250ms falls in frame 2 (200–300ms)
    REQUIRE(clip.getFrameIndex(550) == 2);
}

TEST_CASE("AnimationClip: frame lookup for non-looping clip", "[AnimationClip]")
{
    AnimationClip clip;
    clip.name = "test";
    clip.loop = false;
    clip.frames = {
        { 0.0f, 0.0f, 0.25f, 0.5f, 100 },
        { 0.25f, 0.0f, 0.25f, 0.5f, 100 },
        { 0.5f, 0.0f, 0.25f, 0.5f, 100 },
    };

    REQUIRE(clip.getFrameIndex(0) == 0);
    REQUIRE(clip.getFrameIndex(50) == 0);
    REQUIRE(clip.getFrameIndex(100) == 1);
    REQUIRE(clip.getFrameIndex(200) == 2);
    REQUIRE(clip.getFrameIndex(300) == 2);  // clamped at last frame
    REQUIRE(clip.getFrameIndex(999) == 2);  // still clamped
}

TEST_CASE("AnimationClip: isComplete for looping clip", "[AnimationClip]")
{
    AnimationClip clip;
    clip.name = "test";
    clip.loop = true;
    clip.frames = { { 0.0f, 0.0f, 1.0f, 1.0f, 100 } };

    // Looping clips are never "complete"
    REQUIRE_FALSE(clip.isComplete(0));
    REQUIRE_FALSE(clip.isComplete(100));
    REQUIRE_FALSE(clip.isComplete(99999));
}

TEST_CASE("AnimationClip: isComplete for non-looping clip", "[AnimationClip]")
{
    AnimationClip clip;
    clip.name = "test";
    clip.loop = false;
    clip.frames = {
        { 0.0f, 0.0f, 1.0f, 1.0f, 100 },
        { 0.0f, 0.0f, 1.0f, 1.0f, 100 },
    };

    REQUIRE_FALSE(clip.isComplete(0));
    REQUIRE_FALSE(clip.isComplete(50));
    REQUIRE_FALSE(clip.isComplete(199));
    REQUIRE(clip.isComplete(200));
    REQUIRE(clip.isComplete(999));
}

// ============================================================
// Test Suite 2: SpriteInstance
// ============================================================

// Helper to create a simple 3-frame clip
static AnimationClip makeTestClip(const std::string& name = "test", bool loop = true)
{
    AnimationClip clip;
    clip.name = name;
    clip.loop = loop;
    clip.frames = {
        { 0.0f, 0.0f, 0.25f, 0.5f, 100 },
        { 0.25f, 0.0f, 0.25f, 0.5f, 100 },
        { 0.5f, 0.0f, 0.25f, 0.5f, 100 },
    };
    return clip;
}

TEST_CASE("SpriteInstance: initial state", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteTransform t;
    SpriteInstance instance(&clip, SpriteSpace::Screen, t);

    REQUIRE(instance.currentFrame() == 0);
    REQUIRE(instance.elapsedTime() == 0.0f);
    REQUIRE_FALSE(instance.isComplete());
    REQUIRE(instance.space() == SpriteSpace::Screen);
    REQUIRE(instance.clip() == &clip);
}

TEST_CASE("SpriteInstance: tick advances frame", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});

    // After 150ms, should be on frame 1
    bool completed = instance.tick(0.150f);
    REQUIRE(instance.currentFrame() == 1);
    REQUIRE_FALSE(completed);
}

TEST_CASE("SpriteInstance: tick wraps on loop", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});

    // After 350ms (3 frames of 100ms + 50ms into next cycle), should wrap to frame 0
    bool completed = instance.tick(0.350f);
    REQUIRE(instance.currentFrame() == 0);
    REQUIRE_FALSE(completed);  // looping, never completes
}

TEST_CASE("SpriteInstance: tick completes non-loop", "[SpriteInstance]")
{
    auto clip = makeTestClip("test", false);
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});

    // 200ms — still in progress
    bool completed = instance.tick(0.200f);
    REQUIRE(instance.currentFrame() == 2);
    REQUIRE_FALSE(completed);
    REQUIRE_FALSE(instance.isComplete());

    // Another tick past total duration (300ms)
    completed = instance.tick(0.150f);
    REQUIRE(instance.currentFrame() == 2);  // clamped
    REQUIRE(completed);
    REQUIRE(instance.isComplete());
}

TEST_CASE("SpriteInstance: gpuData reflects transform", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteTransform t;
    t.posX = 0.5f;
    t.posY = -0.3f;
    t.posZ = 0.1f;
    t.scaleX = 2.0f;
    t.scaleY = 1.5f;
    t.rotation = 1.57f;
    t.r = 0.8f;
    t.g = 0.6f;
    t.b = 0.4f;
    t.a = 0.9f;

    SpriteInstance instance(&clip, SpriteSpace::Screen, t);
    GpuSpriteData data = instance.gpuData();

    REQUIRE(data.posX == 0.5f);
    REQUIRE(data.posY == -0.3f);
    REQUIRE(data.posZ == 0.1f);
    REQUIRE(data.scaleX == 2.0f);
    REQUIRE(data.scaleY == 1.5f);
    REQUIRE(data.rotation == 1.57f);
    REQUIRE(data.r == 0.8f);
    REQUIRE(data.g == 0.6f);
    REQUIRE(data.b == 0.4f);
    REQUIRE(data.a == 0.9f);
}

TEST_CASE("SpriteInstance: gpuData UV comes from current frame", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});

    // Frame 0
    {
        GpuSpriteData data = instance.gpuData();
        REQUIRE(data.texU == 0.0f);
        REQUIRE(data.texV == 0.0f);
        REQUIRE(data.texW == 0.25f);
        REQUIRE(data.texH == 0.5f);
    }

    // Advance to frame 1
    instance.tick(0.150f);
    {
        GpuSpriteData data = instance.gpuData();
        REQUIRE(data.texU == 0.25f);
        REQUIRE(data.texV == 0.0f);
        REQUIRE(data.texW == 0.25f);
        REQUIRE(data.texH == 0.5f);
    }

    // Advance to frame 2
    instance.tick(0.100f);
    {
        GpuSpriteData data = instance.gpuData();
        REQUIRE(data.texU == 0.5f);
        REQUIRE(data.texV == 0.0f);
        REQUIRE(data.texW == 0.25f);
        REQUIRE(data.texH == 0.5f);
    }
}

TEST_CASE("SpriteInstance: setClip restarts by default", "[SpriteInstance]")
{
    auto clipA = makeTestClip("A");
    auto clipB = makeTestClip("B");

    SpriteInstance instance(&clipA, SpriteSpace::Screen, {});
    instance.tick(0.250f);  // well into clip A
    REQUIRE(instance.currentFrame() == 2);

    instance.setClip(&clipB);  // restart = true by default
    REQUIRE(instance.clip() == &clipB);
    REQUIRE(instance.currentFrame() == 0);
    REQUIRE(instance.elapsedTime() == 0.0f);
}

TEST_CASE("SpriteInstance: setClip with restart=false preserves time", "[SpriteInstance]")
{
    auto clipA = makeTestClip("A");
    auto clipB = makeTestClip("B");

    SpriteInstance instance(&clipA, SpriteSpace::Screen, {});
    instance.tick(0.150f);  // 150ms into clip A

    instance.setClip(&clipB, false);  // don't restart
    REQUIRE(instance.clip() == &clipB);
    REQUIRE(instance.elapsedTime() == 0.150f);  // time preserved
    // Frame depends on clip B's structure at 150ms — clip B has same frame layout,
    // so 150ms → frame 1
    REQUIRE(instance.currentFrame() == 1);
}

TEST_CASE("SpriteInstance: setTransform updates running instance", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});

    SpriteTransform newT;
    newT.posX = 0.9f;
    newT.posY = 0.1f;
    newT.scaleX = 3.0f;
    newT.a = 0.5f;

    instance.setTransform(newT);
    GpuSpriteData data = instance.gpuData();

    REQUIRE(data.posX == 0.9f);
    REQUIRE(data.posY == 0.1f);
    REQUIRE(data.scaleX == 3.0f);
    REQUIRE(data.a == 0.5f);
}

TEST_CASE("SpriteInstance: setSpace changes space", "[SpriteInstance]")
{
    auto clip = makeTestClip();
    SpriteInstance instance(&clip, SpriteSpace::Screen, {});
    REQUIRE(instance.space() == SpriteSpace::Screen);

    instance.setSpace(SpriteSpace::World);
    REQUIRE(instance.space() == SpriteSpace::World);
}

// ============================================================
// Test Suite 3: SpriteAtlas
// ============================================================

TEST_CASE("SpriteAtlas: loads animations from JSON", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {
            "test_idle": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 0.5, "h": 1.0, "duration_ms": 200}
                ]
            }
        }
    })"_json;

    SpriteAtlas atlas(desc);

    REQUIRE(atlas.animationCount() == 1);
    REQUIRE(atlas.hasAnimation("test_idle"));
    REQUIRE_FALSE(atlas.hasAnimation("nonexistent"));
}

TEST_CASE("SpriteAtlas: findAnimation returns correct clip", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {
            "my_clip": {
                "loop": false,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 0.5, "h": 1.0, "duration_ms": 100},
                    {"u": 0.5, "v": 0.0, "w": 0.5, "h": 1.0, "duration_ms": 200}
                ]
            }
        }
    })"_json;

    SpriteAtlas atlas(desc);

    const AnimationClip* clip = atlas.findAnimation("my_clip");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->name == "my_clip");
    REQUIRE(clip->loop == false);
    REQUIRE(clip->frames.size() == 2);
    REQUIRE(clip->frames[0].durationMs == 100);
    REQUIRE(clip->frames[1].durationMs == 200);
    REQUIRE(clip->totalDurationMs() == 300);
}

TEST_CASE("SpriteAtlas: findAnimation returns nullptr for missing", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {}
    })"_json;

    SpriteAtlas atlas(desc);
    REQUIRE(atlas.findAnimation("anything") == nullptr);
}

TEST_CASE("SpriteAtlas: multiple animations", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {
            "idle": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 0.5, "h": 0.5, "duration_ms": 500}
                ]
            },
            "swing": {
                "loop": false,
                "frames": [
                    {"u": 0.0, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40},
                    {"u": 0.2, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40},
                    {"u": 0.4, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40}
                ]
            },
            "effect": {
                "loop": false,
                "frames": [
                    {"u": 0.6, "v": 0.5, "w": 0.4, "h": 0.5, "duration_ms": 80}
                ]
            }
        }
    })"_json;

    SpriteAtlas atlas(desc);

    REQUIRE(atlas.animationCount() == 3);
    REQUIRE(atlas.hasAnimation("idle"));
    REQUIRE(atlas.hasAnimation("swing"));
    REQUIRE(atlas.hasAnimation("effect"));

    const AnimationClip* swing = atlas.findAnimation("swing");
    REQUIRE(swing != nullptr);
    REQUIRE(swing->frames.size() == 3);
    REQUIRE_FALSE(swing->loop);
}

TEST_CASE("SpriteAtlas: loop flag defaults to true when omitted", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {
            "default_loop": {
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;

    SpriteAtlas atlas(desc);

    const AnimationClip* clip = atlas.findAnimation("default_loop");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->loop == true);
}

TEST_CASE("SpriteAtlas: empty animations object is valid", "[SpriteAtlas]")
{
    json desc = R"({
        "texture": "test_atlas.png",
        "animations": {}
    })"_json;

    SpriteAtlas atlas(desc);
    REQUIRE(atlas.animationCount() == 0);
}

// ============================================================
// Test Suite 4: SpriteManager (CPU logic)
// ============================================================

// Minimal SpriteManager that doesn't need GPU — we test only the instance management.
// For Phase 1, SpriteManager's constructor doesn't create GPU resources until
// we explicitly set things up. We'll use a nullptr device for CPU tests.

TEST_CASE("SpriteManager: play returns valid handle", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    // Add an atlas so play() can find the animation
    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test_clip": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h = mgr.play("test_clip", SpriteSpace::Screen, {});
    REQUIRE(h != INVALID_SPRITE);
}

TEST_CASE("SpriteManager: play twice returns different handles", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test_clip": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h1 = mgr.play("test_clip", SpriteSpace::Screen, {});
    SpriteHandle h2 = mgr.play("test_clip", SpriteSpace::Screen, {});
    REQUIRE(h1 != h2);
}

TEST_CASE("SpriteManager: stop removes instance", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test_clip": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h = mgr.play("test_clip", SpriteSpace::Screen, {});
    mgr.stop(h);

    // After stop, isComplete should return true (or the handle is inactive)
    REQUIRE(mgr.isComplete(h));
}

TEST_CASE("SpriteManager: update advances all active instances", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "a": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            },
            "b": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 200}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h1 = mgr.play("a", SpriteSpace::Screen, {});
    SpriteHandle h2 = mgr.play("b", SpriteSpace::Screen, {});

    mgr.update(0.150f);

    // Both instances should have advanced
    // h1: 150ms into 100ms frames → frame 1
    // h2: 150ms into 200ms frames → frame 0
    REQUIRE_FALSE(mgr.isComplete(h1));  // looping
    REQUIRE_FALSE(mgr.isComplete(h2));  // looping
}

TEST_CASE("SpriteManager: isComplete for non-loop animation", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "once": {
                "loop": false,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h = mgr.play("once", SpriteSpace::Screen, {});

    mgr.update(0.050f);
    REQUIRE_FALSE(mgr.isComplete(h));

    mgr.update(0.100f);  // total 150ms > 100ms
    REQUIRE(mgr.isComplete(h));
}

TEST_CASE("SpriteManager: setAnimation swaps clip on running instance", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "idle": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            },
            "swing": {
                "loop": false,
                "frames": [
                    {"u": 0.5, "v": 0.0, "w": 0.5, "h": 1.0, "duration_ms": 50}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h = mgr.play("idle", SpriteSpace::Screen, {});
    mgr.setAnimation(h, "swing");

    // Should now be playing "swing" — a non-loop clip
    mgr.update(0.100f);  // past swing's 50ms duration
    REQUIRE(mgr.isComplete(h));
}

TEST_CASE("SpriteManager: setTransform modifies running instance", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteTransform initial;
    initial.posX = 0.5f;
    initial.posY = -0.3f;
    SpriteHandle h = mgr.play("test", SpriteSpace::Screen, initial);

    SpriteTransform modified;
    modified.posX = 0.9f;
    modified.posY = -0.1f;
    modified.scaleX = 2.0f;
    mgr.setTransform(h, modified);

    // Verify by checking gpuData through the manager
    GpuSpriteData data = mgr.gpuData(h);
    REQUIRE(data.posX == 0.9f);
    REQUIRE(data.posY == -0.1f);  // setTransform replaces entire transform
    REQUIRE(data.scaleX == 2.0f);
}

TEST_CASE("SpriteManager: play with nonexistent animation returns invalid", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {}
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h = mgr.play("nonexistent", SpriteSpace::Screen, {});
    REQUIRE(h == INVALID_SPRITE);
}

TEST_CASE("SpriteManager: handle reuse after stop", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    SpriteHandle h1 = mgr.play("test", SpriteSpace::Screen, {});
    mgr.stop(h1);

    // Playing again should work — handles are always increasing
    SpriteHandle h2 = mgr.play("test", SpriteSpace::Screen, {});
    REQUIRE(h2 != INVALID_SPRITE);
    REQUIRE(h2 > h1);
}

TEST_CASE("SpriteManager: active instance count", "[SpriteManager]")
{
    SpriteManager mgr(nullptr, nullptr);

    json desc = R"({
        "texture": "test.png",
        "animations": {
            "test": {
                "loop": true,
                "frames": [
                    {"u": 0.0, "v": 0.0, "w": 1.0, "h": 1.0, "duration_ms": 100}
                ]
            }
        }
    })"_json;
    mgr.addAtlas(desc);

    REQUIRE(mgr.activeCount() == 0);

    auto h1 = mgr.play("test", SpriteSpace::Screen, {});
    REQUIRE(mgr.activeCount() == 1);

    auto h2 = mgr.play("test", SpriteSpace::Screen, {});
    REQUIRE(mgr.activeCount() == 2);

    mgr.stop(h1);
    REQUIRE(mgr.activeCount() == 1);

    mgr.stop(h2);
    REQUIRE(mgr.activeCount() == 0);
}
