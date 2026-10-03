#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Mesh.hpp"

class Camera;

struct WorldUBO {
    glm::mat4 MVP{1.0f};
    glm::vec4 light{0.0f};
    glm::vec4 ambientData{0.5f, 0.0f, 0.0f, 0.0f};
};


struct GpuMesh {
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;

    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;

    bool uploaded = false;
};


class GpuStorage
{
public:
    GpuStorage();
    ~GpuStorage();

    GpuStorage(const GpuStorage&) = delete;
    GpuStorage& operator=(const GpuStorage&) = delete;

    void upload(GpuMesh& gpuMesh, const MeshData& mesh);

    void destroy(GpuMesh& gpuMesh);

private:
    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ibo = 0;
};


class Renderer {
private:
    GLuint _shaderprog = 0;

    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ibo = 0;

    GLuint _cameraUBO = 0;
    GLuint _textureAtlas = 0;

    GLint _chunkPositionLocation = -1;
    GLint _atlasRectsLocation = -1;

    std::size_t _vertexBufferCapacity = 0;
    std::size_t _indexBufferCapacity = 0;

    std::size_t _vertexBufferUsed = 0;
    std::size_t _indexBufferUsed = 0;

    std::vector<std::pair<std::size_t, std::size_t>> _freeVertexRanges;
    std::vector<std::pair<std::size_t, std::size_t>> _freeIndexRanges;

    static constexpr std::size_t INITIAL_VERTEX_BUFFER_SIZE = 16 * 1024 * 1024;

    static constexpr std::size_t INITIAL_INDEX_BUFFER_SIZE = 8 * 1024 * 1024;

    static std::optional<std::string>
    loadShaderCode(const char* path);

    static GLuint compileShader(const std::string& code, GLenum type);

    void createBuffers();
    void createVertexLayout();
    void createCameraUBO();
    
    void ensureVertexCapacity(std::size_t required, std::size_t bytesToPreserve);
    void ensureIndexCapacity(std::size_t required, std::size_t bytesToPreserve);
    static std::size_t allocateRange(
        std::vector<std::pair<std::size_t, std::size_t>>& freeRanges,
        std::size_t& used,
        std::size_t size
    );
    static void freeRange(
        std::vector<std::pair<std::size_t, std::size_t>>& freeRanges,
        std::size_t& used,
        std::size_t offset,
        std::size_t size
    );

public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    static void initProjectionMatrix(
        const GLFWwindow* window,
        const std::unique_ptr<Camera>& camera,
        glm::mat4& mvp
    );

    void beginFrame(const WorldUBO& worldUbo) const;

    void uploadMesh(const MeshData& mesh, GpuMesh& gpuMesh);
    void releaseMesh(GpuMesh& gpuMesh);

    void renderMesh(const GpuMesh& gpuMesh, const glm::vec3& worldPositio) const;

    void setTextureAtlas(GLuint textureAtlas) noexcept;
    void setAtlasRects(const std::array<glm::vec4, 64>& rects);

    void renderBoundingBox(const glm::vec3& minPos, const glm::vec3& maxPos);

    [[nodiscard]]
    GLuint getShaderProgram() const noexcept {
        return _shaderprog;
    }

    [[nodiscard]]
    GLuint getVertexArrayObject() const noexcept {
        return _vao;
    }

    [[nodiscard]]
    GLuint getCameraUBO() const noexcept {
        return _cameraUBO;
    }

    [[nodiscard]]
    GLuint getTextureAtlas() const noexcept {
        return _textureAtlas;
    }
};

#endif
