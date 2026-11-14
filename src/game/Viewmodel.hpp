#pragma once
#include "core/DrawTypes.hpp"
#include "game/Weapon.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace carnage {

enum class Hand { Right, Left };

// Camera-space offsets (camera looks down -Z). The left hand mirrors X.
glm::vec3 muzzleLocal(const WeaponStats& s, Hand hand = Hand::Right);
glm::vec3 ejectionPortLocal(const WeaponStats& s, Hand hand = Hand::Right);

// Appends the weapon's boxes in camera space, including kick, reload dip and
// muzzle flash. `otherHandBusy` means the other hand holds something too, so
// this hand draws a single-handed grip instead of a two-handed one.
void buildViewmodel(const Weapon& weapon, float alpha, std::vector<DrawBox>& out,
                    Hand hand = Hand::Right, bool otherHandBusy = false);

} // namespace carnage
