#pragma once
#include "core/DrawTypes.hpp"
#include "game/Effects.hpp"
#include "game/HudState.hpp"
#include "game/Weapon.hpp"

#include <glm/glm.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace carnage {

// Builds screen-space overlay rectangles: crosshair, hit markers, projected
// impact flashes, muzzle tint, ammo pips and the reload bar.
class Hud {
public:
    void hitMarker(bool kill) { hit_ = 1.0f; kill_ = kill; }
    void update(float dt) { hit_ = hit_ > dt * 5.0f ? hit_ - dt * 5.0f : 0.0f; }

    // `left` is the off-hand weapon (null when empty or holding an item).
    void build(const Weapon& weapon, const Weapon* left, const Effects& fx,
               const glm::mat4& viewProj, float aspect, std::vector<OverlayRect>& out) const;

    // "PRESS [E] GRAB" style prompt, lower-centre.
    static void prompt(std::string_view key, std::string_view verb, float aspect,
                       std::vector<OverlayRect>& out);

    // Health/armor bars (bottom centre), red damage vignette and the death
    // fade. `damage01` is the recent-hit intensity, `fade` the death fade.
    static void vitals(float health01, float armor01, float damage01, float fade,
                       bool dead, float aspect, std::vector<OverlayRect>& out);

    // Horizontal speed gauge above the health bars: fills towards sprint, with ticks at sprint,
    // wall-run and the speed-line threshold, and turns crimson in the overdrive tier.
    static void momentum(const MomentumGauge& g, float aspect, std::vector<OverlayRect>& out);

    // "[F] SEIZE" while a staggered enemy is in reach; while holding one as a shield, its
    // integrity bar and the "[F] THROW" prompt. `key` is the player's bound grab/throw key.
    static void seize(const SeizeHud& s, std::string_view key, float aspect, std::vector<OverlayRect>& out);

    // Fading crimson arcs around the crosshair, one per recent attacker direction.
    static void damageArcs(std::span<const DamageArc> arcs, float aspect, std::vector<OverlayRect>& out);

    // Corner brackets around the crosshair that pulse with the heartbeat below 25 HP.
    static void crosshairWarning(const CrosshairWarning& w, float aspect, std::vector<OverlayRect>& out);

    // Objective line, top centre.
    static void objective(std::string_view text, float aspect, std::vector<OverlayRect>& out);

    // Tutorial hint, upper centre, on a dim backing strip. `alpha` is 0..1.
    static void tutorial(std::string_view text, float alpha, float aspect,
                         std::vector<OverlayRect>& out);

private:
    static void drawAmmo(const Weapon& w, float side, float aspect, std::vector<OverlayRect>& out);
    float hit_  = 0.0f;
    bool  kill_ = false;
};

} // namespace carnage
