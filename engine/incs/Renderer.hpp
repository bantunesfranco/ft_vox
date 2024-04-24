
#ifndef Renderer_HPP
#define Renderer_HPP

#define FSHADER_PATH "engine/shaders/fragment.glsl"
#define VSHADER_PATH "engine/shaders/vertex.glsl"

#include "defines.hpp"
#include <cstring>
#include <string>

class Engine;

class Renderer
{
	private:
		uint32_t	_vao;
		uint32_t	_vbo;
		uint32_t	_shaderprog;

		void				loadShaderCode(const char* path, char* code);
		uint32_t			compileShader(const char* code, int32_t type);
		void				initBuffers();

	public:
		Renderer(Engine* engine);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&);
	
		void		render();
		uint32_t	getShaderProg() { return _shaderprog; }

};

#endif
