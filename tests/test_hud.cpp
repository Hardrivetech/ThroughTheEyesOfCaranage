#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/PostFx.hpp"
#include "game/HudState.hpp"
#include "ui/Menu.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace carnage;
using Catch::Approx;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kAspect = 16.0f / 9.0f;

// Where the attacker is for a hit from `angle` radians clockwise of straight ahead, at `dist` metres.
glm::vec3 attackerAt(const glm::vec3& player, float yaw, float angle, float dist, float height = 0.0f) {
    const glm::vec2 forward{-std::sin(yaw), -std::cos(yaw)};
    const glm::vec2 right{std::cos(yaw), -std::sin(yaw)};
    const glm::vec2 d = forward * std::cos(angle) + right * std::sin(angle);
    return {player.x + d.x * dist, player.y + height, player.z + d.y * dist};
}

float wrapDiff(float a, float b) {
    float d = std::fmod(a - b, 2.0f * kPi);
    if (d > kPi) d -= 2.0f * kPi;
    if (d < -kPi) d += 2.0f * kPi;
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Momentum gauge
// ---------------------------------------------------------------------------

TEST_CASE("The momentum gauge is a 0..1 ratio that is full at twice sprint speed", "[hud][momentum]") {
    const MovementConfig cfg;
    CHECK(momentumGauge(0.0f).fill == 0.0f);
    CHECK(momentumGauge(cfg.sprintSpeed).fill == Approx(0.5f));
    CHECK(momentumGauge(cfg.sprintSpeed * 2.0f).fill == Approx(1.0f));
    CHECK(momentumGauge(cfg.sprintSpeed * 5.0f).fill == 1.0f);   // clamped
    CHECK(momentumGauge(cfg.walkSpeed).fill == Approx(cfg.walkSpeed / (cfg.sprintSpeed * 2.0f)));

    float last = -1.0f;
    for (float v = 0.0f; v <= 40.0f; v += 0.25f) {   // monotonic, never outside 0..1
        const float f = momentumGauge(v).fill;
        CHECK(f >= last);
        CHECK(f >= 0.0f);
        CHECK(f <= 1.0f);
        last = f;
    }
}

TEST_CASE("The gauge's tiers follow walk, run, sprint and the speed-line threshold", "[hud][momentum]") {
    const MovementConfig cfg;
    CHECK(momentumGauge(0.0f).tier == MomentumTier::Idle);
    CHECK(momentumGauge(0.4f).tier == MomentumTier::Idle);
    CHECK(momentumGauge(0.5f).tier == MomentumTier::Walk);
    CHECK(momentumGauge(cfg.walkSpeed).tier == MomentumTier::Walk);
    CHECK(momentumGauge(cfg.walkSpeed * 1.05f).tier == MomentumTier::Run);
    CHECK(momentumGauge(cfg.sprintSpeed * 0.9f).tier == MomentumTier::Run);
    CHECK(momentumGauge(cfg.sprintSpeed * 0.95f).tier == MomentumTier::Sprint);
    CHECK(momentumGauge(cfg.sprintSpeed).tier == MomentumTier::Sprint);
    CHECK(momentumGauge(cfg.wallRunSpeed).tier == MomentumTier::Sprint);   // a wall-run is a sprint-class speed
    CHECK(momentumGauge(cfg.sprintSpeed * kSpeedLineStart).tier == MomentumTier::Overdrive);
    CHECK(momentumGauge(cfg.maxSpeed).tier == MomentumTier::Overdrive);
}

TEST_CASE("The gauge's marks sit where sprint, wall-run and the speed lines start", "[hud][momentum]") {
    const MovementConfig cfg;
    const MomentumGauge g = momentumGauge(0.0f);
    CHECK(g.sprintMark == Approx(0.5f));
    CHECK(g.wallRunMark == Approx(cfg.wallRunSpeed / (cfg.sprintSpeed * 2.0f)));
    CHECK(g.overdriveMark == Approx(cfg.sprintSpeed * kSpeedLineStart / (cfg.sprintSpeed * 2.0f)));
    CHECK(g.sprintMark < g.wallRunMark);
    CHECK(g.wallRunMark < g.overdriveMark);
    for (const float m : {g.sprintMark, g.wallRunMark, g.overdriveMark}) {
        CHECK(m > 0.0f);
        CHECK(m < 1.0f);
    }
    // The bar reaches a mark exactly when the speed does.
    CHECK(momentumGauge(cfg.sprintSpeed).fill == Approx(g.sprintMark));
    CHECK(momentumGauge(cfg.wallRunSpeed).fill == Approx(g.wallRunMark));
}

TEST_CASE("The overdrive tier appears exactly when the PostFx speed lines do", "[hud][momentum][postfx]") {
    const MovementConfig cfg;
    for (float v = 0.0f; v <= cfg.maxSpeed + 5.0f; v += 0.25f) {
        INFO("speed " << v);
        const bool lines     = computePostFx(v, cfg.sprintSpeed, cfg.maxSpeed, 0.0f).speedLines > 0.0f;
        const bool overdrive = momentumGauge(v, cfg).tier == MomentumTier::Overdrive;
        CHECK(lines == overdrive);
    }
    // Right at the threshold the tier has begun and the lines are about to.
    const float start = cfg.sprintSpeed * kSpeedLineStart;
    CHECK(momentumGauge(start).tier == MomentumTier::Overdrive);
    CHECK(computePostFx(start + 0.01f, cfg.sprintSpeed, cfg.maxSpeed, 0.0f).speedLines > 0.0f);
    CHECK(momentumGauge(start - 0.01f).tier == MomentumTier::Sprint);
}

TEST_CASE("Bad speeds read as standing still", "[hud][momentum][malformed]") {
    for (const float v : {-5.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        INFO(v);
        const MomentumGauge g = momentumGauge(v);
        CHECK(g.fill == 0.0f);
        CHECK(g.tier == MomentumTier::Idle);
    }
}

TEST_CASE("The gauge follows the movement config", "[hud][momentum]") {
    MovementConfig fast;
    fast.sprintSpeed = 20.0f;
    CHECK(momentumGauge(20.0f, fast).fill == Approx(0.5f));
    CHECK(momentumGauge(20.0f, fast).tier == MomentumTier::Sprint);
    CHECK(momentumGauge(10.5f, fast).tier != MomentumTier::Sprint);
}

// ---------------------------------------------------------------------------
// Directional damage
// ---------------------------------------------------------------------------

TEST_CASE("An attacker straight ahead, to the right, left and behind", "[hud][damage]") {
    float a = 99.0f;
    const glm::vec3 p{5.0f, 0.0f, 5.0f};
    REQUIRE(attackAngle(p, 0.0f, {5.0f, 0.0f, -5.0f}, a));   // yaw 0 faces -Z
    CHECK(a == Approx(0.0f).margin(1e-5));
    REQUIRE(attackAngle(p, 0.0f, {15.0f, 0.0f, 5.0f}, a));   // +X is the right
    CHECK(a == Approx(kPi / 2.0f));
    REQUIRE(attackAngle(p, 0.0f, {-5.0f, 0.0f, 5.0f}, a));
    CHECK(a == Approx(-kPi / 2.0f));
    REQUIRE(attackAngle(p, 0.0f, {5.0f, 0.0f, 15.0f}, a));
    CHECK(std::abs(a) == Approx(kPi));
}

TEST_CASE("The angle follows the camera yaw", "[hud][damage]") {
    float a = 0.0f;
    const glm::vec3 p{0.0f};
    // Facing -X (yaw +pi/2): -X is ahead, and -Z is now on the right.
    REQUIRE(attackAngle(p, kPi / 2.0f, {-10.0f, 0.0f, 0.0f}, a));
    CHECK(a == Approx(0.0f).margin(1e-5));
    REQUIRE(attackAngle(p, kPi / 2.0f, {0.0f, 0.0f, -10.0f}, a));
    CHECK(a == Approx(kPi / 2.0f));
}

TEST_CASE("Any bearing at any yaw comes back as the same relative angle", "[hud][damage]") {
    const glm::vec3 player{12.0f, 3.0f, -7.0f};
    for (float yaw = -3.0f; yaw <= 3.0f; yaw += 0.5f) {
        for (float angle = -3.1f; angle <= 3.1f; angle += 0.31f) {
            INFO("yaw " << yaw << " angle " << angle);
            float got = 0.0f;
            REQUIRE(attackAngle(player, yaw, attackerAt(player, yaw, angle, 9.0f), got));
            CHECK(std::abs(wrapDiff(got, angle)) < 1e-3f);
        }
    }
}

TEST_CASE("Distance and height do not change the direction", "[hud][damage]") {
    const glm::vec3 player{0.0f};
    float near = 0.0f, far = 0.0f, high = 0.0f;
    REQUIRE(attackAngle(player, 0.4f, attackerAt(player, 0.4f, 1.0f, 2.0f), near));
    REQUIRE(attackAngle(player, 0.4f, attackerAt(player, 0.4f, 1.0f, 80.0f), far));
    REQUIRE(attackAngle(player, 0.4f, attackerAt(player, 0.4f, 1.0f, 8.0f, 30.0f), high));
    CHECK(near == Approx(far));
    CHECK(near == Approx(high));
}

TEST_CASE("An attacker on top of the player has no direction", "[hud][damage][malformed]") {
    float a = 1.23f;
    CHECK_FALSE(attackAngle({1, 2, 3}, 0.0f, {1, 2, 3}, a));
    CHECK_FALSE(attackAngle({1, 2, 3}, 0.0f, {1.1f, 9.0f, 3.1f}, a));   // directly above
    CHECK(a == 1.23f);                                                   // untouched
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(attackAngle({0, 0, 0}, nan, {5, 0, 5}, a));
    CHECK_FALSE(attackAngle({0, 0, 0}, 0.0f, {nan, 0, 5}, a));
    CHECK(a == 1.23f);
}

TEST_CASE("A damage arc appears, fades, and goes away", "[hud][damage]") {
    DamageIndicators d;
    CHECK(d.arcs().empty());
    d.add(0.8f, 0.5f);
    REQUIRE(d.arcs().size() == 1);
    CHECK(d.arcs()[0].angle == Approx(0.8f));
    CHECK(d.arcs()[0].strength == Approx(0.5f));
    CHECK(d.arcs()[0].life == Approx(DamageIndicators::kLife));
    CHECK(DamageIndicators::alpha(d.arcs()[0]) == Approx(0.5f));

    float last = 2.0f;
    for (int i = 0; i < 30; ++i) {   // fades smoothly
        d.update(1.0f / 60.0f);
        REQUIRE(d.arcs().size() == 1);
        const float al = DamageIndicators::alpha(d.arcs()[0]);
        CHECK(al < last);
        CHECK(al > 0.0f);
        last = al;
    }
    for (int i = 0; i < 60; ++i) d.update(1.0f / 60.0f);   // past kLife in total
    CHECK(d.arcs().empty());
}

TEST_CASE("Hits from about the same direction share one arc", "[hud][damage]") {
    DamageIndicators d;
    d.add(0.0f, 0.3f);
    d.update(0.5f);
    d.add(0.2f, 0.9f);   // within kMergeAngle
    REQUIRE(d.arcs().size() == 1);
    CHECK(d.arcs()[0].life == Approx(DamageIndicators::kLife));   // refreshed
    CHECK(d.arcs()[0].strength == Approx(0.9f));                  // the harder hit wins
    CHECK(d.arcs()[0].angle > 0.0f);                              // drifts towards the new hit
    CHECK(d.arcs()[0].angle < 0.2f);

    d.add(0.0f, 0.1f);                                            // a weak hit does not weaken it
    CHECK(d.arcs()[0].strength == Approx(0.9f));

    d.add(2.0f, 0.5f);                                            // a different direction gets its own
    CHECK(d.arcs().size() == 2);
}

TEST_CASE("Arcs merge across the behind-you wrap-around", "[hud][damage]") {
    DamageIndicators d;
    d.add(3.1f, 0.5f);
    d.add(-3.1f, 0.5f);   // 0.08 rad apart, on opposite sides of +-pi
    CHECK(d.arcs().size() == 1);
}

TEST_CASE("Only a few arcs are shown; the oldest is replaced", "[hud][damage]") {
    DamageIndicators d;
    const std::size_t cap = DamageIndicators::kMax;
    for (std::size_t i = 0; i < cap; ++i) {
        d.add(-3.0f + static_cast<float>(i) * 1.0f, 0.5f);
        d.update(0.1f);   // earlier arcs are older
    }
    REQUIRE(d.arcs().size() == cap);
    const float oldestAngle = d.arcs()[0].angle;
    d.add(2.9f, 0.7f);    // a direction not near any arc
    REQUIRE(d.arcs().size() == cap);
    bool hasNew = false, hasOldest = false;
    for (const DamageArc& a : d.arcs()) {
        hasNew    = hasNew || std::abs(a.angle - 2.9f) < 1e-4f;
        hasOldest = hasOldest || std::abs(a.angle - oldestAngle) < 1e-4f;
    }
    CHECK(hasNew);
    CHECK_FALSE(hasOldest);
}

TEST_CASE("When the list is full the arc closest to fading out is replaced, wherever it is", "[hud][damage]") {
    DamageIndicators d;
    for (std::size_t i = 0; i < DamageIndicators::kMax; ++i) {
        d.add(-3.0f + static_cast<float>(i), 0.5f);   // arcs 0..5, one radian apart
        d.update(0.1f);
    }
    d.add(-3.0f, 0.5f);   // a fresh hit on the first arc: now arc 1 is the oldest
    REQUIRE(d.arcs().size() == DamageIndicators::kMax);
    const float second = d.arcs()[1].angle;
    d.add(2.64f, 0.5f);   // nowhere near any arc (the gap across +-pi is 1.28 rad)
    REQUIRE(d.arcs().size() == DamageIndicators::kMax);
    bool hasFirst = false, hasSecond = false, hasNew = false;
    for (const DamageArc& a : d.arcs()) {
        hasFirst  = hasFirst  || std::abs(a.angle - (-3.0f)) < 0.2f;
        hasSecond = hasSecond || std::abs(a.angle - second) < 1e-4f;
        hasNew    = hasNew    || std::abs(a.angle - 2.64f) < 1e-4f;
    }
    CHECK(hasFirst);       // refreshed, so it stays
    CHECK_FALSE(hasSecond);  // the oldest went
    CHECK(hasNew);
}

TEST_CASE("Bad arc input is ignored or clamped", "[hud][damage][malformed]") {
    DamageIndicators d;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    d.add(nan, 0.5f);
    d.add(0.0f, nan);
    d.add(std::numeric_limits<float>::infinity(), 0.5f);
    CHECK(d.arcs().empty());

    d.add(1.0f, 7.0f);
    REQUIRE(d.arcs().size() == 1);
    CHECK(d.arcs()[0].strength == 1.0f);
    d.clear();
    d.add(1.0f, -3.0f);
    CHECK(d.arcs()[0].strength == 0.0f);
    CHECK(DamageIndicators::alpha(d.arcs()[0]) == 0.0f);

    d.update(0.0f);
    d.update(-1.0f);
    d.update(nan);
    REQUIRE(d.arcs().size() == 1);
    CHECK(d.arcs()[0].life == Approx(DamageIndicators::kLife));
    d.clear();
    CHECK(d.arcs().empty());
}

// ---------------------------------------------------------------------------
// Seize / human shield
// ---------------------------------------------------------------------------

TEST_CASE("The seize prompt shows only for a staggered enemy in reach", "[hud][seize]") {
    CHECK(seizeHud(false, false, 0, 0, 0).state == SeizeState::None);
    CHECK(seizeHud(false, true, 0, 0, 0).state == SeizeState::CanSeize);
    const SeizeHud none = seizeHud(false, false, 50, 100, 5);
    CHECK(none.shield01 == 0.0f);        // nothing is held, so no shield to show
    CHECK_FALSE(none.throwReady);
    CHECK(none.grip01 == 0.0f);
}

TEST_CASE("Holding a shield shows its integrity and wins over a seizable enemy", "[hud][seize]") {
    const SeizeHud h = seizeHud(true, true, 60.0f, 100.0f, 1.0f);
    CHECK(h.state == SeizeState::Holding);
    CHECK(h.shield01 == Approx(0.6f));
    CHECK_FALSE(h.shieldWeak);
    CHECK(h.throwReady);
    CHECK(seizeHud(true, false, 100.0f, 100.0f, 1.0f).shield01 == 1.0f);
}

TEST_CASE("The shield is weak below a quarter, and not ready when gone", "[hud][seize]") {
    CHECK_FALSE(seizeHud(true, false, 25.0f, 100.0f, 1.0f).shieldWeak);   // exactly a quarter is not yet weak
    CHECK(seizeHud(true, false, 24.0f, 100.0f, 1.0f).shieldWeak);
    CHECK(seizeHud(true, false, 1.0f, 100.0f, 1.0f).throwReady);
    const SeizeHud gone = seizeHud(true, false, 0.0f, 100.0f, 1.0f);
    CHECK(gone.shieldWeak);
    CHECK_FALSE(gone.throwReady);
    CHECK(gone.shield01 == 0.0f);
    CHECK(seizeHud(true, false, -10.0f, 100.0f, 1.0f).shield01 == 0.0f);    // overkill damage
    CHECK(seizeHud(true, false, 300.0f, 100.0f, 1.0f).shield01 == 1.0f);    // never over full
}

TEST_CASE("The grip pip fills over the first moments of a hold", "[hud][seize]") {
    CHECK(seizeHud(true, false, 100, 100, 0.0f).grip01 == 0.0f);
    CHECK(seizeHud(true, false, 100, 100, kGripSeconds * 0.5f).grip01 == Approx(0.5f));
    CHECK(seizeHud(true, false, 100, 100, kGripSeconds).grip01 == Approx(1.0f));
    CHECK(seizeHud(true, false, 100, 100, 99.0f).grip01 == 1.0f);
    float last = -1.0f;
    for (float t = 0.0f; t < 1.0f; t += 0.05f) {
        const float g = seizeHud(true, false, 100, 100, t).grip01;
        CHECK(g >= last);
        last = g;
    }
}

TEST_CASE("The HUD state walks nothing -> seize -> hold -> nothing", "[hud][seize]") {
    // A staggered enemy comes into reach, is grabbed, takes some shots, is thrown.
    CHECK(seizeHud(false, false, 0, 0, 0).state == SeizeState::None);
    CHECK(seizeHud(false, true, 100, 100, 0).state == SeizeState::CanSeize);
    SeizeHud h = seizeHud(true, false, 100, 100, 0.0f);
    CHECK(h.state == SeizeState::Holding);
    CHECK(h.shield01 == 1.0f);
    h = seizeHud(true, false, 40, 100, 1.0f);
    CHECK(h.shield01 == Approx(0.4f));
    h = seizeHud(true, false, 10, 100, 2.0f);
    CHECK(h.shieldWeak);
    CHECK(h.throwReady);
    CHECK(seizeHud(false, false, 10, 100, 2.0f).state == SeizeState::None);   // thrown
}

TEST_CASE("Bad shield numbers show an empty shield, not nonsense", "[hud][seize][malformed]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const SeizeHud& h : {seizeHud(true, false, nan, 100.0f, 1.0f), seizeHud(true, false, 50.0f, nan, 1.0f),
                              seizeHud(true, false, 50.0f, 0.0f, 1.0f), seizeHud(true, false, 50.0f, -10.0f, 1.0f)}) {
        CHECK(h.state == SeizeState::Holding);
        CHECK(h.shield01 == 0.0f);
        CHECK_FALSE(h.throwReady);
    }
    CHECK(seizeHud(true, false, 50.0f, 100.0f, nan).grip01 == 0.0f);
}

// ---------------------------------------------------------------------------
// Low health
// ---------------------------------------------------------------------------

TEST_CASE("The crosshair warns below 25 HP and only while alive", "[hud][health]") {
    CHECK_FALSE(crosshairWarning(100.0f, 0.0f).active);
    CHECK_FALSE(crosshairWarning(25.0f, 0.0f).active);   // exactly 25 is fine
    CHECK(crosshairWarning(24.9f, 0.0f).active);
    CHECK(crosshairWarning(1.0f, 0.0f).active);
    CHECK_FALSE(crosshairWarning(0.0f, 0.0f).active);    // dead: the death fade takes over
    CHECK_FALSE(crosshairWarning(-5.0f, 0.0f).active);
    CHECK_FALSE(crosshairWarning(std::numeric_limits<float>::quiet_NaN(), 0.0f).active);
}

TEST_CASE("Low-health severity rises as health falls", "[hud][health]") {
    CHECK(lowHealthSeverity(25.0f) == 0.0f);
    CHECK(lowHealthSeverity(100.0f) == 0.0f);
    CHECK(lowHealthSeverity(0.0f) == 0.0f);
    CHECK(lowHealthSeverity(12.5f) == Approx(0.5f));
    CHECK(lowHealthSeverity(0.5f) == Approx(0.98f));
    float last = 0.0f;
    for (float hp = 24.0f; hp > 0.0f; hp -= 1.0f) {
        const float s = lowHealthSeverity(hp);
        CHECK(s > last);
        CHECK(s <= 1.0f);
        last = s;
    }
}

TEST_CASE("The heartbeat is a bounded double thump that repeats", "[hud][health]") {
    CHECK(lowHealthPulse(100.0f, 0.3f) == 0.0f);   // healthy: no pulse at any time
    CHECK(lowHealthPulse(25.0f, 0.3f) == 0.0f);
    CHECK(lowHealthPulse(0.0f, 0.3f) == 0.0f);

    constexpr float kPeriod = 1.0f / 1.2f;
    float peak = 0.0f, trough = 1.0f;
    for (float t = 0.0f; t < 3.0f; t += 0.01f) {
        const float p = lowHealthPulse(10.0f, t);
        CHECK(p >= 0.0f);
        CHECK(p <= 1.0f);
        peak   = std::max(peak, p);
        trough = std::min(trough, p);
        CHECK(lowHealthPulse(10.0f, t + kPeriod) == Approx(p).margin(1e-3));   // repeats every beat
    }
    CHECK(peak > 0.4f);       // clearly visible
    CHECK(trough < 0.01f);    // and fades to nothing between beats

    // Two thumps per beat: a second local peak after the first has died down.
    const float first = lowHealthPulse(10.0f, 0.0f);
    const float dip   = lowHealthPulse(10.0f, 0.12f * kPeriod);
    const float second = lowHealthPulse(10.0f, 0.25f * kPeriod);
    CHECK(dip < first);
    CHECK(second > dip);
    CHECK(second < first);
}

TEST_CASE("The heartbeat is stronger the closer to death", "[hud][health]") {
    CHECK(lowHealthPulse(2.0f, 0.0f) > lowHealthPulse(12.0f, 0.0f));
    CHECK(lowHealthPulse(12.0f, 0.0f) > lowHealthPulse(24.0f, 0.0f));
    CHECK(lowHealthPulse(24.0f, 0.0f) > 0.0f);
}

TEST_CASE("Low health pulses the PostFx vignette, subtly", "[hud][health][postfx]") {
    const MovementConfig cfg;
    const auto fx = [&](float hp, float t, float shake = 0.0f) {
        return computePostFx(0.0f, cfg.sprintSpeed, cfg.maxSpeed, shake, hp, t);
    };
    CHECK(fx(100.0f, 0.0f).crimson == 0.0f);          // healthy: nothing, as before
    CHECK(fx(25.0f, 0.0f).crimson == 0.0f);
    CHECK(fx(10.0f, 0.0f).crimson > 0.0f);            // low: the vignette flushes red
    float peak = 0.0f;
    for (float t = 0.0f; t < 2.0f; t += 0.01f) peak = std::max(peak, fx(1.0f, t).crimson);
    CHECK(peak <= 0.45f + 1e-6f);                     // subtle: capped well below a hard hit
    CHECK(peak > 0.3f);

    // Heavy camera trauma still wins.
    CHECK(fx(10.0f, 0.5f, 1.0f).crimson == Approx(1.0f));
    // It does not touch the speed lines.
    CHECK(computePostFx(30.0f, cfg.sprintSpeed, cfg.maxSpeed, 0.0f, 5.0f, 0.0f).speedLines == Approx(1.0f));
}

TEST_CASE("PostFx without a health argument behaves exactly as before", "[hud][postfx]") {
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 0.0f).crimson == 0.0f);
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 1.0f).crimson == Approx(1.0f));
    CHECK(computePostFx(30.0f, 10.5f, 30.0f, 0.0f).speedLines == Approx(1.0f));
}

TEST_CASE("Non-finite health or time never leaves a stuck pulse", "[hud][health][malformed]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, inf, -inf}) {
        CHECK(lowHealthPulse(bad, 0.0f) == 0.0f);
        CHECK(lowHealthPulse(10.0f, bad) == 0.0f);
        CHECK(computePostFx(0.0f, 10.5f, 30.0f, 0.0f, bad, 0.0f).crimson == 0.0f);
        CHECK(computePostFx(0.0f, 10.5f, 30.0f, 0.0f, 10.0f, bad).crimson == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

namespace {

using ui::Menu;
using ui::MenuAction;
using ui::MenuContext;
using ui::MenuInput;
using ui::Screen;

MenuInput nav(bool up = false, bool down = false, bool confirm = false) {
    MenuInput in;
    in.up = up;
    in.down = down;
    in.confirm = confirm;
    in.cursor = {5.0f, 5.0f};   // the mouse is off screen
    return in;
}
MenuInput clickAt(glm::vec2 p) {
    MenuInput in;
    in.cursor = p;
    in.clicked = true;
    return in;
}
MenuInput moveTo(glm::vec2 p) {
    MenuInput in;
    in.cursor = p;
    return in;
}

MenuContext withSave() {
    MenuContext c;
    c.canContinue = true;
    c.continueDetail = "KODIAK_HUB";
    return c;
}

std::vector<std::string> labels(const std::vector<ui::MenuItem>& items) {
    std::vector<std::string> out;
    for (const auto& i : items) out.push_back(i.label);
    return out;
}

// A menu that has started a game and then paused it.
Menu pausedMenu() {
    Menu m;
    (void)m.update(nav(false, false, true), kAspect);   // NEW GAME
    MenuInput esc;
    esc.esc = true;
    (void)m.update(esc, kAspect);
    return m;
}

} // namespace

TEST_CASE("The main menu has the five rows, in order", "[menu][hud]") {
    const auto items = ui::mainMenuItems({});
    CHECK(labels(items) == std::vector<std::string>{"NEW GAME", "CONTINUE", "SELECT LEVEL / HUB", "SETTINGS", "QUIT GAME"});
    CHECK(items[0].action == MenuAction::Play);
    CHECK(items[1].action == MenuAction::Continue);
    CHECK(items[2].action == MenuAction::SelectLevel);
    CHECK(items[3].opensSettings);
    CHECK(items[4].action == MenuAction::Quit);
    CHECK(items[4].danger);
}

TEST_CASE("Continue is available only when there is a save", "[menu][hud]") {
    const auto without = ui::mainMenuItems({});
    CHECK_FALSE(without[1].enabled);
    CHECK(without[1].detail.empty());

    const auto with = ui::mainMenuItems(withSave());
    CHECK(with[1].enabled);
    CHECK(with[1].detail == "KODIAK_HUB");   // it says which level you would continue in

    MenuContext stale;                        // a leftover label with no save does not show
    stale.continueDetail = "OLD";
    CHECK(ui::mainMenuItems(stale)[1].detail.empty());
}

TEST_CASE("Select Level is available only once the hub is unlocked", "[menu][hud]") {
    MenuContext c;
    CHECK_FALSE(ui::mainMenuItems(c)[2].enabled);
    c.canSelectLevel = true;
    CHECK(ui::mainMenuItems(c)[2].enabled);
    CHECK(ui::mainMenuItems(c)[1].enabled == false);   // independent of Continue
    // Everything else is always there.
    for (const std::size_t i : {std::size_t{0}, std::size_t{3}, std::size_t{4}}) CHECK(ui::mainMenuItems({})[i].enabled);
}

TEST_CASE("The pause menu has the six rows, in order", "[menu][hud]") {
    const auto items = ui::pauseMenuItems();
    CHECK(labels(items) == std::vector<std::string>{"RESUME", "LOAD LAST CHECKPOINT", "SAVE CHECKPOINT", "RESTART LEVEL",
                                                    "SETTINGS", "QUIT TO MAIN MENU"});
    CHECK(items[0].action == MenuAction::Play);
    CHECK(items[1].action == MenuAction::LoadCheckpoint);
    CHECK(items[2].action == MenuAction::SaveCheckpoint);
    CHECK(items[3].action == MenuAction::RestartLevel);
    CHECK(items[4].opensSettings);
    CHECK(items[5].action == MenuAction::MainMenu);
    for (const auto& i : items) CHECK(i.enabled);
}

TEST_CASE("Menu rows are spaced, on screen and shared by drawing and hit-testing", "[menu][hud]") {
    for (const Screen s : {Screen::Main, Screen::Pause}) {
        const std::size_t n = s == Screen::Main ? ui::mainMenuItems({}).size() : ui::pauseMenuItems().size();
        for (std::size_t i = 0; i < n; ++i) {
            const float y = ui::itemCenterY(s, i);
            CHECK(y + ui::kItemHalfHeight < 1.0f);
            CHECK(y - ui::kItemHalfHeight > -1.0f);
            if (i > 0) CHECK(ui::itemCenterY(s, i - 1) - y > 2.0f * ui::kItemHalfHeight);   // rows do not overlap
        }
    }
    CHECK(ui::itemCenterY(Screen::Main, 0) == Approx(0.10f));   // New Game sits where Play always did
}

TEST_CASE("Keyboard focus starts on the first usable row and skips disabled ones", "[menu][hud]") {
    Menu m;
    REQUIRE(m.screen() == Screen::Main);
    (void)m.update(moveTo({5, 5}), kAspect);
    CHECK(m.focus() == 0);                             // NEW GAME

    (void)m.update(nav(false, true), kAspect);         // Down: Continue and Select Level are disabled
    CHECK(m.focus() == 3);                             // SETTINGS
    (void)m.update(nav(false, true), kAspect);
    CHECK(m.focus() == 4);                             // QUIT
    (void)m.update(nav(false, true), kAspect);
    CHECK(m.focus() == 0);                             // wraps to the top
    (void)m.update(nav(true, false), kAspect);
    CHECK(m.focus() == 4);                             // and up wraps to the bottom
}

TEST_CASE("With a save and an unlocked hub, every row can be reached", "[menu][hud]") {
    Menu m;
    MenuContext c = withSave();
    c.canSelectLevel = true;
    m.setContext(c);
    (void)m.update(moveTo({5, 5}), kAspect);
    std::vector<int> seen{m.focus()};
    for (int i = 0; i < 4; ++i) {
        (void)m.update(nav(false, true), kAspect);
        seen.push_back(m.focus());
    }
    CHECK(seen == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("Enter and Space choose the focused row", "[menu][hud]") {
    SECTION("New Game") {
        Menu m;
        CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::Play);
        CHECK_FALSE(m.active());
        CHECK(m.soundClick());
    }
    SECTION("Continue, when it is enabled") {
        Menu m;
        m.setContext(withSave());
        (void)m.update(moveTo({5, 5}), kAspect);
        (void)m.update(nav(false, true), kAspect);
        REQUIRE(m.focus() == 1);
        CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::Continue);
        CHECK_FALSE(m.active());
    }
    SECTION("Select Level, when the hub is unlocked") {
        Menu m;
        MenuContext c;
        c.canSelectLevel = true;
        m.setContext(c);
        (void)m.update(moveTo({5, 5}), kAspect);
        (void)m.update(nav(false, true), kAspect);
        REQUIRE(m.focus() == 2);
        CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::SelectLevel);
        CHECK_FALSE(m.active());
    }
    SECTION("Quit") {
        Menu m;
        (void)m.update(nav(true), kAspect);   // wrap to the bottom
        CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::Quit);
    }
    SECTION("nothing happens without a confirm key") {
        Menu m;
        CHECK(m.update(nav(), kAspect) == MenuAction::None);
        CHECK(m.active());
    }
}

TEST_CASE("A disabled row cannot be chosen by key or by mouse", "[menu][hud]") {
    Menu m;   // no save, hub locked
    // The mouse click on Continue does nothing, and makes no click sound.
    CHECK(m.update(clickAt({0.0f, ui::itemCenterY(Screen::Main, 1)}), kAspect) == MenuAction::None);
    CHECK(m.active());
    CHECK_FALSE(m.soundClick());
    CHECK(m.update(clickAt({0.0f, ui::itemCenterY(Screen::Main, 2)}), kAspect) == MenuAction::None);
    CHECK(m.active());
    // Neither can it take focus by hovering.
    (void)m.update(moveTo({0.0f, ui::itemCenterY(Screen::Main, 1)}), kAspect);
    CHECK(m.focus() == 0);
}

TEST_CASE("Clicking a row chooses it", "[menu][hud]") {
    SECTION("Continue") {
        Menu m;
        m.setContext(withSave());
        CHECK(m.update(clickAt({0.0f, ui::itemCenterY(Screen::Main, 1)}), kAspect) == MenuAction::Continue);
        CHECK_FALSE(m.active());
    }
    SECTION("Quit") {
        Menu m;
        CHECK(m.update(clickAt({0.0f, ui::itemCenterY(Screen::Main, 4)}), kAspect) == MenuAction::Quit);
    }
    SECTION("outside every row") {
        Menu m;
        CHECK(m.update(clickAt({0.95f, 0.9f}), kAspect) == MenuAction::None);
        CHECK(m.active());
    }
}

TEST_CASE("Hovering moves focus; a resting mouse does not fight the keyboard", "[menu][hud]") {
    Menu m;
    const glm::vec2 overQuit{0.0f, ui::itemCenterY(Screen::Main, 4)};
    (void)m.update(moveTo(overQuit), kAspect);
    CHECK(m.focus() == 4);
    CHECK(m.soundHover());

    // The mouse does not move; the keyboard takes over.
    MenuInput down = moveTo(overQuit);
    down.down = true;
    (void)m.update(down, kAspect);
    CHECK(m.focus() == 0);
    (void)m.update(moveTo(overQuit), kAspect);   // still resting there
    CHECK(m.focus() == 0);
    // Moving the mouse takes focus back.
    (void)m.update(moveTo({overQuit.x + 0.01f, overQuit.y}), kAspect);
    CHECK(m.focus() == 4);
}

TEST_CASE("Moving focus with the keyboard raises the hover sound", "[menu][hud]") {
    Menu m;
    (void)m.update(nav(), kAspect);
    CHECK_FALSE(m.soundHover());           // opening the menu is silent
    (void)m.update(nav(false, true), kAspect);
    CHECK(m.soundHover());
    (void)m.update(nav(), kAspect);
    CHECK_FALSE(m.soundHover());
}

TEST_CASE("Settings opens from the keyboard, ignores navigation keys, and comes back", "[menu][hud]") {
    Menu m;
    (void)m.update(nav(false, true), kAspect);     // -> SETTINGS (the only enabled row after NEW GAME)
    REQUIRE(m.focus() == 3);
    CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::None);
    CHECK(m.screen() == Screen::Options);
    CHECK(m.items().empty());
    CHECK(m.focus() == -1);

    // Up/Down/Enter are for rebinding keys here, not for the menu rows.
    for (int i = 0; i < 3; ++i) (void)m.update(nav(true, false, true), kAspect);
    CHECK(m.screen() == Screen::Options);

    MenuInput esc;
    esc.esc = true;
    (void)m.update(esc, kAspect);
    CHECK(m.screen() == Screen::Main);
    CHECK(m.focus() == 3);                          // remembers where you were
}

TEST_CASE("If the focused row becomes unavailable, Enter that tick does not fall through to another row", "[menu][hud]") {
    Menu m;
    m.setContext(withSave());
    (void)m.update(moveTo({5, 5}), kAspect);
    (void)m.update(nav(false, true), kAspect);
    REQUIRE(m.focus() == 1);                         // CONTINUE

    m.setContext({});                                // the save vanished
    CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::None);   // not NEW GAME
    CHECK(m.active());
    CHECK(m.focus() == 0);                           // focus fell back to the first usable row
    CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::Play);   // now Enter works again
}

TEST_CASE("ESC does nothing on the main menu", "[menu][hud]") {
    Menu m;
    MenuInput esc;
    esc.esc = true;
    CHECK(m.update(esc, kAspect) == MenuAction::None);
    CHECK(m.screen() == Screen::Main);
}

TEST_CASE("After a failed Continue the game can bring the main menu back", "[menu][hud]") {
    Menu m;
    m.setContext(withSave());
    (void)m.update(moveTo({5, 5}), kAspect);
    (void)m.update(nav(false, true), kAspect);
    CHECK(m.update(nav(false, false, true), kAspect) == MenuAction::Continue);
    REQUIRE_FALSE(m.active());
    m.showMain();                                   // the load failed
    CHECK(m.active());
    CHECK(m.screen() == Screen::Main);
    CHECK(m.items().size() == 5);
}

TEST_CASE("ESC opens the pause menu and ESC again resumes", "[menu][hud]") {
    Menu m = pausedMenu();
    CHECK(m.screen() == Screen::Pause);
    CHECK(m.items().size() == 6);
    CHECK(m.focus() == 0);
    MenuInput esc;
    esc.esc = true;
    CHECK(m.update(esc, kAspect) == MenuAction::Play);
    CHECK_FALSE(m.active());
}

TEST_CASE("Every pause row works from the keyboard", "[menu][hud]") {
    struct Row { int downs; MenuAction action; bool resumes; };
    const Row rows[] = {
        {0, MenuAction::Play,           true},
        {1, MenuAction::LoadCheckpoint, true},
        {2, MenuAction::SaveCheckpoint, false},   // stays in the pause menu
        {3, MenuAction::RestartLevel,   true},
        {5, MenuAction::MainMenu,       false},
    };
    for (const Row& r : rows) {
        INFO("row " << r.downs);
        Menu m = pausedMenu();
        for (int i = 0; i < r.downs; ++i) (void)m.update(nav(false, true), kAspect);
        REQUIRE(m.focus() == r.downs);
        CHECK(m.update(nav(false, false, true), kAspect) == r.action);
        CHECK(m.active() == !r.resumes);
        if (r.action == MenuAction::SaveCheckpoint) CHECK(m.screen() == Screen::Pause);
        if (r.action == MenuAction::MainMenu) CHECK(m.screen() == Screen::Main);
    }
}

TEST_CASE("Every pause row works with the mouse", "[menu][hud]") {
    const MenuAction expect[] = {MenuAction::Play, MenuAction::LoadCheckpoint, MenuAction::SaveCheckpoint,
                                 MenuAction::RestartLevel, MenuAction::None /* settings */, MenuAction::MainMenu};
    for (std::size_t i = 0; i < 6; ++i) {
        INFO("row " << i);
        Menu m = pausedMenu();
        CHECK(m.update(clickAt({0.0f, ui::itemCenterY(Screen::Pause, i)}), kAspect) == expect[i]);
        if (i == 4) CHECK(m.screen() == Screen::Options);
    }
}

TEST_CASE("Settings from the pause menu returns to the pause menu", "[menu][hud]") {
    Menu m = pausedMenu();
    (void)m.update(clickAt({0.0f, ui::itemCenterY(Screen::Pause, 4)}), kAspect);
    REQUIRE(m.screen() == Screen::Options);
    MenuInput esc;
    esc.esc = true;
    (void)m.update(esc, kAspect);
    CHECK(m.screen() == Screen::Pause);
}

TEST_CASE("Saving a checkpoint can show a short notice that expires", "[menu][hud]") {
    Menu m = pausedMenu();
    CHECK(m.notice().empty());
    m.notify("CHECKPOINT SAVED");
    CHECK(m.notice() == "CHECKPOINT SAVED");
    for (int i = 0; i < 100; ++i) (void)m.update(nav(), kAspect);
    CHECK(m.notice() == "CHECKPOINT SAVED");   // still there after under two seconds of updates
    for (int i = 0; i < 100; ++i) (void)m.update(nav(), kAspect);
    CHECK(m.notice().empty());
}

TEST_CASE("The menus draw something for every state", "[menu][hud]") {
    Menu m;
    for (const bool save : {false, true}) {
        MenuContext c = save ? withSave() : MenuContext{};
        c.canSelectLevel = save;
        m.setContext(c);
        (void)m.update(nav(), kAspect);
        std::vector<OverlayRect> out;
        m.draw(out);
        CHECK(out.size() > 20);
        for (const OverlayRect& r : out) {
            CHECK(r.min.x <= r.max.x);
            CHECK(r.min.y <= r.max.y);
            CHECK(r.color.a >= 0.0f);
            CHECK(r.color.a <= 1.0f);
        }
    }
    Menu paused = pausedMenu();
    std::vector<OverlayRect> out;
    (void)paused.update(nav(), kAspect);
    paused.draw(out);
    CHECK(out.size() > 20);
}
