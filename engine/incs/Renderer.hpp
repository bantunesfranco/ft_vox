
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "engine/shaders/fragment.glsl"
#define VSHADER_PATH "engine/shaders/vertex.glsl"

#include "glad/gl.h"
#include "GLFW/glfw3.h"
#include "stb_image.h"

#include "defines.hpp"
#include <cstring>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class Camera;
class Renderer
{
	private:
		GLuint _shaderprog;
		GLuint _vao;
		GLuint _vbo;
		GLuint _ibo;
		GLint _mvpLocation;
		GLuint _textureID;

		const char	*loadShaderCode(const char* path);
		uint32_t	compileShader(const char* code, int32_t type);

	public:
		Renderer();
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = default;
	
		void	render(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const glm::mat4 *mvp);
		void	initProjectionMatrix(GLFWwindow *window, Camera *camera, glm::mat4 *mvp);
		GLuint	getShaderProgram() const { return _shaderprog; }
		GLint	getMVPUniformLocation() const { return _mvpLocation; }
		GLuint	getVertexArrayObject() const { return _vao; }
		GLuint	loadTexture(const char* path);

};

#endif
