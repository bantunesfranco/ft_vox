
#include <iostream>
#include <fstream>
#include <string>
#include "Engine.hpp"
#include "vertex.hpp"
#include <vector>
#include <cmath>

#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

static const char* vshader_src =
"#version 330 core\n\
layout(location = 0) in vec3 position;\n\
layout(location = 1) in vec3 color;\n\
out vec3 FragPos;\n\
out vec3 Color;\n\
uniform mat4 tranform;\n\
uniform mat4 view;\n\
uniform mat4 projection;\n\
void main() {\n\
    FragPos = vec3(tranform * vec4(position, 1.0));\n\
    Color = color;\n\
    gl_Position = projection * view * vec4(FragPos, 1.0);\n\
}\n";
 
static const char* fshader_src =
"#version 330 core\n\
in vec3 FragPos;\n\
in vec3 color;\n\
out vec4 FragColor;\n\
void main() {\n\
    FragColor = vec4(color, 1.0);\n\
}\n";

static const std::vector<Vertex> vertices =
{
    { { -0.6f, -0.4f, 0.0f }, { 1.f, 0.f, 0.f } },
    { {  0.6f, -0.4f, 0.0f }, { 0.f, 1.f, 0.f } },
    { {   0.f,  0.6f, 0.0f }, { 0.f, 0.f, 1.f } }
};

Renderer::Renderer() : _vao(0), _vbo(0), _shaderprog(0) {};

void	Renderer::initBuffers()
{
	glGenVertexArrays(1, &this->_vao);
	glBindVertexArray(this->_vao);

	glGenBuffers(1, &this->_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, this->_vbo);

	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos)); // Position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, col)); // Color
	glEnableVertexAttribArray(1);

	GLint fshader = 0;
	GLint vshader = 0;
	
	char*	code = NULL;
	loadShaderCode(FSHADER_PATH, code);
	vshader = compileShader(code, GL_VERTEX_SHADER);
	if (!vshader)
		throw Engine::EngineException(VOX_VERTFAIL);
	
	loadShaderCode(VSHADER_PATH, code);
	fshader = compileShader(code, GL_FRAGMENT_SHADER);
	if (!fshader)
		throw Engine::EngineException(VOX_FRAGFAIL);

	this->_shaderprog = glCreateProgram();
	if (!this->_shaderprog)
		throw Engine::EngineException(VOX_SHDRFAIL);

	glAttachShader(this->_shaderprog, vshader);
	glAttachShader(this->_shaderprog, fshader);
	glLinkProgram(this->_shaderprog);

	glDeleteShader(vshader);
	glDeleteShader(fshader);

	GLint success;
	glGetProgramiv(this->_shaderprog, GL_LINK_STATUS, &success);
	if (!success)
	{
		char infolog[512] = {0};
		glGetProgramInfoLog(this->_shaderprog, sizeof(infolog), NULL, infolog);
		std::cerr << static_cast<char*>(infolog) << std::endl;
		throw Engine::EngineException(VOX_SHDRFAIL);
	}

	glUseProgram(this->_shaderprog);
	glBindVertexArray(0);

	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
		std::cerr << "OpenGL error: " << error << std::endl;
}

Renderer::~Renderer()
{
	glDeleteProgram(this->_shaderprog);
	glDeleteBuffers(1, &(this->_vbo));
	glDeleteVertexArrays(1, &(this->_vao));
}

uint32_t Renderer::compileShader(const char* code, int32_t type)
{
	GLint success;
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
		glGetShaderInfoLog(shader, sizeof(infolog), &len, infolog);
		std::cerr << static_cast<char*>(infolog) << std::endl;
		return (0);
	}
	return (shader);
}

void Renderer::loadShaderCode(const char* path, char* code)
{
	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string data = "";
	std::string buff;
	while (!file.eof())
	{
		std::getline(file, buff);
		data += buff + "\n";
	}
	file.close();

	*(&code) = const_cast<char *>(data.c_str());
}

void Renderer::render() {
	glUseProgram(this->_shaderprog);

	GLint projectionLoc = glGetUniformLocation(_shaderprog, "projection");
	glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, (const GLfloat*)this->_projection);

	glBindVertexArray(this->_vao);
	glDrawArrays(GL_TRIANGLES, 0, vertices.size());
	glBindVertexArray(0);

	glUseProgram(0);
}

void Renderer::setProjectionMatrix(const vec4& projection) {
    GLint projectionLoc = glGetUniformLocation(this->_shaderprog, "projection");
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, projection);
}

void Renderer::initProjectionMatrix(int screenWidth, int screenHeight) {
    float aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    float fov = 45.0f; // Field of view
    float nearPlane = 0.1f; // Near clipping plane
    float farPlane = 100.0f; // Far clipping plane

	mat4x4_perspective(this->_projection, (fov * 3.141592f / 180.0f), aspectRatio, nearPlane, farPlane);
}