#pragma once
#include <array>
#include <optional>
#include <string_view>

namespace carnage {

// Shading model selector. Values are mirrored by the M_* constants in
// assets/shaders/common.glsl; keep them in sync.
enum class Material : int {
    Concrete     = 0,  // board-formed concrete, stains, pits
    Tile         = 1,  // floor tiles with seams and wet patches
    PaintedMetal = 2,  // worn paint over bare/rusty metal
    Steel        = 3,  // smooth dark metal
    Brass        = 4,  // casings
    Flesh        = 5,  // targets
    Plastic      = 6,  // generic dielectric
    Emissive     = 7,  // unlit, HDR bright
    Skin         = 8,  // subsurface-scattered organic skin
    Cloth        = 9,  // woven tactical fabric
    Leather      = 10, // boots, gloves, straps

    // Texture-backed materials: a tileable albedo / normal / roughness-metal-AO set (generated at
    // startup, or loaded from assets/textures/), sampled triplanar in world space. These six are
    // consecutive so their texture-array layer is `value - kFirstTexturedMaterial`.
    ConcreteWorn    = 11,  // poured bunker concrete with grime seams
    MetalRusted     = 12,  // corrugated plated steel, edge rust
    AsphaltWet      = 13,  // cracked tarmac with wet puddles
    SnowPacked      = 14,  // wind-scoured snow and ice
    HazardTrim      = 15,  // yellow/black caution stripes (doors, gates, wall-run lanes)
    GrateIndustrial = 16,  // perforated catwalk grating

    Portal = 17,  // animated emissive: mission gates, checkpoints
};

inline constexpr int kFirstTexturedMaterial = 11;
inline constexpr int kTexturedMaterialCount = 6;

// The texture-array layer of a texture-backed material, or -1 for every other material.
constexpr int texturedLayer(Material m) {
    const int i = static_cast<int>(m) - kFirstTexturedMaterial;
    return (i >= 0 && i < kTexturedMaterialCount) ? i : -1;
}

// The names `.lvl` files use (`mat=concrete_worn`). Also the file stems for texture overrides.
struct MaterialName {
    std::string_view name;
    Material         material;
};

inline constexpr std::array<MaterialName, 18> kMaterialNames{{
    {"concrete", Material::Concrete},
    {"tile", Material::Tile},
    {"painted_metal", Material::PaintedMetal},
    {"steel", Material::Steel},
    {"brass", Material::Brass},
    {"flesh", Material::Flesh},
    {"plastic", Material::Plastic},
    {"emissive", Material::Emissive},
    {"skin", Material::Skin},
    {"cloth", Material::Cloth},
    {"leather", Material::Leather},
    {"concrete_worn", Material::ConcreteWorn},
    {"metal_rusted", Material::MetalRusted},
    {"asphalt_wet", Material::AsphaltWet},
    {"snow_packed", Material::SnowPacked},
    {"hazard_trim", Material::HazardTrim},
    {"grate_industrial", Material::GrateIndustrial},
    {"portal", Material::Portal},
}};

inline std::optional<Material> materialFromName(std::string_view name) {
    for (const MaterialName& m : kMaterialNames)
        if (m.name == name) return m.material;
    return std::nullopt;
}

inline std::string_view materialName(Material m) {
    for (const MaterialName& e : kMaterialNames)
        if (e.material == m) return e.name;
    return "concrete";
}

} // namespace carnage
