
#define GLAD_GL_IMPLEMENTATION 
#define GLFW_INCLUDE_NONE

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Engine.hpp"
#include "Renderer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <unistd.h>



Renderer::Renderer() : _shaderprog(0), _vao(0), _vbo(0), _ibo(0), _mvpLocation(-1), _textureID(0) {
    std::string *code = loadShaderCode(VSHADER_PATH);
    GLuint vshader = compileShader(code, GL_VERTEX_SHADER);
    delete code;
    if (!vshader) throw Engine::EngineException(VOX_VERTFAIL);

    code = loadShaderCode(FSHADER_PATH);
    GLuint fshader = compileShader(code, GL_FRAGMENT_SHADER);
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
    const GLint vcol_location = glGetAttribLocation(_shaderprog, "vCol");
    const GLint vtex_location = glGetAttribLocation(_shaderprog, "vTexCoord");

    if (vpos_location == -1 || vcol_location == -1 || vtex_location == -1) {
        std::cerr << "Failed to get attribute locations." << std::endl;
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);

    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    glEnableVertexAttribArray(vcol_location);
    glVertexAttribPointer(vcol_location, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glEnableVertexAttribArray(vtex_location);
    glVertexAttribPointer(vtex_location, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

    glBindVertexArray(0);
}

Renderer::~Renderer() {
    glDeleteProgram(_shaderprog);
    glDeleteBuffers(1, &_vbo);
    glDeleteBuffers(1, &_ibo);
    glDeleteVertexArrays(1, &_vao);
    if (_textureID) {
        glDeleteTextures(1, &_textureID);
    }
}

GLuint Renderer::compileShader(const std::string* code, int32_t type) {
    if (!code) return 0;

    GLuint shader = glCreateShader(type);
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

void Renderer::render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4 *mvp) {
    if (vertices.empty() || indices.empty()) {
        std::cerr << "No vertices or indices to render." << std::endl;
        return;
    }

    glUseProgram(_shaderprog);
    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);


    // Bind texture
    if (_textureID) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _textureID);
    }

    GLint texLoc = glGetUniformLocation(_shaderprog, "textureSampler");
    if (texLoc == -1) {
        std::cerr << "Failed to get textureSampler uniform location." << std::endl;
    } else {
        glUniform1i(texLoc, 0);
    }

    if (_mvpLocation == -1) {
        std::cerr << "Failed to get MVP uniform location." << std::endl;
    } else {
        glUniformMatrix4fv(_mvpLocation, 1, GL_FALSE, glm::value_ptr(*mvp));
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, NULL);

    glBindVertexArray(0);
}


void Renderer::initProjectionMatrix(GLFWwindow* window, Camera* camera, glm::mat4* mvp) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    const float ratio = static_cast<float>(width) / static_cast<float>(height);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 m = glm::mat4(1.0f);
    glm::vec3 target = camera->pos + camera->dir;
    glm::mat4 v = glm::lookAt(camera->pos, target, camera->up);
    glm::mat4 p = glm::perspective(glm::radians(camera->fov), ratio, 0.1f, 100.0f);

    *mvp = p * v * m;

    std::cout << camera->pos.x << " " << camera->pos.y << " " << camera->pos.z << std::endl;
}

GLuint Renderer::loadTexture(const char* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Load texture image here (using stb_image or another method)
    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);

    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    } else {
        std::cerr << "Failed to load texture: " << path << std::endl;
    }

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return textureID;
}

