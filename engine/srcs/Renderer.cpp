#include "Renderer.hpp"
#include <iostream>
#include <fstream>
#include "vertex.h"


Engine::Renderer::Renderer(Engine* engine)
{
	this->_vao = 0;
	this->_vbo = 0;
	this->_shaderprog = 0;
	
	glfwMakeContextCurrent(engine->window);
	glfwSetFramebufferSizeCallback(engine->window, framebuffer_callback);
	glfwSetWindowUserPointer(engine->window, engine);
	glfwSwapInterval(1);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		throw Engine::EngineException(VOX_GLADFAIL);

	GLint fshader = 0;
	GLint vshader = 0;
	
	const char* code = loadShaderCode(FSHADER_PATH);
	vshader = compileShader(code, GL_VERTEX_SHADER);
	if (!vshader)
		throw Engine::EngineException(VOX_VERTFAIL);
	
	code = loadShaderCode(VSHADER_PATH);
	fshader = compileShader(FSHADER_PATH, GL_FRAGMENT_SHADER);
	if (!fshader)
		throw Engine::EngineException(VOX_FRAGFAIL);this->

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

Engine::Renderer::~Renderer()
{
	glDeleteProgram(this->_shaderprog);
	glDeleteBuffers(1, &this->_vbo);
	glDeleteVertexArrays(1, &this->_vao);
}

static void framebuffer_callback(GLFWwindow *window, int width, int height)
{
	(void)window;
	glViewport(0, 0, width, height);
}

void	Engine::Renderer::initBuffers()
{
	glActiveTexture(GL_TEXTURE0);
	glGenVertexArrays(1, &_vao);
	glGenBuffers(1, &_vbo);
	glBindVertexArray(_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);

	// Vertex XYZ coordinates
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), NULL);
	glEnableVertexAttribArray(0);

	// UV Coordinates
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void *)(sizeof(float) * 3));
	glEnableVertexAttribArray(1);

	// Texture index
	glVertexAttribIPointer(2, 1, GL_BYTE, sizeof(vertex_t), (void *)(sizeof(float) * 5));
	glEnableVertexAttribArray(2);

	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture0"), 0);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture1"), 1);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture2"), 2);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture3"), 3);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture4"), 4);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture5"), 5);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture6"), 6);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture7"), 7);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture8"), 8);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture9"), 9);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture10"), 10);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture11"), 11);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture12"), 12);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture13"), 13);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture14"), 14);
	glUniform1i(glGetUniformLocation(this->_shaderprog, "Texture15"), 15);
}

GLuint Engine::Renderer::compileShader(const char* code, int32_t type)
{
	int32_t success;
	char infolog[512] = {0};

	GLuint shader = glCreateShader(type);
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

const char* Engine::Renderer::loadShaderCode(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return (nullptr);

	const char* code;
	file.get(code, file.gcount());
	file.close();

	return (code);
}