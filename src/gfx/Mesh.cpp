#include "gfx/Mesh.hpp"
#include "core/Log.hpp"
#include "gfx/Assets.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace carnage::gfx {

// ---------------------------------------------------------------------------
// OBJ parser
// ---------------------------------------------------------------------------

namespace {

struct Key {
    int v, t, n;
    bool operator==(const Key&) const = default;
};

struct KeyHash {
    std::size_t operator()(const Key& k) const {
        return static_cast<std::size_t>(k.v) * 73856093u ^
               static_cast<std::size_t>(k.t + 1) * 19349663u ^
               static_cast<std::size_t>(k.n + 1) * 83492791u;
    }
};

std::string_view nextToken(std::string_view& s) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    std::size_t n = 0;
    while (n < s.size() && !isSpace(s[n])) ++n;
    const std::string_view token = s.substr(0, n);
    s.remove_prefix(n);
    return token;
}

bool toFloat(std::string_view t, float& out) {
    const auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    return r.ec == std::errc{} && r.ptr == t.data() + t.size();
}

bool toInt(std::string_view t, int& out) {
    const auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    return r.ec == std::errc{} && r.ptr == t.data() + t.size();
}

template <int N>
bool readFloats(std::string_view& line, float (&out)[N]) {
    for (int i = 0; i < N; ++i) {
        const std::string_view t = nextToken(line);
        if (t.empty() || !toFloat(t, out[i])) return false;
    }
    return true;
}

// Resolves a 1-based (or negative, relative) OBJ index to 0-based.
// An empty field resolves to -1 when `optional`.
bool resolveIndex(std::string_view field, std::size_t count, bool optional, int& out) {
    if (field.empty()) {
        out = -1;
        return optional;
    }
    int idx = 0;
    if (!toInt(field, idx) || idx == 0) return false;
    const long long resolved = idx > 0 ? idx - 1 : static_cast<long long>(count) + idx;
    if (resolved < 0 || resolved >= static_cast<long long>(count)) return false;
    out = static_cast<int>(resolved);
    return true;
}

} // namespace

void MeshData::computeBounds() {
    if (vertices.empty()) {
        boundsMin = boundsMax = glm::vec3(0.0f);
        return;
    }
    boundsMin = boundsMax = vertices.front().pos;
    for (const Vertex& v : vertices) {
        boundsMin = glm::min(boundsMin, v.pos);
        boundsMax = glm::max(boundsMax, v.pos);
    }
}

std::expected<MeshData, std::string> parseObj(std::string_view text) {
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;

    MeshData                                   mesh;
    std::unordered_map<Key, std::uint32_t, KeyHash> lookup;
    std::vector<char>                          needsNormal;  // parallel to mesh.vertices
    std::vector<std::uint32_t>                 face;
    int                                        lineNo = 0;

    auto fail = [&](std::string_view msg) {
        return std::unexpected(std::format("OBJ line {}: {}", lineNo, msg));
    };

    while (!text.empty()) {
        ++lineNo;
        const std::size_t nl = text.find('\n');
        std::string_view  line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos)
            line = line.substr(0, hash);

        const std::string_view tag = nextToken(line);
        if (tag.empty()) continue;

        if (tag == "v") {
            float p[3];
            if (!readFloats(line, p)) return fail("bad vertex position");
            positions.emplace_back(p[0], p[1], p[2]);
        } else if (tag == "vn") {
            float n[3];
            if (!readFloats(line, n)) return fail("bad vertex normal");
            const glm::vec3 v(n[0], n[1], n[2]);
            const float     len = glm::length(v);
            normals.push_back(len > 1e-8f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f));
        } else if (tag == "vt") {
            float t[2];
            if (!readFloats(line, t)) return fail("bad texture coordinate");
            uvs.emplace_back(t[0], t[1]);
        } else if (tag == "f") {
            face.clear();
            for (std::string_view tok = nextToken(line); !tok.empty(); tok = nextToken(line)) {
                // v, v/t, v//n or v/t/n
                std::string_view part[3];
                int              parts = 0;
                std::string_view rest  = tok;
                while (parts < 3) {
                    const std::size_t slash = rest.find('/');
                    part[parts++] = rest.substr(0, slash);
                    if (slash == std::string_view::npos) break;
                    rest = rest.substr(slash + 1);
                }

                Key key{-1, -1, -1};
                if (!resolveIndex(part[0], positions.size(), false, key.v)) return fail("bad vertex index");
                if (!resolveIndex(parts > 1 ? part[1] : std::string_view{}, uvs.size(), true, key.t))
                    return fail("bad texture index");
                if (!resolveIndex(parts > 2 ? part[2] : std::string_view{}, normals.size(), true, key.n))
                    return fail("bad normal index");

                auto [it, inserted] = lookup.try_emplace(key, static_cast<std::uint32_t>(mesh.vertices.size()));
                if (inserted) {
                    Vertex v{};
                    v.pos    = positions[static_cast<std::size_t>(key.v)];
                    v.normal = key.n >= 0 ? normals[static_cast<std::size_t>(key.n)] : glm::vec3(0.0f);
                    v.uv     = key.t >= 0 ? uvs[static_cast<std::size_t>(key.t)] : glm::vec2(0.0f);
                    mesh.vertices.push_back(v);
                    needsNormal.push_back(key.n < 0);
                }
                face.push_back(it->second);
            }
            if (face.size() < 3) return fail("face needs at least 3 vertices");

            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                const std::uint32_t a = face[0], b = face[i], c = face[i + 1];
                mesh.indices.insert(mesh.indices.end(), {a, b, c});

                // Area-weighted accumulation for vertices that lack a normal.
                const glm::vec3 fn = glm::cross(mesh.vertices[b].pos - mesh.vertices[a].pos,
                                                mesh.vertices[c].pos - mesh.vertices[a].pos);
                for (const std::uint32_t idx : {a, b, c})
                    if (needsNormal[idx]) mesh.vertices[idx].normal += fn;
            }
        }
        // Everything else (o, g, s, usemtl, mtllib, ...) is intentionally ignored.
    }

    if (mesh.indices.empty()) return std::unexpected("OBJ contains no faces");

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (!needsNormal[i]) continue;
        glm::vec3& n = mesh.vertices[i].normal;
        const float len = glm::length(n);
        n = len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    mesh.computeBounds();
    return mesh;
}

std::expected<MeshData, std::string> loadObj(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot open '{}'", file.string()));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto mesh = parseObj(text);
    if (!mesh) return std::unexpected(std::format("{}: {}", file.string(), mesh.error()));
    return mesh;
}

// ---------------------------------------------------------------------------
// Binary format
// ---------------------------------------------------------------------------

namespace {

struct CMeshHeader {
    char          magic[4];  // "CMSH"
    std::uint32_t version;
    std::uint32_t vertexCount;
    std::uint32_t indexCount;
    float         boundsMin[3];
    float         boundsMax[3];
};
static_assert(sizeof(CMeshHeader) == 40);
static_assert(std::endian::native == std::endian::little, ".cmesh files are little-endian");

constexpr std::uint32_t kCMeshVersion = 1;

} // namespace

std::expected<void, std::string> saveBinary(const std::filesystem::path& file, const MeshData& mesh) {
    CMeshHeader h{};
    std::memcpy(h.magic, "CMSH", 4);
    h.version     = kCMeshVersion;
    h.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    h.indexCount  = static_cast<std::uint32_t>(mesh.indices.size());
    std::memcpy(h.boundsMin, &mesh.boundsMin, sizeof h.boundsMin);
    std::memcpy(h.boundsMax, &mesh.boundsMax, sizeof h.boundsMax);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected(std::format("cannot write '{}'", file.string()));
    out.write(reinterpret_cast<const char*>(&h), sizeof h);
    out.write(reinterpret_cast<const char*>(mesh.vertices.data()),
              static_cast<std::streamsize>(mesh.vertices.size() * sizeof(Vertex)));
    out.write(reinterpret_cast<const char*>(mesh.indices.data()),
              static_cast<std::streamsize>(mesh.indices.size() * sizeof(std::uint32_t)));
    if (!out) return std::unexpected(std::format("write failed for '{}'", file.string()));
    return {};
}

std::expected<MeshData, std::string> loadBinary(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return std::unexpected(std::format("cannot open '{}'", file.string()));
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);

    CMeshHeader h{};
    if (size < sizeof h || !in.read(reinterpret_cast<char*>(&h), sizeof h))
        return std::unexpected("file too small for a .cmesh header");
    if (std::memcmp(h.magic, "CMSH", 4) != 0) return std::unexpected("bad .cmesh magic");
    if (h.version != kCMeshVersion)           return std::unexpected("unsupported .cmesh version");

    const std::size_t expected = sizeof h + static_cast<std::size_t>(h.vertexCount) * sizeof(Vertex) +
                                 static_cast<std::size_t>(h.indexCount) * sizeof(std::uint32_t);
    if (expected != size || h.indexCount % 3 != 0 || h.vertexCount == 0)
        return std::unexpected("corrupt .cmesh (size mismatch)");

    MeshData mesh;
    mesh.vertices.resize(h.vertexCount);
    mesh.indices.resize(h.indexCount);
    in.read(reinterpret_cast<char*>(mesh.vertices.data()),
            static_cast<std::streamsize>(mesh.vertices.size() * sizeof(Vertex)));
    in.read(reinterpret_cast<char*>(mesh.indices.data()),
            static_cast<std::streamsize>(mesh.indices.size() * sizeof(std::uint32_t)));
    if (!in) return std::unexpected("truncated .cmesh");

    for (const std::uint32_t i : mesh.indices)
        if (i >= h.vertexCount) return std::unexpected("corrupt .cmesh (index out of range)");

    std::memcpy(&mesh.boundsMin, h.boundsMin, sizeof h.boundsMin);
    std::memcpy(&mesh.boundsMax, h.boundsMax, sizeof h.boundsMax);
    return mesh;
}

std::expected<MeshData, std::string> loadMeshCached(const std::filesystem::path& relative) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path source = assets::root() / relative;
    fs::path       binary = source;
    binary.replace_extension(".cmesh");

    if (source.extension() == ".cmesh") return loadBinary(source);

    const bool haveSource = fs::exists(source, ec);
    const bool haveBinary = fs::exists(binary, ec);

    if (haveBinary && (!haveSource || fs::last_write_time(binary, ec) >= fs::last_write_time(source, ec))) {
        auto cached = loadBinary(binary);
        if (cached) return cached;
        log::warn("Ignoring bad mesh cache '{}': {}", binary.string(), cached.error());
    }
    if (!haveSource) return std::unexpected(std::format("mesh '{}' not found", relative.string()));

    auto mesh = loadObj(source);
    if (!mesh) return mesh;

    if (auto saved = saveBinary(binary, *mesh); !saved)
        log::warn("Could not cache '{}': {}", relative.string(), saved.error());
    else
        log::info("Cached {} ({} verts, {} tris)", binary.filename().string(),
                  mesh->vertices.size(), mesh->indices.size() / 3);
    return mesh;
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

void GpuMesh::swap(GpuMesh& o) noexcept {
    std::swap(vao_, o.vao_);
    std::swap(vbo_, o.vbo_);
    std::swap(ebo_, o.ebo_);
    std::swap(count_, o.count_);
    std::swap(boundsMin, o.boundsMin);
    std::swap(boundsMax, o.boundsMax);
}

bool GpuMesh::upload(const MeshData& mesh) {
    release();
    if (mesh.vertices.empty() || mesh.indices.empty()) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glBindVertexArray(0);

    count_    = static_cast<int>(mesh.indices.size());
    boundsMin = mesh.boundsMin;
    boundsMax = mesh.boundsMax;
    return true;
}

void GpuMesh::draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void GpuMesh::release() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    count_ = 0;
}

} // namespace carnage::gfx
