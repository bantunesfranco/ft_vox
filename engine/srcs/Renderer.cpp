
#include <iostream>
#include <fstream>
#include "Engine.hpp"
#include "vertex.hpp"

#include "glad/gl.h"

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

Renderer::Renderer(Engine* engine)
{
	this->_vao = 0;
	this->_vbo = 0;
	this->_shaderprog = 0;
	
	glfwMakeContextCurrent(engine->window);
	glfwSetFramebufferSizeCallback(engine->window, framebuffer_callback);
	glfwSetWindowUserPointer(engine->window, engine);
	glfwSwapInterval(1);

	GLint fshader = 0;
	GLint vshader = 0;
	
	const char* code = loadShaderCode(FSHADER_PATH);
	vshader = compileShader(code, GL_VERTEX_SHADER);
	if (!vshader)
		throw Engine::EngineException(VOX_VERTFAIL);
	
	code = loadShaderCode(VSHADER_PATH);
	fshader = compileShader(FSHADER_PATH, GL_FRAGMENT_SHADER);
	if (!fshader)
		throw Engine::EngineException(VOX_FRAGFAIL);

	this->_shaderprog = glCreateProgram();
	if (!this->_shaderprog)
		throw Engine::EngineException(VOX_SHDRFAIL);

	glAttachShader(this->_shaderprog, vshader);
	glAttachShader(this->_shaderprog, fshader);
	glLinkProgram(this->_shaderprog);


	GLint success;
	glGetProgramiv(this->_shaderprog, GL_LINK_STATUS, &success);
	if (!success)
	{
		char infolog[512] = {0};
		glGetProgramInfoLog(this->_shaderprog, sizeof(infolog), NULL, infolog);
		std::cerr << static_cast<char*>(infolog) << std::endl;
		throw Engine::EngineException(VOX_SHDRFAIL);
	}

	glDeleteShader(vshader);
	glDeleteShader(fshader);
	glUseProgram(this->_shaderprog);

	initBuffers();
}

Renderer::~Renderer()
{
	glDeleteProgram(this->_shaderprog);
	glDeleteBuffers(1, &(this->_vbo));
	glDeleteVertexArrays(1, &(this->_vao));
}

void	Renderer::initBuffers()
{
	glActiveTexture(GL_TEXTURE0);
	glGenVertexArrays(1, &this->_vao);
	glGenBuffers(1, &this->_vbo);
	glBindVertexArray(this->_vao);
	glBindBuffer(GL_ARRAY_BUFFER, this->_vbo);

	// Vertex XYZ coordinates
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), NULL);
	glEnableVertexAttribArray(0);

	// UV Coordinates
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(sizeof(float) * 3));
	glEnableVertexAttribArray(1);

	// Texture index
	glVertexAttribIPointer(2, 1, GL_BYTE, sizeof(Vertex), (void *)(sizeof(float) * 5));
	glEnableVertexAttribArray(2);

	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUniform1i(glGetUniformLocation(_shaderprog, "Texture0"), 0);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture1"), 1);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture2"), 2);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture3"), 3);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture4"), 4);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture5"), 5);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture6"), 6);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture7"), 7);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture8"), 8);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture9"), 9);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture10"), 10);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture11"), 11);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture12"), 12);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture13"), 13);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture14"), 14);
	glUniform1i(glGetUniformLocation(_shaderprog, "Texture15"), 15);
}

uint32_t Renderer::compileShader(const char* code, int32_t type)
{
	int32_t success;
	char infolog[512] = {0};

	uint32_t shader = glCreateShader(type);
	if (!code || !shader)
		return (0);

	GLint len = strlen(code);
	glShaderSource(shader, 1, &code, &len);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(shader, sizeof(infolog), NULL, infolog);
		std::cerr << static_cast<char*>(infolog) << std::endl;
		return (0);
	}
	return (shader);
}

const char* Renderer::loadShaderCode(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return (nullptr);

	char* code = nullptr;
	file.get(code, file.gcount());
	file.close();

	return (code);
}