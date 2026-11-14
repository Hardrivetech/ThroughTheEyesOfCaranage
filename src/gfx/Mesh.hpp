#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace carnage::gfx {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};
static_assert(sizeof(Vertex) == 32, "Vertex is written to .cmesh files verbatim");

struct MeshData {
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;  // triangle list
    glm::vec3                  boundsMin{0.0f};
    glm::vec3                  boundsMax{0.0f};

    void computeBounds();
};

// Wavefront OBJ: v / vt / vn / f (any polygon, fan-triangulated, negative
// indices allowed). Missing normals are generated as smooth, area-weighted
// vertex normals. Materials, groups and smoothing groups are ignored.
std::expected<MeshData, std::string> parseObj(std::string_view text);
std::expected<MeshData, std::string> loadObj(const std::filesystem::path& file);

// Custom binary format (.cmesh): 40-byte header + raw vertices + indices.
std::expected<void, std::string>     saveBinary(const std::filesystem::path& file, const MeshData& mesh);
std::expected<MeshData, std::string> loadBinary(const std::filesystem::path& file);

// `relative` is under the asset root. Uses "<name>.cmesh" next to the .obj
// when it is at least as new as the source, otherwise parses the OBJ and
// refreshes the cache.
std::expected<MeshData, std::string> loadMeshCached(const std::filesystem::path& relative);

// Static indexed mesh on the GPU (VAO + VBO + EBO). Attributes:
// 0 = position, 1 = normal, 2 = uv. Move-only; release() before the GL
// context is destroyed.
class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh() { release(); }
    GpuMesh(const GpuMesh&)            = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& o) noexcept { swap(o); }
    GpuMesh& operator=(GpuMesh&& o) noexcept {
        if (this != &o) { release(); swap(o); }
        return *this;
    }

    bool upload(const MeshData& mesh);
    void draw() const;
    void release();
    bool valid() const { return vao_ != 0; }

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

private:
    void swap(GpuMesh& o) noexcept;

    unsigned int vao_ = 0, vbo_ = 0, ebo_ = 0;
    int          count_ = 0;
};

} // namespace carnage::gfx
