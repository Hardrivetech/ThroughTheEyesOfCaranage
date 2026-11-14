#include "game/Viewmodel.hpp"
#include "game/Weapons.hpp"
#include "gfx/Humanoid.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace carnage {

namespace {
constexpr glm::vec3 kPivot{0.20f, -0.19f, -0.42f};

glm::vec3 pivotFor(Hand hand) {
    return hand == Hand::Left ? glm::vec3(-kPivot.x, kPivot.y, kPivot.z) : kPivot;
}
}

glm::vec3 muzzleLocal(const WeaponStats& s, Hand hand) {
    return pivotFor(hand) + glm::vec3(0.0f, 0.0f, -s.length);
}

glm::vec3 ejectionPortLocal(const WeaponStats& s, Hand hand) {
    const float side = hand == Hand::Left ? -1.0f : 1.0f;
    return pivotFor(hand) + glm::vec3(-0.01f * side, s.viewSize.y * 0.4f, -s.length * 0.35f);
}

void buildViewmodel(const Weapon& w, float alpha, std::vector<DrawBox>& out, Hand hand,
                    bool otherHandBusy) {
    const WeaponStats& s = w.stats();

    if (std::string_view(s.name) == "Fists") {
        int side = 0;
        if (const auto* melee = dynamic_cast<const MeleeWeapon*>(&w)) {
            side = melee->punchSide();
        }
        gfx::buildViewmodelArmsAndFists("Fists", w.kick(alpha), side, w.kick(alpha),
                                        w.reloadProgress(), out, !otherHandBusy);
        return;
    }

    const float kick = w.kick(alpha);
    const float rp   = w.reloadProgress();
    const float dip  = rp >= 0.0f ? std::sin(rp * glm::pi<float>()) : 0.0f;
    const float mir  = hand == Hand::Left ? -1.0f : 1.0f;

    glm::vec3 pivot = pivotFor(hand);
    pivot.z += kick * 0.07f;
    pivot.y -= dip * 0.20f;

    const float pitch = kick * 0.10f + dip * 0.35f;
    const float roll  = (dip * 0.6f + kick * 0.05f) * mir;  // kicks roll outward per hand
    const glm::mat3 R = glm::mat3_cast(glm::angleAxis(roll, glm::vec3(0, 0, 1)) *
                                       glm::angleAxis(pitch, glm::vec3(1, 0, 0)));

    auto add = [&](glm::vec3 local, glm::vec3 half, glm::vec3 color,
                   Material mat = Material::Steel) {
        local.x *= mir;
        out.push_back({pivot + R * local, half, R, color, mat == Material::Emissive, mat});
    };

    const float L = s.length;
    const float w2 = s.viewSize.x * 0.5f;
    const float h2 = s.viewSize.y * 0.5f;
    const glm::vec3 dark = s.color * 0.55f;

    add({0.0f, 0.0f, -L * 0.35f}, {w2, h2, L * 0.35f}, s.color);                       // receiver
    add({0.0f, h2 * 0.35f, -L * 0.85f}, {w2 * 0.45f, w2 * 0.45f, L * 0.15f}, dark);    // barrel
    add({0.0f, -h2 - 0.05f, -L * 0.12f}, {w2 * 0.8f, 0.06f, 0.03f}, dark, Material::Plastic);  // grip
    add({0.0f, h2 + 0.012f, -L * 0.20f}, {0.006f, 0.012f, 0.012f}, dark);              // sight

    if (w.flashing()) {
        const float fs = s.flashSize * (0.6f + 0.4f * w.flashT());
        add({0.0f, h2 * 0.35f, -L - fs}, glm::vec3(fs), {1.0f, 0.85f, 0.45f},
                    Material::Emissive);
    }

    const glm::vec3 tapeCol{0.22f, 0.20f, 0.18f};
    const glm::vec3 gloveCol{0.10f, 0.10f, 0.12f};

    if (otherHandBusy || hand == Hand::Left) {
        // Single-handed grip: forearm plus glove wrapped round the grip.
        add({0.0f, -h2 - 0.07f, 0.10f}, {0.040f, 0.040f, 0.14f}, tapeCol, Material::Cloth);
        add({0.0f, -h2 - 0.05f, -L * 0.12f}, {0.040f, 0.040f, 0.04f}, gloveCol, Material::Leather);
        return;
    }

    // Two-handed: arms and gloved hands gripping the firearm.
    gfx::buildViewmodelArmsAndFists(s.name, 0.0f, 0, kick, rp, out, true);
}

} // namespace carnage
