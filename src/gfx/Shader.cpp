#include "gfx/Shader.hpp"
#include "core/Log.hpp"
#include "gfx/Assets.hpp"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <expected>
#include <filesystem>
#include <format>

namespace carnage::gfx {

namespace {

constexpr int kMaxIncludeDepth = 8;

std::expected<std::string, std::string> expand(const std::filesystem::path& file, int depth) {
    if (depth > kMaxIncludeDepth)
        return std::unexpected(std::format("#include nesting too deep at '{}'", file.string()));

    auto source = assets::readText(std::filesystem::path("shaders") / file);
    if (!source) return std::unexpected(source.error());

    std::string      out;
    std::string_view rest = *source;
    out.reserve(source->size());

    while (!rest.empty()) {
        const std::size_t nl   = rest.find('\n');
        std::string_view  line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);

        std::string_view trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.remove_prefix(1);

        if (trimmed.starts_with("#include")) {
            const std::size_t a = trimmed.find('"');
            const std::size_t b = a == std::string_view::npos ? a : trimmed.find('"', a + 1);
            if (b == std::string_view::npos)
                return std::unexpected(std::format("malformed #include in '{}'", file.string()));
            auto included = expand(std::filesystem::path(std::string(trimmed.substr(a + 1, b - a - 1))),
                                   depth + 1);
            if (!included) return std::unexpected(included.error());
            out += *included;
            out += '\n';
        } else {
            out += line;
            out += '\n';
        }
    }
    return out;
}

std::expected<GLuint, std::string> compileStage(GLenum type, const std::string& source,
                                                const std::string& label) {
    const GLuint shader = glCreateShader(type);
    const char*  text   = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetShaderInfoLog(shader, sizeof buf, nullptr, buf);
        glDeleteShader(shader);
        return std::unexpected(std::format("{} failed to compile:\n{}", label, buf));
    }
    return shader;
}

std::expected<GLuint, std::string> buildProgram(const std::string& vs, const std::string& fs,
                                                const std::string& vname, const std::string& fname) {
    auto v = compileStage(GL_VERTEX_SHADER, vs, vname);
    if (!v) return std::unexpected(v.error());
    auto f = compileStage(GL_FRAGMENT_SHADER, fs, fname);
    if (!f) {
        glDeleteShader(*v);
        return std::unexpected(f.error());
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, *v);
    glAttachShader(program, *f);
    glLinkProgram(program);
    glDeleteShader(*v);
    glDeleteShader(*f);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetProgramInfoLog(program, sizeof buf, nullptr, buf);
        glDeleteProgram(program);
        return std::unexpected(std::format("{} + {} failed to link:\n{}", vname, fname, buf));
    }
    return program;
}

} // namespace

bool Shader::load(std::string vertexFile, std::string fragmentFile) {
    vert_ = std::move(vertexFile);
    frag_ = std::move(fragmentFile);
    return reload();
}

bool Shader::reload() {
    auto vs = expand(vert_, 0);
    if (!vs) { log::error("Shader '{}': {}", vert_, vs.error()); return false; }
    auto fs = expand(frag_, 0);
    if (!fs) { log::error("Shader '{}': {}", frag_, fs.error()); return false; }

    auto program = buildProgram(*vs, *fs, vert_, frag_);
    if (!program) {
        log::error("{}", program.error());
        return false;
    }
    release();
    program_ = *program;
    return true;
}

void Shader::release() {
    if (program_) glDeleteProgram(program_);
    program_ = 0;
    cache_.clear();
}

void Shader::use() const { glUseProgram(program_); }

int Shader::location(std::string_view name) const {
    if (const auto it = cache_.find(name); it != cache_.end()) return it->second;
    const int loc = glGetUniformLocation(program_, std::string(name).c_str());
    cache_.emplace(std::string(name), loc);
    return loc;
}

void Shader::set(std::string_view n, int v) const   { glProgramUniform1i(program_, location(n), v); }
void Shader::set(std::string_view n, float v) const { glProgramUniform1f(program_, location(n), v); }
void Shader::set(std::string_view n, const glm::vec2& v) const { glProgramUniform2fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(std::string_view n, const glm::vec3& v) const { glProgramUniform3fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(std::string_view n, const glm::vec4& v) const { glProgramUniform4fv(program_, location(n), 1, glm::value_ptr(v)); }
void Shader::set(std::string_view n, const glm::mat3& v) const { glProgramUniformMatrix3fv(program_, location(n), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::set(std::string_view n, const glm::mat4& v) const { glProgramUniformMatrix4fv(program_, location(n), 1, GL_FALSE, glm::value_ptr(v)); }

void Shader::setArray(std::string_view n, std::span<const glm::vec3> v) const {
    if (!v.empty()) glProgramUniform3fv(program_, location(n), static_cast<GLsizei>(v.size()), glm::value_ptr(v.front()));
}
void Shader::setArray(std::string_view n, std::span<const glm::vec4> v) const {
    if (!v.empty()) glProgramUniform4fv(program_, location(n), static_cast<GLsizei>(v.size()), glm::value_ptr(v.front()));
}

} // namespace carnage::gfx
