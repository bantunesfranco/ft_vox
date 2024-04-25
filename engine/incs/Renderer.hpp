
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
		mat4x4		_projection;

		void		loadShaderCode(const char* path, char* code);
		uint32_t	compileShader(const char* code, int32_t type);
		void		setProjectionMatrix(const vec4& projection);
		void		initProjectionMatrix(int screenWidth, int screenHeight);

		

	public:
		Renderer();
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
