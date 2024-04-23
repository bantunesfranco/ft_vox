
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "engine/shaders/fragment.glsl"
#define VSHADER_PATH "engine/shaders/vertex.glsl"

#include "defines.hpp"
#include <cstring>

class Engine;

class Renderer
{
	private:
		uint32_t	_vao;
		uint32_t	_vbo;
		uint32_t	_shaderprog;

		const char*	loadShaderCode(const char* path);
		uint32_t		compileShader(const char* code, int32_t type);
		void		initBuffers();

	public:
		Renderer(Engine* engine);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&);
	
		void		render();
		uint32_t	getShaderProg() { return _shaderprog; }

};

#endif
