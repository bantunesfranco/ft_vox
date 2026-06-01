
#define GLAD_GL_IMPLEMENTATION 
#define GLFW_INCLUDE_NONE

#include "Engine.hpp"
#include "Renderer.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

Renderer::Renderer() : _shaderprog(0), _vao(0), _vbo(0), _ibo(0), _cameraUBO(0), _textureArray(0)
{
    std::string code = loadShaderCode(VSHADER_PATH).value_or("");
    const GLuint vshader = compileShader(code, GL_VERTEX_SHADER);

    code = loadShaderCode(FSHADER_PATH).value_or("");
    const GLuint fshader = compileShader(code, GL_FRAGMENT_SHADER);

    code = loadShaderCode(CSHADER_PATH).value_or("");
    const GLuint cshader = compileShader(code, GL_COMPUTE_SHADER);

    if (!vshader or !fshader or !cshader) {
        if (vshader) glDeleteShader(vshader);
        if (fshader) glDeleteShader(fshader);
        if (cshader) glDeleteShader(cshader);
        throw Engine::EngineException(VOX_FRAGFAIL);
    }

    _shaderprog = glCreateProgram();
    glAttachShader(_shaderprog, vshader);
    glAttachShader(_shaderprog, fshader);
    glLinkProgram(_shaderprog);

    glDeleteShader(vshader);
    glDeleteShader(fshader);

    GLint success;
    glGetProgramiv(_shaderprog, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infolog[512];
        glGetProgramInfoLog(_shaderprog, static_cast<GLsizei>(sizeof(infolog)), nullptr, infolog);
        std::cerr << "Shader Program Linking Failed: " << infolog << std::endl;
        glDeleteProgram(_shaderprog);
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    _vbo = VBOManager::get().getVBO();
    _ibo = VBOManager::get().getVBO();

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));

    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(
        2, 1, GL_UNSIGNED_INT,
        sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texIndex))
    );

    glBindVertexArray(0);

    glUseProgram(_shaderprog);
    const GLint samplerLoc = glGetUniformLocation(_shaderprog, "uTextures");
    if (samplerLoc != -1)
        glUniform1i(samplerLoc, 0);

    glUseProgram(0);

    if (glGetError() != GL_NO_ERROR)
        throw Engine::EngineException(VOX_GLADFAIL);
}

Renderer::~Renderer() {
    glDeleteProgram(_shaderprog);
    if (_computeProg) glDeleteProgram(_computeProg);
    glDeleteBuffers(1, &_ibo);
    glDeleteBuffers(1, &_vbo);
    glDeleteVertexArrays(1, &_vao);
}

GLuint Renderer::compileShader(const std::string& code, const int32_t type) {
    if (code.empty()) return 0;

    const GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    const char* cstr = code.c_str();
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(infolog)), nullptr, infolog);
        std::cerr << infolog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::optional<std::string> Renderer::loadShaderCode(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return std::nullopt;
    }

    std::stringstream shaderStream;
    shaderStream << file.rdbuf();
    file.close();

    std::string code = shaderStream.str();
    return code;
}

// void Renderer::dispatchCompute(GLuint groupsX, GLuint groupsY, GLuint groupsZ) const {
//     if (!_computeProg) return;
//     glUseProgram(_computeProg);
//     glDispatchCompute(groupsX, groupsY, groupsZ);
//     glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
// }

void Renderer::render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& mvp) const
{
    if (vertices.empty() || indices.empty())
        return;

    glUseProgram(_shaderprog);
    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STREAM_DRAW);

    glBindBuffer(GL_UNIFORM_BUFFER, _cameraUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4),glm::value_ptr(mvp));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindTextureUnit(0, _textureArray);
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
}

void Renderer::initProjectionMatrix(const GLFWwindow* window, const std::unique_ptr<Camera>& camera, glm::mat4& mvp) {
    int width, height;
    glfwGetFramebufferSize(const_cast<GLFWwindow*>(window), &width, &height);
    const float ratio = static_cast<float>(width) / static_cast<float>(height);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera->view = glm::lookAt(camera->pos, camera->pos + camera->dir, camera->up);
    camera->proj = glm::perspective(glm::radians(camera->fov), ratio, 0.1f, 256.0f);
    mvp = camera->proj * camera->view * glm::mat4(1.0f);
}

void Renderer::releaseVBO() {
    if (_vbo != 0) {
        VBOManager::get().returnVBO(_vbo);
        _vbo = 0;
    }
}

void Renderer::renderBoundingBox(const glm::vec3& minPos, const glm::vec3& maxPos) {
    // Use lines or wireframe to represent the bounding box
    const GLfloat vertices[] = {
        minPos.x, minPos.y, minPos.z,
        maxPos.x, minPos.y, minPos.z,
        maxPos.x, maxPos.y, minPos.z,
        minPos.x, maxPos.y, minPos.z,
        minPos.x, minPos.y, maxPos.z,
        maxPos.x, minPos.y, maxPos.z,
        maxPos.x, maxPos.y, maxPos.z,
        minPos.x, maxPos.y, maxPos.z
    };

    const GLuint indices[] = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    };

    if (_vbo == 0)
        _vbo = VBOManager::get().getVBO();

    if (_ibo == 0)
        _ibo = VBOManager::get().getVBO();

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
}
