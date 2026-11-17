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
    glm::vec4 eye{0.0f};      // xyz = camera position
    glm::vec4 fog{0.0f};      // rgb = colour, w = distance it fades over (0 = off)
    glm::vec4 skyFog{0.0f};   // rgb = colour, w = view distance: hides the edge of the loaded world (0 = off)
    glm::vec4 time{1.0f, 0.0f, 0.0f, 0.0f};   // x = daylight (1 noon .. ~0.2 midnight): scales sky light
};


struct GpuMesh {
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;

    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;

    bool uploaded = false;
};


// One mesh to draw at a world position (see Renderer::drawBatch).
struct DrawItem {
    const GpuMesh* mesh = nullptr;
    glm::vec3 position{0.0f};
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
    GLuint _skyProgram = 0;
    GLuint _skyVao = 0;          // empty: the sky triangle comes from gl_VertexID
    GLint  _skyInverseLocation = -1;
    GLint  _skyZenithLocation = -1;
    GLint  _skyHorizonLocation = -1;
    GLint  _skySunLocation = -1;

    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ibo = 0;

    GLuint _cameraUBO = 0;
    GLuint _textureAtlas = 0;

    GLint _atlasRectsLocation = -1;
    GLint _highlightPassLocation = -1;
    GLint _boxSizeLocation = -1;
    // Multi-draw: one indirect command and one chunk position per draw.
    GLuint _indirectBuffer = 0;
    GLuint _instanceBuffer = 0;
    std::vector<std::array<std::uint32_t, 5>> _commands;   // DrawElementsIndirectCommand
    std::vector<glm::vec3> _positions;

    GLuint _boundsVao = 0;
    GLuint _boundsVbo = 0;
    GLuint _boundsIbo = 0;

    std::size_t _vertexBufferCapacity = 0;
    std::size_t _indexBufferCapacity = 0;

    std::size_t _vertexBufferUsed = 0;
    std::size_t _indexBufferUsed = 0;

    std::vector<std::pair<std::size_t, std::size_t>> _freeVertexRanges;
    std::vector<std::pair<std::size_t, std::size_t>> _freeIndexRanges;

    static constexpr std::size_t INITIAL_VERTEX_BUFFER_SIZE = 128 * 1024 * 1024;   // 8-byte vertices

    static constexpr std::size_t INITIAL_INDEX_BUFFER_SIZE = 64 * 1024 * 1024;

    static std::optional<std::string>
    loadShaderCode(const char* path);

    static GLuint compileShader(const std::string& code, GLenum type);
    static GLuint buildProgram(const char* vertexPath, const char* fragmentPath);

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

    // Sky gradient behind everything; call before drawing the world.
    // `inverseViewProj`: inverse of projection * camera rotation (no translation).
    void drawSky(const glm::mat4& inverseViewProj, const glm::vec3& zenith, const glm::vec3& horizon,
                 const glm::vec3& sunDirection) const;

    void uploadMesh(const MeshData& mesh, GpuMesh& gpuMesh);
    void releaseMesh(GpuMesh& gpuMesh);

    // Draws every item with ONE glMultiDrawElementsIndirect call, in order
    // (so a back-to-front sorted transparent batch stays sorted).
    void drawBatch(const std::vector<DrawItem>& items);
    void beginTransparentPass() const;
    void endTransparentPass() const;

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
