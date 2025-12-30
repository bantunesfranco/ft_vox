
#ifndef Renderer_HPP
#define Renderer_HPP

constexpr auto FSHADER_PATH = "./engine/shaders/fragment.glsl";
constexpr auto VSHADER_PATH = "./engine/shaders/vertex.glsl";

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include "defines.hpp"
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class Camera;

class VBOManager {
	private:
		std::queue<GLuint> availableVBOs;
		std::mutex poolMutex;

		static inline std::unique_ptr<VBOManager> _instance = nullptr;

	public:
		VBOManager(const size_t initialPoolSize = 128) {
			std::lock_guard<std::mutex> lock(poolMutex);
			for (size_t i = 0; i < initialPoolSize; ++i) {
				GLuint vbo = 0;
				glGenBuffers(1, &vbo);
				if (glGetError() == GL_NO_ERROR)
					availableVBOs.push(vbo);
			}
		}

		~VBOManager() {
			std::lock_guard<std::mutex> lock(poolMutex);
			while (!availableVBOs.empty()) {
				GLuint vbo = availableVBOs.front();
				availableVBOs.pop();
				glDeleteBuffers(1, &vbo);
			}
		}

		GLuint getVBO() {
			std::lock_guard<std::mutex> lock(poolMutex);
			if (availableVBOs.empty()) {
				GLuint vbo = 0;
				glGenBuffers(1, &vbo);
				return vbo;
			}
			const GLuint vbo = availableVBOs.front();
			availableVBOs.pop();
			return vbo;
		}

		void returnVBO(const GLuint vbo) {
			std::lock_guard<std::mutex> lock(poolMutex);
			availableVBOs.push(vbo);
		}

		static VBOManager& get()
		{
			if (!_instance) _instance = std::make_unique<VBOManager>();
			return *_instance;
		}
};


class Renderer
{
	private:
		GLuint		_shaderprog;
		GLuint		_vao;
		GLuint		_vbo;
		GLuint		_ibo;
		GLint		_mvpLocation;

		static std::string* loadShaderCode(const char* path);
		static uint32_t		compileShader(const std::string* code, int32_t type);

	public:
		Renderer();
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = default;

		GLuint textureID;

		static void	initProjectionMatrix(const GLFWwindow* window, const std::unique_ptr<Camera>& camera, glm::mat4& mvp);

		void	render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& mvp);
		void	setTextureID(const GLuint id) { textureID = id; }
		void	releaseVBO();
        void	renderBoundingBox(const glm::vec3& minPos, const glm::vec3& maxPos);

		[[nodiscard]] GLuint	getShaderProgram()      const { return _shaderprog;  }
		[[nodiscard]] GLint		getMVPUniformLocation() const { return _mvpLocation; }
		[[nodiscard]] GLuint	getVertexArrayObject()  const { return _vao;         }
		[[nodiscard]] GLuint	getTextureID()          const { return textureID;    }
};

#endif
