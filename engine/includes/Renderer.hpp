
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "engine/shaders/fragment.glsl"
#define VSHADER_PATH "engine/shaders/vertex.glsl"

#include "defines.hpp"
#include "Engine.hpp"
#include <cstring>

class Engine::Renderer
{
	private:
		Renderer();
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&);

		GLuint	_vao;
		GLuint	_vbo;
		GLuint	_shaderprog;

		const char*	loadShaderCode(const char* path);
		GLuint		compileShader(const char* code, int32_t type)
		void		initBuffers();

	public:
		void		render();
		GLuint		getShaderProg() { return _shaderprog; }
};

#endif
