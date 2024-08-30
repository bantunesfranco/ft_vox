
#include "Engine.hpp"
#include "Renderer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

#define GLAD_GL_IMPLEMENTATION 
#define GLFW_INCLUDE_NONE
#define STB_IMAGE_IMPLEMENTATION


Renderer::Renderer() : _shaderprog(0), _vao(0), _vbo(0), _ibo(0), _mvpLocation(-1), _textureID(0) {
    const char* code = loadShaderCode(VSHADER_PATH);
    GLuint vshader = compileShader(code, GL_VERTEX_SHADER);
    if (!vshader) throw Engine::EngineException(VOX_VERTFAIL);

    code = loadShaderCode(FSHADER_PATH);
    GLuint fshader = compileShader(code, GL_FRAGMENT_SHADER);
    if (!fshader) throw Engine::EngineException(VOX_FRAGFAIL);

    _shaderprog = glCreateProgram();
    if (!_shaderprog) throw Engine::EngineException(VOX_SHDRFAIL);

    glAttachShader(_shaderprog, vshader);
    glAttachShader(_shaderprog, fshader);
    glLinkProgram(_shaderprog);

    glDeleteShader(vshader);
    glDeleteShader(fshader);

    GLint success;
    glGetProgramiv(_shaderprog, GL_LINK_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetProgramInfoLog(_shaderprog, sizeof(infolog), NULL, infolog);
        std::cerr << infolog << std::endl;
        throw Engine::EngineException(VOX_SHDRFAIL);
    }

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);

    const GLint vpos_location = glGetAttribLocation(_shaderprog, "vPos");
    const GLint vcol_location = glGetAttribLocation(_shaderprog, "vCol");
    const GLint vtex_location = glGetAttribLocation(_shaderprog, "vTexCoord");

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
    glDeleteVertexArrays(1, &_vao);
    if (_textureID) {
        glDeleteTextures(1, &_textureID);
    }
}

GLuint Renderer::compileShader(const char* code, int32_t type) {
    if (!code) return 0;

    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &code, nullptr);
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

const char* Renderer::loadShaderCode(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return nullptr;
    }

    std::stringstream shaderStream;
    shaderStream << file.rdbuf();
    file.close();

    std::string str = shaderStream.str();
    return str.c_str();
}

void Renderer::render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4 *mvp) {
    glUseProgram(_shaderprog);

    glBindVertexArray(_vao);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);


    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _textureID);

    GLint texLoc = glGetUniformLocation(_shaderprog, "textureSampler");
    glUniform1i(texLoc, 0);

    // Set MVP matrix
    glUniformMatrix4fv(_mvpLocation, 1, GL_FALSE, glm::value_ptr(*mvp));

    // Draw
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

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
    glm::mat4 p = glm::perspective(glm::radians(45.0f), ratio, 0.1f, 100.0f);

    *mvp = p * v * m;
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

