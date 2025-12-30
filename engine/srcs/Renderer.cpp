
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

Renderer::Renderer() : _shaderprog(0), _vao(0), _vbo(0), _ibo(0), _mvpLocation(-1), textureID(0) {
    const std::string *code = loadShaderCode(VSHADER_PATH);
    const GLuint vshader = compileShader(code, GL_VERTEX_SHADER);
    delete code;
    if (!vshader) throw Engine::EngineException(VOX_VERTFAIL);

    code = loadShaderCode(FSHADER_PATH);
    const GLuint fshader = compileShader(code, GL_FRAGMENT_SHADER);
    delete code;
    if (!fshader)
    {
        glDeleteShader(vshader);
        throw Engine::EngineException(VOX_FRAGFAIL);
    }

    _shaderprog = glCreateProgram();
    if (!_shaderprog)
    {
        glDeleteShader(vshader);
        glDeleteShader(fshader);
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glAttachShader(_shaderprog, vshader);
    glAttachShader(_shaderprog, fshader);
    glLinkProgram(_shaderprog);

    GLint success;
    glGetProgramiv(_shaderprog, GL_LINK_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetProgramInfoLog(_shaderprog, sizeof(infolog), NULL, infolog);
        std::cerr << "Shader Program Linking Failed: " << infolog << std::endl;
        glDeleteShader(vshader);
        glDeleteShader(fshader);
        glDeleteProgram(_shaderprog);
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glDeleteShader(vshader);
    glDeleteShader(fshader);

    _mvpLocation = glGetUniformLocation(_shaderprog, "MVP");
    if (_mvpLocation == -1) {
        std::cerr << "Failed to get MVP uniform location." << std::endl;
        glDeleteProgram(_shaderprog);
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    const GLint vpos_location = glGetAttribLocation(_shaderprog, "vPos");
    const GLint vtex_location = glGetAttribLocation(_shaderprog, "vTexCoord");

    if (vpos_location == -1 || vtex_location == -1) {
        std::cerr << "Failed to get attribute locations." << std::endl;
        glDeleteProgram(_shaderprog);
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    _vbo = VBOManager::get().getVBO();
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    _ibo = VBOManager::get().getVBO();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(vtex_location);
    glVertexAttribPointer(vtex_location, 2, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoords)));

    glBindVertexArray(0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteProgram(_shaderprog);
        glDeleteBuffers(1, &_vbo);
        glDeleteBuffers(1, &_ibo);
        glDeleteVertexArrays(1, &_vao);
        throw Engine::EngineException(VOX_GLADFAIL);
    }

}

Renderer::~Renderer() {
    glDeleteProgram(_shaderprog);
    glDeleteBuffers(1, &_ibo);
    glDeleteVertexArrays(1, &_vao);
}

GLuint Renderer::compileShader(const std::string* code, int32_t type) {
    if (!code) return 0;

    const GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    const char* cstr = code->c_str();
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetShaderInfoLog(shader, sizeof(infolog), nullptr, infolog);
        std::cerr << infolog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::string* Renderer::loadShaderCode(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return nullptr;
    }

    std::stringstream shaderStream;
    shaderStream << file.rdbuf();
    file.close();

    return new std::string(shaderStream.str());
}

void Renderer::render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& mvp) {
    if (vertices.empty() || indices.empty()) {
        std::cerr << "No vertices or indices to render." << std::endl;
        return;
    }

    glUseProgram(_shaderprog);
    glBindVertexArray(_vao);

    if (_vbo == 0)
        _vbo = VBOManager::get().getVBO();

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    if (_mvpLocation == -1) {
        std::cerr << "Failed to get MVP uniform location." << std::endl;
        return;
    }
    glUniformMatrix4fv(_mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));

    const GLint texLoc = glGetUniformLocation(_shaderprog, "textureSampler");
    if (texLoc == -1) {
        std::cerr << "Failed to get textureSampler uniform location." << std::endl;
        return;
    }
    glUniform1i(texLoc, 0);


    GLuint currentTextureID = vertices[0].textureID;
    for (size_t i = 0; i < indices.size(); i += 6) {
        textureID = vertices[indices[i]].textureID;

        if (textureID != currentTextureID) {
            currentTextureID = textureID;
            glBindTexture(GL_TEXTURE_2D, currentTextureID);
        }

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, reinterpret_cast<void*>(i * sizeof(uint32_t)));
    }

    glBindVertexArray(0);
}

void Renderer::initProjectionMatrix(const GLFWwindow* window, const std::unique_ptr<Camera>& camera, glm::mat4& mvp) {
    int width, height;
    glfwGetFramebufferSize(const_cast<GLFWwindow*>(window), &width, &height);
    const float ratio = static_cast<float>(width) / static_cast<float>(height);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera->view = glm::lookAt(camera->pos, camera->pos + camera->dir, camera->up);
    camera->proj = glm::perspective(glm::radians(camera->fov), ratio, 0.1f, 100.0f);
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_DYNAMIC_DRAW);

    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
}
