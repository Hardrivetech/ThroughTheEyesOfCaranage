#pragma once
#include <glm/glm.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace carnage::gfx {

// A linked vertex+fragment program built from files under assets/shaders/.
// `#include "file.glsl"` is expanded by the loader. reload() keeps the old
// program if the new source fails to compile. Uniform setters bind nothing
// (they use glProgramUniform*) and silently ignore unknown names.
class Shader {
public:
    Shader() = default;
    ~Shader() { release(); }
    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept { swap(o); }
    Shader& operator=(Shader&& o) noexcept {
        if (this != &o) { release(); swap(o); }
        return *this;
    }

    bool load(std::string vertexFile, std::string fragmentFile);
    bool reload();
    void release();

    void use() const;
    bool valid() const { return program_ != 0; }

    void set(std::string_view name, int v) const;
    void set(std::string_view name, float v) const;
    void set(std::string_view name, const glm::vec2& v) const;
    void set(std::string_view name, const glm::vec3& v) const;
    void set(std::string_view name, const glm::vec4& v) const;
    void set(std::string_view name, const glm::mat3& v) const;
    void set(std::string_view name, const glm::mat4& v) const;
    void setArray(std::string_view name, std::span<const glm::vec3> v) const;
    void setArray(std::string_view name, std::span<const glm::vec4> v) const;

private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    void swap(Shader& o) noexcept {
        std::swap(program_, o.program_);
        std::swap(vert_, o.vert_);
        std::swap(frag_, o.frag_);
        std::swap(cache_, o.cache_);
    }
    int location(std::string_view name) const;

    unsigned int program_ = 0;
    std::string  vert_, frag_;
    mutable std::unordered_map<std::string, int, StringHash, std::equal_to<>> cache_;
};

} // namespace carnage::gfx
