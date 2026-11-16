#define GLFW_INCLUDE_NONE

#include "Engine.hpp"
#include "Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include <glm/gtc/type_ptr.hpp>

const auto VSHADER_PATH = "./engine/shaders/vertex.glsl";
const auto FSHADER_PATH = "./engine/shaders/fragment.glsl";

Renderer::Renderer() {
    const std::string vertexCode = loadShaderCode(VSHADER_PATH).value_or("");
    const std::string fragmentCode = loadShaderCode(FSHADER_PATH).value_or("");

    const GLuint vertexShader = compileShader(vertexCode, GL_VERTEX_SHADER);
    const GLuint fragmentShader = compileShader(fragmentCode, GL_FRAGMENT_SHADER);

    if (!vertexShader || !fragmentShader) {
        if (vertexShader)
            glDeleteShader(vertexShader);

        if (fragmentShader)
            glDeleteShader(fragmentShader);

        throw Engine::EngineException(VOX_FRAGFAIL);
    }

    _shaderprog = glCreateProgram();

    if (!_shaderprog) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glAttachShader(_shaderprog, vertexShader);
    glAttachShader(_shaderprog, fragmentShader);

    glLinkProgram(_shaderprog);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;

    glGetProgramiv(_shaderprog, GL_LINK_STATUS, &success);

    if (!success) {
        char infoLog[1024]{};

        glGetProgramInfoLog(_shaderprog, sizeof(infoLog), nullptr, infoLog);

        std::cerr << "Shader Program Linking Failed: " << infoLog << std::endl;

        glDeleteProgram(_shaderprog);
        _shaderprog = 0;

        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    createBuffers();
    createVertexLayout();
    createCameraUBO();

    glUseProgram(_shaderprog);

    const GLint samplerLoc = glGetUniformLocation(_shaderprog, "uAtlas");

    if (samplerLoc >= 0)
        glUniform1i(samplerLoc, 0);

    _atlasRectsLocation = glGetUniformLocation(_shaderprog, "uTextureRects");
    _highlightPassLocation = glGetUniformLocation(_shaderprog, "uHighlightPass");
    _boxSizeLocation = glGetUniformLocation(_shaderprog, "uBoxSize");

    glUseProgram(0);

    if (glGetError() != GL_NO_ERROR)
        throw Engine::EngineException(VOX_GLADFAIL);
}


Renderer::~Renderer() {
    if (_boundsIbo)
        glDeleteBuffers(1, &_boundsIbo);
    if (_boundsVbo)
        glDeleteBuffers(1, &_boundsVbo);
    if (_boundsVao)
        glDeleteVertexArrays(1, &_boundsVao);
    if (_cameraUBO)
        glDeleteBuffers(1, &_cameraUBO);
    if (_indirectBuffer)
        glDeleteBuffers(1, &_indirectBuffer);
    if (_instanceBuffer)
        glDeleteBuffers(1, &_instanceBuffer);

    if (_ibo)
        glDeleteBuffers(1, &_ibo);

    if (_vbo)
        glDeleteBuffers(1, &_vbo);

    if (_vao)
        glDeleteVertexArrays(1, &_vao);

    if (_shaderprog)
        glDeleteProgram(_shaderprog);
}


void Renderer::createBuffers() {
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);
    glGenBuffers(1, &_indirectBuffer);
    glGenBuffers(1, &_instanceBuffer);

    if (!_vao || !_vbo || !_ibo || !_indirectBuffer || !_instanceBuffer)
        throw Engine::EngineException(VOX_GLADFAIL);

    _vertexBufferCapacity = INITIAL_VERTEX_BUFFER_SIZE;
    _indexBufferCapacity = INITIAL_INDEX_BUFFER_SIZE;

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_vertexBufferCapacity),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_indexBufferCapacity),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    _vertexBufferUsed = 0;
    _indexBufferUsed = 0;
}


void Renderer::createVertexLayout() {
    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    /*
     * PackedVertex == uint32_t.
     * The shader receives the complete 32-bit value and unpacks it.
     */
    glEnableVertexAttribArray(0);

    glVertexAttribIPointer(
        0,
        1,
        GL_UNSIGNED_INT,
        sizeof(PackedVertex),
        nullptr
    );

    // Chunk position, one per draw: divisor 1 + baseInstance = draw index.
    glBindBuffer(GL_ARRAY_BUFFER, _instanceBuffer);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glVertexAttribDivisor(1, 1);

    glBindVertexArray(0);
}


void Renderer::createCameraUBO() {
    glGenBuffers(1, &_cameraUBO);

    glBindBuffer(
        GL_UNIFORM_BUFFER,
        _cameraUBO
    );

    glBufferData(
        GL_UNIFORM_BUFFER,
        sizeof(WorldUBO),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glBindBufferBase(
        GL_UNIFORM_BUFFER,
        0,
        _cameraUBO
    );

    glBindBuffer(
        GL_UNIFORM_BUFFER,
        0
    );
}

void Renderer::ensureVertexCapacity(const std::size_t required, const std::size_t bytesToPreserve) {
    if (required <= _vertexBufferCapacity)
        return;

    std::size_t newCapacity = _vertexBufferCapacity;

    while (newCapacity < required)
        newCapacity *= 2;

    GLuint newBuffer = 0;
    glGenBuffers(1, &newBuffer);

    glBindBuffer(GL_COPY_WRITE_BUFFER, newBuffer);

    glBufferData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLsizeiptr>(newCapacity),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_COPY_READ_BUFFER, _vbo);

    if (bytesToPreserve > 0) {
        glCopyBufferSubData(
            GL_COPY_READ_BUFFER,
            GL_COPY_WRITE_BUFFER,
            0,
            0,
            static_cast<GLsizeiptr>(bytesToPreserve)
        );
    }

    glDeleteBuffers(1, &_vbo);

    _vbo = newBuffer;
    _vertexBufferCapacity = newCapacity;

    /*
     * The VAO stores the vertex-buffer binding.
     * Rebind the new VBO to the vertex attribute layout.
     */
    createVertexLayout();
}

void Renderer::ensureIndexCapacity(const std::size_t required, const std::size_t bytesToPreserve) {
    if (required <= _indexBufferCapacity)
        return;

    std::size_t newCapacity = _indexBufferCapacity;

    while (newCapacity < required)
        newCapacity *= 2;

    GLuint newBuffer = 0;
    glGenBuffers(1, &newBuffer);

    glBindBuffer(GL_COPY_WRITE_BUFFER, newBuffer);

    glBufferData(
        GL_COPY_WRITE_BUFFER,
        static_cast<GLsizeiptr>(newCapacity),
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_COPY_READ_BUFFER, _ibo);

    if (bytesToPreserve > 0) {
        glCopyBufferSubData(
            GL_COPY_READ_BUFFER,
            GL_COPY_WRITE_BUFFER,
            0,
            0,
            static_cast<GLsizeiptr>(bytesToPreserve)
        );
    }

    glDeleteBuffers(1, &_ibo);

    _ibo = newBuffer;
    _indexBufferCapacity = newCapacity;

    createVertexLayout();
}

GLuint Renderer::compileShader(const std::string& code, const GLenum type) {
    if (code.empty())
        return 0;

    const GLuint shader = glCreateShader(type);

    if (!shader)
        return 0;

    const char* source = code.c_str();

    glShaderSource(shader, 1, &source, nullptr);

    glCompileShader(shader);

    GLint success = GL_FALSE;

    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        char infoLog[1024]{};

        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);

        std::cerr << "Shader compilation failed: " << infoLog << std::endl;

        glDeleteShader(shader);

        return 0;
    }

    return shader;
}

std::optional<std::string> Renderer::loadShaderCode(const char* path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path
                  << " (run the game from the project root)" << std::endl;

        return std::nullopt;
    }

    std::stringstream stream;
    stream << file.rdbuf();

    return stream.str();
}

void Renderer::beginFrame(const WorldUBO& worldUbo) const
{
    glBindBuffer(
        GL_UNIFORM_BUFFER,
        _cameraUBO
    );

    glBufferSubData(
        GL_UNIFORM_BUFFER,
        0,
        sizeof(WorldUBO),
        &worldUbo
    );

    glBindBuffer(
        GL_UNIFORM_BUFFER,
        0
    );
}

void Renderer::setTextureAtlas(const GLuint textureAtlas) noexcept {
    _textureAtlas = textureAtlas;
}

void Renderer::setAtlasRects(const std::array<glm::vec4, 64>& rects) {
    if (_atlasRectsLocation < 0)
        return;

    glUseProgram(_shaderprog);
    glUniform4fv(_atlasRectsLocation, static_cast<GLsizei>(rects.size()), glm::value_ptr(rects[0]));
    glUseProgram(0);
}

std::size_t Renderer::allocateRange(
    std::vector<std::pair<std::size_t, std::size_t>>& freeRanges,
    std::size_t& used,
    const std::size_t size
) {
    for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it) {
        if (it->second < size)
            continue;

        const std::size_t offset = it->first;
        it->first += size;
        it->second -= size;
        if (it->second == 0)
            freeRanges.erase(it);
        return offset;
    }

    const std::size_t offset = used;
    used += size;
    return offset;
}

void Renderer::freeRange(
    std::vector<std::pair<std::size_t, std::size_t>>& freeRanges,
    std::size_t& used,
    const std::size_t offset,
    const std::size_t size
) {
    if (size == 0)
        return;

    auto it = std::lower_bound(
        freeRanges.begin(),
        freeRanges.end(),
        offset,
        [](const auto& range, const std::size_t value) { return range.first < value; }
    );
    it = freeRanges.insert(it, {offset, size});

    if (it != freeRanges.begin()) {
        auto previous = std::prev(it);
        if (previous->first + previous->second == it->first) {
            previous->second += it->second;
            it = freeRanges.erase(it);
            it = previous;
        }
    }

    if (std::next(it) != freeRanges.end() && it->first + it->second == std::next(it)->first) {
        it->second += std::next(it)->second;
        freeRanges.erase(std::next(it));
    }

    while (!freeRanges.empty() && freeRanges.back().first + freeRanges.back().second == used) {
        used = freeRanges.back().first;
        freeRanges.pop_back();
    }
}

void Renderer::uploadMesh(const MeshData& mesh, GpuMesh& gpuMesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        gpuMesh = {};
        return;
    }

    const std::size_t vertexBytes = mesh.vertices.size() * sizeof(PackedVertex);
    const std::size_t indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    const std::size_t vertexBytesToPreserve = _vertexBufferUsed;
    const std::size_t indexBytesToPreserve = _indexBufferUsed;

    const std::size_t vertexOffsetBytes = allocateRange(_freeVertexRanges, _vertexBufferUsed, vertexBytes);
    const std::size_t indexOffsetBytes = allocateRange(_freeIndexRanges, _indexBufferUsed, indexBytes);

    ensureVertexCapacity(vertexOffsetBytes + vertexBytes, vertexBytesToPreserve);
    ensureIndexCapacity(indexOffsetBytes + indexBytes, indexBytesToPreserve);

    /* Upload vertices */
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    glBufferSubData(
        GL_ARRAY_BUFFER,
        static_cast<GLintptr>(vertexOffsetBytes),
        static_cast<GLsizeiptr>(vertexBytes),
        mesh.vertices.data()
    );

    /* Upload indices */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLintptr>(indexOffsetBytes),
        static_cast<GLsizeiptr>(indexBytes),
        mesh.indices.data()
    );

    /* Store offsets in units of elements, not bytes */
    gpuMesh.vertexOffset = static_cast<std::uint32_t>(vertexOffsetBytes / sizeof(PackedVertex));
    gpuMesh.indexOffset = static_cast<std::uint32_t>(indexOffsetBytes / sizeof(std::uint32_t));

    gpuMesh.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    gpuMesh.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

    gpuMesh.uploaded = true;

}

void Renderer::releaseMesh(GpuMesh& gpuMesh) {
    if (!gpuMesh.uploaded)
        return;

    freeRange(
        _freeVertexRanges,
        _vertexBufferUsed,
        static_cast<std::size_t>(gpuMesh.vertexOffset) * sizeof(PackedVertex),
        static_cast<std::size_t>(gpuMesh.vertexCount) * sizeof(PackedVertex)
    );
    freeRange(
        _freeIndexRanges,
        _indexBufferUsed,
        static_cast<std::size_t>(gpuMesh.indexOffset) * sizeof(std::uint32_t),
        static_cast<std::size_t>(gpuMesh.indexCount) * sizeof(std::uint32_t)
    );

    gpuMesh = {};
}

void Renderer::drawBatch(const std::vector<DrawItem>& items) {
    _commands.clear();
    _positions.clear();
    for (const DrawItem& item : items) {
        const GpuMesh& mesh = *item.mesh;
        if (!mesh.uploaded || mesh.indexCount == 0)
            continue;
        // {count, instanceCount, firstIndex, baseVertex, baseInstance}
        _commands.push_back({mesh.indexCount, 1u, mesh.indexOffset, mesh.vertexOffset,
                             static_cast<std::uint32_t>(_positions.size())});
        _positions.push_back(item.position);
    }
    if (_commands.empty())
        return;

    // Orphan + refill: the driver hands out fresh storage, no stall on the GPU
    // still reading last frame's (or the previous batch's) data.
    glNamedBufferData(_instanceBuffer, static_cast<GLsizeiptr>(_positions.size() * sizeof(glm::vec3)),
                      _positions.data(), GL_STREAM_DRAW);
    glNamedBufferData(_indirectBuffer, static_cast<GLsizeiptr>(_commands.size() * sizeof(_commands[0])),
                      _commands.data(), GL_STREAM_DRAW);

    glUseProgram(_shaderprog);
    glBindVertexArray(_vao);
    if (_highlightPassLocation >= 0)
        glUniform1i(_highlightPassLocation, GL_FALSE);
    glBindTextureUnit(0, _textureAtlas);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, _indirectBuffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(_commands.size()), 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

// Outline of an axis-aligned box of any (fractional) size: a unit cube,
// uploaded once, scaled by uBoxSize in the vertex shader.
void Renderer::renderBoundingBox(const glm::vec3& minPos, const glm::vec3& maxPos) {
    static constexpr std::array<std::uint32_t, 24> indices = {{
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    }};

    if (!_boundsVao) {
        static constexpr std::array<PackedVertex, 8> vertices = {{
            packVertex(0, 0, 0, NormalDir::PositiveY, 0, 3, 0),
            packVertex(1, 0, 0, NormalDir::PositiveY, 0, 3, 0),
            packVertex(1, 1, 0, NormalDir::PositiveY, 0, 3, 0),
            packVertex(0, 1, 0, NormalDir::PositiveY, 0, 3, 0),
            packVertex(0, 0, 1, NormalDir::PositiveY, 0, 3, 0),
            packVertex(1, 0, 1, NormalDir::PositiveY, 0, 3, 0),
            packVertex(1, 1, 1, NormalDir::PositiveY, 0, 3, 0),
            packVertex(0, 1, 1, NormalDir::PositiveY, 0, 3, 0)
        }};

        glGenVertexArrays(1, &_boundsVao);
        glGenBuffers(1, &_boundsVbo);
        glGenBuffers(1, &_boundsIbo);

        glBindVertexArray(_boundsVao);
        glBindBuffer(GL_ARRAY_BUFFER, _boundsVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _boundsIbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(PackedVertex), nullptr);
    }

    const glm::vec3 size = glm::max(maxPos - minPos, glm::vec3(0.01f));

    glBindVertexArray(_boundsVao);
    glUseProgram(_shaderprog);
    glVertexAttrib3fv(1, glm::value_ptr(minPos));   // attribute 1 has no array in this VAO
    if (_boxSizeLocation >= 0)
        glUniform3fv(_boxSizeLocation, 1, glm::value_ptr(size));
    if (_highlightPassLocation >= 0)
        glUniform1i(_highlightPassLocation, GL_TRUE);
    glBindTextureUnit(0, _textureAtlas);
    glDepthMask(GL_FALSE);
    glLineWidth(2.0f);
    glDrawElements(GL_LINES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    if (_highlightPassLocation >= 0)
        glUniform1i(_highlightPassLocation, GL_FALSE);
    glBindVertexArray(0);
}

void Renderer::beginTransparentPass() const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
}

void Renderer::endTransparentPass() const {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::initProjectionMatrix(
    const GLFWwindow* window,
    const std::unique_ptr<Camera>& camera,
    glm::mat4& mvp
) {
    int width = 0;
    int height = 0;

    glfwGetFramebufferSize(
        const_cast<GLFWwindow*>(window),
        &width,
        &height
    );

    if (width <= 0 || height <= 0)
        return;

    const float ratio = static_cast<float>(width) / static_cast<float>(height);

    // The caller clears at the start of the frame; clearing here too would
    // just repeat it.
    glViewport(
        0,
        0,
        width,
        height
    );

    camera->view =
        glm::lookAt(
            camera->pos,
            camera->pos + camera->dir,
            camera->up
        );

    camera->proj =
        glm::perspective(
            glm::radians(camera->fov),
            ratio,
            0.1f,
            512.0f
        );

    mvp = camera->proj * camera->view;
}
