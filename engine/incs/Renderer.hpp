
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "engine/shaders/fragment.glsl"
#define VSHADER_PATH "engine/shaders/vertex.glsl"

#include "defines.hpp"
#include <cstring>
#include <string>

class Renderer
{
	private:
		uint32_t	_vao;
		uint32_t	_vbo;
		uint32_t	_shaderprog;

		void		loadShaderCode(const char* path, char* code);
		uint32_t	compileShader(const char* code, int32_t type);
		void		initProjectionMatrix(GLFWwindow *window, mat4x4 *mvp);

	public:
		Renderer() = default;
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&);
	
		void		render();
		void		initBuffers();
		uint32_t	getShaderProg() { return _shaderprog; }
		uint32_t	getVao() { return _vao; }
		uint32_t	getVbo() { return _vbo; }

};

#endif
