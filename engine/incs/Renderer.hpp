
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "./engine/shaders/fragment.glsl"
#define VSHADER_PATH "./engine/shaders/vertex.glsl"

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include "defines.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <queue>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class Camera;

class VBOManager {
public:
    VBOManager(const size_t initialPoolSize = 10) {
        // Preallocate a pool of VBOs
        for (size_t i = 0; i < initialPoolSize; ++i) {
            GLuint vbo = 0;
            glGenBuffers(1, &vbo);
			if (glGetError() != GL_NO_ERROR)
	        	availableVBOs.push(vbo);
        }
    }

    ~VBOManager() {
        // Clean up all VBOs in the pool
        while (!availableVBOs.empty()) {
            GLuint vbo = availableVBOs.front();
            availableVBOs.pop();
            glDeleteBuffers(1, &vbo);
        }
    }

    GLuint getVBO() {
        // If the pool is empty, create a new VBO
        if (availableVBOs.empty()) {
            GLuint vbo = 0;
            glGenBuffers(1, &vbo);
            return vbo;
        }

        // Otherwise, reuse a VBO from the pool
        const GLuint vbo = availableVBOs.front();
        availableVBOs.pop();
        return vbo;
    }

    void returnVBO(const GLuint vbo) {
        // Return the VBO to the pool for reuse
        availableVBOs.push(vbo);
    }

private:
    std::queue<GLuint> availableVBOs;
};


class Renderer
{
	private:
		GLuint		_shaderprog;
		GLuint		_vao;
		GLuint		_vbo;
		GLuint		_ibo;
		GLint		_mvpLocation;
		VBOManager	*_vboManager;

		static std::string* loadShaderCode(const char* path);
		static uint32_t	compileShader(const std::string* code, int32_t type);

	public:
		Renderer();
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = default;

		GLuint textureID;

		static void	initProjectionMatrix(const GLFWwindow* window, const std::unique_ptr<Camera>& camera, glm::mat4& mvp);

		void	render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4 *mvp);
		[[nodiscard]] GLuint	getShaderProgram() const { return _shaderprog; }
		[[nodiscard]] GLint	getMVPUniformLocation() const { return _mvpLocation; }
		[[nodiscard]] GLuint	getVertexArrayObject() const { return _vao; }
		void	setTextureID(const GLuint id) { textureID = id; }
		[[nodiscard]] GLuint	getTextureID() const { return textureID; }
		void	releaseVBO();
        void    renderBoundingBox(const glm::vec3& minPos, const glm::vec3& maxPos);
};

#endif
