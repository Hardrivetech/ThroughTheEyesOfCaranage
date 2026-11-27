#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/Environment.hpp"
#include "core/Material.hpp"
#include "core/PostFx.hpp"
#include "core/Record.hpp"
#include "game/Level.hpp"
#include "gfx/ProcTextures.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

using namespace carnage;
using Catch::Approx;

namespace {

const std::string kAssets = CARNAGE_ASSET_DIR;

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A level with one 10 x 10 floor (line 3), so a record on line 4 is easy to find.
const std::string kBase =
    "level name=t\n"
    "spawn pos=0,0,0\n"
    "block min=-5,-5 max=5,5 top=0\n";

bool mentions(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

const rec::Issue* issueOnLine(const LevelLoad& r, int line) {
    const auto it = std::find_if(r.issues.begin(), r.issues.end(), [&](const rec::Issue& i) { return i.line == line; });
    return it == r.issues.end() ? nullptr : &*it;
}

} // namespace

// ---------------------------------------------------------------------------
// Material names and the shader's copy of the enum
// ---------------------------------------------------------------------------

TEST_CASE("Every material has a unique name that round-trips", "[material]") {
    std::set<std::string_view> names;
    std::set<int>              values;
    for (const MaterialName& m : kMaterialNames) {
        CHECK(names.insert(m.name).second);
        CHECK(values.insert(static_cast<int>(m.material)).second);
        const auto back = materialFromName(m.name);
        REQUIRE(back.has_value());
        CHECK(*back == m.material);
        CHECK(materialName(m.material) == m.name);
        CHECK(rec::validId(m.name));
    }
    // Ids 0..17 with no gaps, so the shader can use them as plain integers.
    CHECK(kMaterialNames.size() == 18);
    CHECK(*values.begin() == 0);
    CHECK(*values.rbegin() == 17);
    CHECK_FALSE(materialFromName("marshmallow").has_value());
    CHECK_FALSE(materialFromName("").has_value());
    CHECK_FALSE(materialFromName("Concrete_Worn").has_value());  // names are lower-case
}

TEST_CASE("The six textured materials are consecutive and map to array layers", "[material]") {
    const Material textured[] = {Material::ConcreteWorn, Material::MetalRusted, Material::AsphaltWet,
                                 Material::SnowPacked, Material::HazardTrim, Material::GrateIndustrial};
    for (int i = 0; i < kTexturedMaterialCount; ++i) CHECK(texturedLayer(textured[i]) == i);
    for (const MaterialName& m : kMaterialNames) {
        const bool isTextured = std::find(std::begin(textured), std::end(textured), m.material) != std::end(textured);
        INFO(m.name);
        CHECK((texturedLayer(m.material) >= 0) == isTextured);
    }
    CHECK(texturedLayer(Material::Portal) == -1);
    CHECK(texturedLayer(Material::Leather) == -1);
}

TEST_CASE("common.glsl's material constants match the Material enum", "[material][shader]") {
    const std::string src = readFile(kAssets + "/shaders/common.glsl");
    REQUIRE_FALSE(src.empty());

    std::map<std::string, int> shader;
    const std::regex re(R"(const\s+int\s+M_([A-Z_]+)\s*=\s*(\d+)\s*;)");
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it)
        shader[(*it)[1]] = std::stoi((*it)[2]);

    for (const MaterialName& m : kMaterialNames) {
        std::string key;
        for (const char c : m.name) key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (key == "PAINTED_METAL") key = "PAINTED";  // the one name the shader shortens
        INFO(m.name << " -> M_" << key);
        REQUIRE(shader.count(key) == 1);
        CHECK(shader[key] == static_cast<int>(m.material));
    }
    CHECK(shader.size() == kMaterialNames.size());
}

TEST_CASE("scene.frag has a tile size for every textured material", "[material][shader]") {
    const std::string src = readFile(kAssets + "/shaders/scene.frag");
    REQUIRE_FALSE(src.empty());
    const std::regex re(R"(TEX_METRES\[(\d+)\]\s*=\s*float\[\d+\]\(([^)]*)\))");
    std::smatch m;
    REQUIRE(std::regex_search(src, m, re));
    CHECK(std::stoi(m[1]) == kTexturedMaterialCount);
    const std::string list = m[2];
    CHECK(std::count(list.begin(), list.end(), ',') + 1 == kTexturedMaterialCount);
}

// ---------------------------------------------------------------------------
// mat= in .lvl files
// ---------------------------------------------------------------------------

TEST_CASE("mat= sets a block's material; no key means concrete", "[material][level]") {
    const LevelLoad r = parseLevel(kBase +
        "block min=6,0 max=8,2 top=1 mat=concrete_worn\n"                        // 4: a flat block
        "block min=6,3 max=8,9 h0=0 h1=2 axis=z mat=grate_industrial\n"          // 5: a ramp
        "block min=9,0 max=10,2 top=3 base=0 mat=hazard_trim visible=0 door=0\n" // 6: a door
        "block min=11,0 max=12,2 top=1 mat=portal\n"                             // 7
        "block min=13,0 max=14,2 top=1\n");                                      // 8: the default
    REQUIRE(r.opened);
    CHECK(r.clean());
    const auto blocks = r.level.world.blocks();
    REQUIRE(blocks.size() == 6);
    CHECK(blocks[0].material == Material::Concrete);
    CHECK(blocks[1].material == Material::ConcreteWorn);
    CHECK(blocks[2].material == Material::GrateIndustrial);
    CHECK(blocks[2].h0 == 0.0f);   // still a ramp
    CHECK(blocks[2].h1 == 2.0f);
    CHECK(blocks[3].material == Material::HazardTrim);
    CHECK(blocks[3].door == 0);
    CHECK_FALSE(blocks[3].visible);
    CHECK(blocks[4].material == Material::Portal);
    CHECK(blocks[5].material == Material::Concrete);
}

TEST_CASE("Every material name works in mat= and in the older material=", "[material][level]") {
    for (const MaterialName& m : kMaterialNames) {
        INFO(m.name);
        for (const char* key : {"mat", "material"}) {
            const LevelLoad r = parseLevel(kBase + "block min=6,0 max=8,2 top=1 " + key + "=" + std::string(m.name) + "\n");
            CHECK(r.clean());
            REQUIRE(r.level.world.blocks().size() == 2);
            CHECK(r.level.world.blocks()[1].material == m.material);
        }
    }
}

TEST_CASE("An unknown material keeps the block with the default material and reports it", "[material][level][malformed]") {
    for (const char* key : {"mat", "material"}) {
        INFO(key);
        const LevelLoad r = parseLevel(kBase + "block min=6,0 max=8,2 top=1 color=0.1,0.2,0.3 " + key + "=marshmallow\n");
        REQUIRE(r.opened);
        const rec::Issue* issue = issueOnLine(r, 4);
        REQUIRE(issue != nullptr);
        CHECK(mentions(issue->message, "unknown material 'marshmallow'"));
        CHECK(mentions(issue->message, "using concrete"));
        // The block is still there, solid, with its other settings, in the default material.
        REQUIRE(r.level.world.blocks().size() == 2);
        const Block& b = r.level.world.blocks()[1];
        CHECK(b.material == Material::Concrete);
        CHECK(b.h0 == 1.0f);
        CHECK(b.color.g == Approx(0.2f));
        CHECK_FALSE(r.clean());
    }
    SECTION("a block that is wrong in another way is still dropped") {
        const LevelLoad r = parseLevel(kBase + "block min=6,0 max=6,2 top=1 mat=marshmallow\n");
        CHECK(r.level.world.blocks().size() == 1);
        CHECK(issueOnLine(r, 4) != nullptr);
    }
}

TEST_CASE("mat= and material= together are rejected", "[material][level][malformed]") {
    const LevelLoad r = parseLevel(kBase + "block min=6,0 max=8,2 top=1 mat=steel material=steel\n");
    const rec::Issue* issue = issueOnLine(r, 4);
    REQUIRE(issue != nullptr);
    CHECK(mentions(issue->message, "either 'mat' or 'material'"));
    CHECK(r.level.world.blocks().size() == 1);
}

TEST_CASE("A material name is case-sensitive and a typo'd key is still caught", "[material][level][malformed]") {
    CHECK(issueOnLine(parseLevel(kBase + "block min=6,0 max=8,2 top=1 mat=Steel\n"), 4) != nullptr);
    const LevelLoad typo = parseLevel(kBase + "block min=6,0 max=8,2 top=1 mt=steel\n");
    const rec::Issue* issue = issueOnLine(typo, 4);
    REQUIRE(issue != nullptr);
    CHECK(mentions(issue->message, "unknown key 'mt'"));
}

TEST_CASE("An unknown material inside an included file is reported against that file", "[material][level][include]") {
    const IncludeReader reader = [](std::string_view path) -> std::optional<std::string> {
        if (path == "levels/pois/p.lvl") return std::string("block min=0,0 max=1,1 top=1 mat=nope\n");
        return std::nullopt;
    };
    const LevelLoad r = parseLevel(kBase + "include path=levels/pois/p.lvl offset=20,0,0\n", reader);
    const rec::Issue* issue = issueOnLine(r, 1);
    REQUIRE(issue != nullptr);
    CHECK(issue->file == "levels/pois/p.lvl");
    CHECK(mentions(issue->message, "unknown material 'nope'"));
    REQUIRE(r.level.world.blocks().size() == 2);   // merged, in the default material
    CHECK(r.level.world.blocks()[1].material == Material::Concrete);
}

TEST_CASE("Included blocks keep their material", "[material][level][include]") {
    const IncludeReader reader = [](std::string_view path) -> std::optional<std::string> {
        if (path == "levels/pois/p.lvl") return std::string("block min=0,0 max=1,1 top=1 mat=metal_rusted\n");
        return std::nullopt;
    };
    const LevelLoad r = parseLevel(kBase + "include path=levels/pois/p.lvl offset=20,0,0 yaw=90\n", reader);
    CHECK(r.clean());
    REQUIRE(r.level.world.blocks().size() == 2);
    CHECK(r.level.world.blocks()[1].material == Material::MetalRusted);
}

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

TEST_CASE("The environment record sets the snow level", "[environment][material]") {
    const LevelLoad r = parseLevel(kBase + "environment snow=0.6\n");
    CHECK(r.clean());
    CHECK(r.level.hasEnvironment);
    CHECK(r.level.environment.snow == Approx(0.6f));
    // Setting the weather alone leaves the fog, far plane and shadows at their defaults...
    CHECK(r.level.environment.haze == Environment::kDefaultHaze);
    CHECK(r.level.environment.farPlane == Environment::kDefaultFarPlane);
    // ...and does not trigger the far-plane warning (the author did not choose those numbers).
    CHECK(r.issues.empty());

    CHECK(parseLevel(kBase + "environment snow=0\n").level.environment.snow == 0.0f);
    CHECK(parseLevel(kBase + "environment snow=1\n").level.environment.snow == 1.0f);
    CHECK(parseLevel(kBase).level.environment.snow == 0.0f);   // no record: no snow, as before
    CHECK(Environment{}.snow == 0.0f);
}

TEST_CASE("Bad snow values are reported and the record is ignored", "[environment][material][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"environment snow=-0.1", "'snow' must be between 0 and 1"},
        {"environment snow=1.5",  "'snow' must be between 0 and 1"},
        {"environment snow=deep", "not a number"},
        {"environment snow=nan",  "not a number"},
    };
    for (const Bad& c : cases) {
        INFO(c.line);
        const LevelLoad r = parseLevel(kBase + c.line + "\n");
        const rec::Issue* issue = issueOnLine(r, 4);
        REQUIRE(issue != nullptr);
        CHECK(mentions(issue->message, c.expect));
        CHECK_FALSE(r.level.hasEnvironment);
        CHECK(r.level.environment.snow == 0.0f);
    }
}

TEST_CASE("The far-plane warning still fires when the author sets fog or the far plane", "[environment]") {
    CHECK(parseLevel(kBase + "environment visibility=4000 far=500 snow=0.3\n").issues.size() == 1);
    CHECK(parseLevel(kBase + "environment far=100\n").issues.size() == 1);    // the default fog is too thin to hide a 100 m far plane
    CHECK(parseLevel(kBase + "environment visibility=400 far=600 snow=0.3\n").issues.empty());
}

TEST_CASE("sanitized() keeps snow in 0..1 and survives garbage", "[environment]") {
    Environment e;
    e.snow = 3.0f;                                          CHECK(sanitized(e).snow == 1.0f);
    e.snow = -2.0f;                                         CHECK(sanitized(e).snow == 0.0f);
    e.snow = std::numeric_limits<float>::quiet_NaN();       CHECK(sanitized(e).snow == 0.0f);
    e.snow = std::numeric_limits<float>::infinity();        CHECK(sanitized(e).snow == 1.0f);
    e.snow = 0.4f;                                          CHECK(sanitized(e).snow == 0.4f);
}

// ---------------------------------------------------------------------------
// Post effects
// ---------------------------------------------------------------------------

TEST_CASE("Speed lines stay off up to a sprint and reach full strength at the cap", "[postfx]") {
    constexpr float sprint = 10.5f, cap = 30.0f;
    CHECK(computePostFx(0.0f, sprint, cap, 0.0f).speedLines == 0.0f);
    CHECK(computePostFx(6.0f, sprint, cap, 0.0f).speedLines == 0.0f);            // walking
    CHECK(computePostFx(sprint, sprint, cap, 0.0f).speedLines == 0.0f);          // a plain sprint looks clean
    CHECK(computePostFx(cap, sprint, cap, 0.0f).speedLines == Approx(1.0f));
    CHECK(computePostFx(cap * 2.0f, sprint, cap, 0.0f).speedLines == Approx(1.0f));   // clamped

    float last = 0.0f;
    for (float v = 0.0f; v <= cap; v += 0.25f) {   // monotonic: faster never gives weaker lines
        const float s = computePostFx(v, sprint, cap, 0.0f).speedLines;
        CHECK(s >= last);
        CHECK(s >= 0.0f);
        CHECK(s <= 1.0f);
        last = s;
    }
    const float mid = computePostFx(20.0f, sprint, cap, 0.0f).speedLines;
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);
}

TEST_CASE("The crimson vignette follows camera trauma and ignores small bumps", "[postfx]") {
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 0.0f).crimson == 0.0f);
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 0.03f).crimson == 0.0f);   // footstep-sized shake
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 1.0f).crimson == Approx(1.0f));
    float last = 0.0f;
    for (float shake = 0.0f; shake <= 1.0f; shake += 0.02f) {
        const float c = computePostFx(0.0f, 10.5f, 30.0f, shake).crimson;
        CHECK(c >= last);
        last = c;
    }
    // The two effects are independent.
    CHECK(computePostFx(30.0f, 10.5f, 30.0f, 0.0f).crimson == 0.0f);
    CHECK(computePostFx(0.0f, 10.5f, 30.0f, 1.0f).speedLines == 0.0f);
}

TEST_CASE("Non-finite inputs never leave a stuck effect on screen", "[postfx]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, inf, -inf}) {
        INFO(bad);
        const PostFx a = computePostFx(bad, 10.5f, 30.0f, 0.0f);
        const PostFx b = computePostFx(0.0f, 10.5f, 30.0f, bad);
        CHECK(std::isfinite(a.speedLines));
        CHECK(std::isfinite(b.crimson));
        CHECK(a.speedLines >= 0.0f);
        CHECK(a.speedLines <= 1.0f);
        CHECK(b.crimson >= 0.0f);
        CHECK(b.crimson <= 1.0f);
    }
    CHECK(computePostFx(nan, 10.5f, 30.0f, 0.0f).speedLines == 0.0f);
    CHECK(computePostFx(20.0f, 30.0f, 10.0f, 0.0f).speedLines == 0.0f);   // nonsense thresholds: off, not NaN
}

// ---------------------------------------------------------------------------
// Procedural textures
// ---------------------------------------------------------------------------

namespace {

constexpr int kTestSize = 64;

struct Channel {
    double mean = 0.0, stddev = 0.0;
    int    lo = 255, hi = 0;
};

Channel channelStats(const std::vector<std::uint8_t>& img, int c) {
    Channel s;
    const std::size_t n = img.size() / 4;
    for (std::size_t i = 0; i < n; ++i) {
        const int v = img[i * 4 + static_cast<std::size_t>(c)];
        s.mean += v;
        s.lo = std::min(s.lo, v);
        s.hi = std::max(s.hi, v);
    }
    s.mean /= static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double d = img[i * 4 + static_cast<std::size_t>(c)] - s.mean;
        s.stddev += d * d;
    }
    s.stddev = std::sqrt(s.stddev / static_cast<double>(n));
    return s;
}

// Mean absolute difference between neighbouring texels, summed over RGB, across the wrap seam
// (last column/row to the first) and across every interior pair.
struct Seam { double wrap = 0.0, interior = 0.0; };

Seam seamStats(const std::vector<std::uint8_t>& img, int size, bool horizontal) {
    const auto px = [&](int x, int y, int c) {
        return static_cast<int>(img[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4 +
                                    static_cast<std::size_t>(c)]);
    };
    Seam s;
    long wrapN = 0, intN = 0;
    for (int a = 0; a < size; ++a) {          // `a` runs along the seam, `b` across it
        for (int b = 0; b < size; ++b) {
            const int nb = (b + 1) % size;
            double d = 0.0;
            for (int c = 0; c < 3; ++c)
                d += std::abs((horizontal ? px(b, a, c) : px(a, b, c)) - (horizontal ? px(nb, a, c) : px(a, nb, c)));
            if (b == size - 1) { s.wrap += d; ++wrapN; }
            else               { s.interior += d; ++intN; }
        }
    }
    s.wrap /= static_cast<double>(wrapN);
    s.interior /= static_cast<double>(intN);
    return s;
}

} // namespace

TEST_CASE("Texture sets have the right dimensions for every material", "[texture]") {
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        INFO("layer " << layer);
        const gfx::TextureSet set = gfx::generateTextureSet(layer, kTestSize);
        REQUIRE_FALSE(set.empty());
        CHECK(set.size == kTestSize);
        const std::size_t bytes = static_cast<std::size_t>(kTestSize) * kTestSize * 4;
        CHECK(set.albedo.size() == bytes);
        CHECK(set.normal.size() == bytes);
        CHECK(set.mask.size() == bytes);
        for (std::size_t i = 3; i < bytes; i += 4) REQUIRE(set.albedo[i] == 255);   // albedo is opaque
    }
}

TEST_CASE("The default texture size is the one the renderer allocates", "[texture]") {
    const gfx::TextureSet set = gfx::generateTextureSet(0);
    CHECK(set.size == gfx::kProcTextureSize);
    CHECK(set.albedo.size() == static_cast<std::size_t>(gfx::kProcTextureSize) * gfx::kProcTextureSize * 4);
}

TEST_CASE("Invalid layers and sizes give an empty set", "[texture][malformed]") {
    CHECK(gfx::generateTextureSet(-1, kTestSize).empty());
    CHECK(gfx::generateTextureSet(kTexturedMaterialCount, kTestSize).empty());
    CHECK(gfx::generateTextureSet(0, 0).empty());
    CHECK(gfx::generateTextureSet(0, 7).empty());
    CHECK(gfx::generateTextureSet(0, 4096).empty());
    CHECK(gfx::generateTextureSet(0, -64).empty());
    CHECK(gfx::generateTextureSet(0, kTestSize).size == kTestSize);
}

TEST_CASE("Every channel of every map carries detail, not a flat fill", "[texture]") {
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        INFO("layer " << layer);
        const gfx::TextureSet set = gfx::generateTextureSet(layer, kTestSize);
        // Albedo: lit, and varying in brightness.
        for (int c = 0; c < 3; ++c) CHECK(channelStats(set.albedo, c).mean > 5.0);
        CHECK(std::max({channelStats(set.albedo, 0).stddev, channelStats(set.albedo, 1).stddev,
                        channelStats(set.albedo, 2).stddev}) > 3.0);
        // Normal: the height channel varies and the normals lean (they are not all straight out).
        CHECK(channelStats(set.normal, 3).stddev > 3.0);
        CHECK(std::max(channelStats(set.normal, 0).stddev, channelStats(set.normal, 1).stddev) > 0.5);
        // Mask: roughness always varies, and there is some occlusion detail.
        CHECK(channelStats(set.mask, 0).stddev > 0.5);
        CHECK(channelStats(set.mask, 2).hi > 0);
    }
}

TEST_CASE("Normals are unit length and point out of the surface", "[texture]") {
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        INFO("layer " << layer);
        const gfx::TextureSet set = gfx::generateTextureSet(layer, kTestSize);
        double worst = 0.0;
        for (std::size_t i = 0; i < set.normal.size(); i += 4) {
            const double x = set.normal[i + 0] / 255.0 * 2.0 - 1.0;
            const double y = set.normal[i + 1] / 255.0 * 2.0 - 1.0;
            const double z = set.normal[i + 2] / 255.0 * 2.0 - 1.0;
            worst = std::max(worst, std::abs(std::sqrt(x * x + y * y + z * z) - 1.0));
            REQUIRE(z > 0.0);   // never faces into the surface
        }
        CHECK(worst < 0.02);    // 8-bit quantisation only
    }
}

TEST_CASE("Texture generation is deterministic", "[texture]") {
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        INFO("layer " << layer);
        const gfx::TextureSet a = gfx::generateTextureSet(layer, kTestSize);
        const gfx::TextureSet b = gfx::generateTextureSet(layer, kTestSize);
        CHECK(a.albedo == b.albedo);
        CHECK(a.normal == b.normal);
        CHECK(a.mask == b.mask);
    }
}

TEST_CASE("The materials are visibly different from each other", "[texture]") {
    std::vector<gfx::TextureSet> sets;
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) sets.push_back(gfx::generateTextureSet(layer, kTestSize));
    for (std::size_t i = 0; i < sets.size(); ++i)
        for (std::size_t j = i + 1; j < sets.size(); ++j) {
            INFO(i << " vs " << j);
            CHECK(sets[i].albedo != sets[j].albedo);
            CHECK(sets[i].mask != sets[j].mask);
        }
}

TEST_CASE("Every texture tiles: the wrap seam is no rougher than the inside", "[texture]") {
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        const gfx::TextureSet set = gfx::generateTextureSet(layer, kTestSize);
        struct Map { const char* name; const std::vector<std::uint8_t>* img; };
        for (const Map& m : {Map{"albedo", &set.albedo}, Map{"normal", &set.normal}, Map{"mask", &set.mask}}) {
            for (const bool horizontal : {true, false}) {
                INFO("layer " << layer << " " << m.name << (horizontal ? " left-right" : " top-bottom"));
                const Seam s = seamStats(*m.img, kTestSize, horizontal);
                // A real seam would be as rough as two unrelated columns, many times the neighbour
                // difference; allow 2x plus a little for noise.
                CHECK(s.wrap <= 2.0 * s.interior + 2.0);
            }
        }
    }
}

TEST_CASE("The materials have the surface properties their names promise", "[texture]") {
    using gfx::generateTextureSet;
    const auto layer = [](Material m) { return texturedLayer(m); };

    const auto concrete = generateTextureSet(layer(Material::ConcreteWorn), kTestSize);
    const auto rusted   = generateTextureSet(layer(Material::MetalRusted), kTestSize);
    const auto asphalt  = generateTextureSet(layer(Material::AsphaltWet), kTestSize);
    const auto snow     = generateTextureSet(layer(Material::SnowPacked), kTestSize);
    const auto hazard   = generateTextureSet(layer(Material::HazardTrim), kTestSize);
    const auto grate    = generateTextureSet(layer(Material::GrateIndustrial), kTestSize);

    SECTION("concrete is a non-metal and very rough") {
        CHECK(channelStats(concrete.mask, 1).hi == 0);
        CHECK(channelStats(concrete.mask, 0).mean > 180.0);
    }
    SECTION("rusted metal is mostly metal, with rust patches that are not") {
        CHECK(channelStats(rusted.mask, 1).mean > 80.0);
        CHECK(channelStats(rusted.mask, 1).lo < 100);
        CHECK(channelStats(rusted.albedo, 0).mean > channelStats(rusted.albedo, 2).mean);   // rust is red-brown
    }
    SECTION("asphalt is dark, with smooth wet puddles") {
        CHECK(channelStats(asphalt.albedo, 1).mean < 90.0);
        CHECK(channelStats(asphalt.mask, 0).lo < 40);    // the puddles are nearly mirror-smooth
        CHECK(channelStats(asphalt.mask, 0).hi > 180);   // the dry road is not
    }
    SECTION("snow is bright") {
        CHECK(channelStats(snow.albedo, 0).mean > 190.0);
        CHECK(channelStats(snow.albedo, 2).mean >= channelStats(snow.albedo, 0).mean);   // cool, never warm
        CHECK(channelStats(snow.albedo, 1).hi > 250);                                    // the glints
    }
    SECTION("hazard trim is yellow and black, and only the yellow glows") {
        const Channel r = channelStats(hazard.albedo, 0);
        CHECK(r.hi > 150);
        CHECK(r.lo < 60);   // the black stripes (near-black paint, sRGB ~40-50)
        const Channel glow = channelStats(hazard.mask, 3);
        CHECK(glow.hi > 200);
        CHECK(glow.lo == 0);
        CHECK(glow.mean > 40.0);
        CHECK(glow.mean < 200.0);
        // Wherever it glows it is yellow: red well above blue.
        for (std::size_t i = 0; i < hazard.mask.size(); i += 4)
            if (hazard.mask[i + 3] > 200) REQUIRE(hazard.albedo[i] > hazard.albedo[i + 2] + 40);
    }
    SECTION("the other materials do not glow") {
        for (const auto* set : {&concrete, &rusted, &asphalt, &snow, &grate}) CHECK(channelStats(set->mask, 3).hi == 0);
    }
    SECTION("grating has dark, fully occluded holes and metal bars") {
        const Channel ao = channelStats(grate.mask, 2);
        CHECK(ao.lo < 20);         // holes
        CHECK(ao.hi > 235);        // bars
        CHECK(channelStats(grate.mask, 1).hi > 200);
        const Channel height = channelStats(grate.normal, 3);
        CHECK(height.lo < 20);
        CHECK(height.hi > 150);
    }
}

// ---------------------------------------------------------------------------
// Override images
// ---------------------------------------------------------------------------

TEST_CASE("resampleRGBA keeps an image of the right size exactly", "[texture]") {
    const gfx::TextureSet set = gfx::generateTextureSet(0, 32);
    CHECK(gfx::resampleRGBA(set.albedo.data(), 32, 32, 32) == set.albedo);
}

TEST_CASE("resampleRGBA scales while keeping colour and size", "[texture]") {
    // A flat colour stays that colour at any size.
    std::vector<std::uint8_t> flat(5 * 3 * 4);
    for (std::size_t i = 0; i < flat.size(); i += 4) { flat[i] = 200; flat[i + 1] = 100; flat[i + 2] = 50; flat[i + 3] = 255; }
    for (const int target : {1, 4, 64}) {
        INFO(target);
        const auto out = gfx::resampleRGBA(flat.data(), 5, 3, target);
        REQUIRE(out.size() == static_cast<std::size_t>(target) * target * 4);
        for (std::size_t i = 0; i < out.size(); i += 4) {
            REQUIRE(out[i] == 200);
            REQUIRE(out[i + 1] == 100);
            REQUIRE(out[i + 2] == 50);
            REQUIRE(out[i + 3] == 255);
        }
    }
    // A down-sized texture is still the same texture (mean colour within a couple of levels).
    const gfx::TextureSet big = gfx::generateTextureSet(3, 128);
    const auto small = gfx::resampleRGBA(big.albedo.data(), 128, 128, 32);
    REQUIRE(small.size() == 32u * 32u * 4u);
    CHECK(channelStats(small, 0).mean == Approx(channelStats(big.albedo, 0).mean).margin(3.0));
}

TEST_CASE("resampleRGBA treats the image as tiling", "[texture]") {
    // Left half black, right half white: halfway across the wrap seam the result is grey, not black.
    std::vector<std::uint8_t> img(2 * 1 * 4, 255);
    img[0] = img[1] = img[2] = 0;
    const auto out = gfx::resampleRGBA(img.data(), 2, 1, 4);
    REQUIRE(out.size() == 4u * 4u * 4u);
    CHECK(out[0] > 0);                        // pixel 0 is pulled towards the white column on the other edge
    CHECK(out[0] < out[2 * 4]);               // but is still darker than the middle

    // The same holds vertically: black on top, white below, so the top row blends with the bottom.
    std::vector<std::uint8_t> tall(1 * 2 * 4, 255);
    tall[0] = tall[1] = tall[2] = 0;
    const auto vert = gfx::resampleRGBA(tall.data(), 1, 2, 4);
    REQUIRE(vert.size() == 4u * 4u * 4u);
    CHECK(vert[0] > 0);                       // row 0 is pulled towards the white row on the other edge
    CHECK(vert[0] < vert[2 * 4 * 4]);         // but is still darker than the row below it
}

TEST_CASE("resampleRGBA rejects bad input", "[texture][malformed]") {
    const std::uint8_t px[4] = {1, 2, 3, 4};
    CHECK(gfx::resampleRGBA(nullptr, 1, 1, 4).empty());
    CHECK(gfx::resampleRGBA(px, 0, 1, 4).empty());
    CHECK(gfx::resampleRGBA(px, 1, -1, 4).empty());
    CHECK(gfx::resampleRGBA(px, 1, 1, 0).empty());
    CHECK(gfx::resampleRGBA(px, 1, 1, 100000).empty());
    CHECK(gfx::resampleRGBA(px, 1, 1, 1) == std::vector<std::uint8_t>({1, 2, 3, 4}));
}

// ---------------------------------------------------------------------------
// The shipped levels use the new materials
// ---------------------------------------------------------------------------

namespace {

LevelLoad loadShipped(const char* id) { return loadLevelFile(kAssets + "/levels/" + id + ".lvl"); }

std::set<Material> materialsIn(const LevelData& level) {
    std::set<Material> out;
    for (const Block& b : level.world.blocks()) out.insert(b.material);
    return out;
}

} // namespace

TEST_CASE("The shipped open-world levels load without a single issue", "[level][material]") {
    for (const char* id : {"kodiak_island", "kodiak_hub", "kodiak_range", "level01_escape"}) {
        INFO(id);
        const LevelLoad r = loadShipped(id);
        REQUIRE(r.opened);
        for (const rec::Issue& i : r.issues) FAIL_CHECK(rec::describe(i, id));
        CHECK(r.clean());
    }
}

TEST_CASE("kodiak_island and the hub use every textured material, with weather", "[level][material]") {
    for (const char* id : {"kodiak_island", "kodiak_hub"}) {
        INFO(id);
        const LevelLoad r = loadShipped(id);
        REQUIRE(r.clean());
        const std::set<Material> used = materialsIn(r.level);
        for (const Material m : {Material::ConcreteWorn, Material::MetalRusted, Material::GrateIndustrial,
                                 Material::HazardTrim, Material::Portal}) {
            INFO(materialName(m));
            CHECK(used.count(m) == 1);
        }
        CHECK(r.level.environment.snow > 0.0f);
        CHECK(r.level.environment.snow <= 1.0f);
    }
    // The island is the outdoor map: snow on the ground, wet tarmac on the roads.
    const std::set<Material> island = materialsIn(loadShipped("kodiak_island").level);
    CHECK(island.count(Material::SnowPacked) == 1);
    CHECK(island.count(Material::AsphaltWet) == 1);
    CHECK(loadShipped("kodiak_island").level.environment.snow > loadShipped("kodiak_hub").level.environment.snow);
}

TEST_CASE("Every door and gate in the shipped levels is marked with hazard trim", "[level][material]") {
    for (const char* id : {"kodiak_island", "kodiak_hub"}) {
        INFO(id);
        const LevelLoad r = loadShipped(id);
        REQUIRE(r.clean());
        int doors = 0;
        for (const Block& b : r.level.world.blocks())
            if (b.door >= 0) {
                ++doors;
                CHECK(b.material == Material::HazardTrim);
            }
        CHECK(doors >= 2);   // the hub has two; the island gets two more from its POIs
    }
}

TEST_CASE("Every portal in the shipped levels has an animated gate frame beside it", "[level][material]") {
    for (const char* id : {"kodiak_island", "kodiak_hub", "kodiak_range"}) {
        INFO(id);
        const LevelLoad r = loadShipped(id);
        REQUIRE(r.clean());
        int portals = 0;
        for (const TriggerVolume& t : r.level.triggers) {
            if (t.action != TriggerAction::Portal) continue;
            ++portals;
            bool framed = false;
            for (const Block& b : r.level.world.blocks()) {
                // A block whose footprint is within 2 m of the trigger's is part of the gate.
                const bool nearX = b.max.x > t.min.x - 2.0f && b.min.x < t.max.x + 2.0f;
                const bool nearZ = b.max.y > t.min.z - 2.0f && b.min.y < t.max.z + 2.0f;
                if (!nearX || !nearZ) continue;
                INFO("portal to " << t.target << ", block at " << b.min.x << "," << b.min.y);
                // Every glowing piece of the gate is the animated kind, never the plain steady glow.
                CHECK(b.material != Material::Emissive);
                framed = framed || b.material == Material::Portal;
            }
            INFO("portal to " << t.target);
            CHECK(framed);
        }
        CHECK(portals >= 1);
    }
}

TEST_CASE("Moving to textured materials did not change any geometry", "[level][material]") {
    // Only the look changed: the island still has the same number of blocks per material family
    // that existed before, and every block keeps its footprint (a regression guard for the rewrite).
    const LevelLoad island = loadShipped("kodiak_island");
    REQUIRE(island.clean());
    const auto blocks = island.level.world.blocks();
    // The basin floor is the first block, flat at height 0 across the whole basin.
    REQUIRE_FALSE(blocks.empty());
    CHECK(blocks[0].material == Material::SnowPacked);
    CHECK(blocks[0].min == glm::vec2(-500.0f, -1300.0f));
    CHECK(blocks[0].max == glm::vec2(500.0f, 100.0f));
    CHECK(blocks[0].topHeight() == 0.0f);
    // No block ended up with a degenerate footprint.
    for (const Block& b : blocks) {
        CHECK(b.max.x > b.min.x);
        CHECK(b.max.y > b.min.y);
    }
}
