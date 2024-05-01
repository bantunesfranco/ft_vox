
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
"#version 330\n"
"uniform mat4 MVP;\n"
"in vec3 vCol;\n"
"in vec2 vPos;\n"
"out vec3 color;\n"
"void main()\n"
"{\n"
"    gl_Position = MVP * vec4(vPos, 0.0, 1.0);\n"
"    color = vCol;\n"
"}\n";
 
static const char* fshader_src =
"#version 330\n"
"in vec3 color;\n"
"out vec4 fragment;\n"
"void main()\n"
"{\n"
"    fragment = vec4(color, 1.0);\n"
"}\n";

extern float rotation;

static const std::vector<Vertex> vertices =
{
    { { .5f, -.5f }, { 0.f, 0.f, 1.f } },
    { { .5f, .5f }, { 1.f, 0.f, 1.f } },
    { { -.5f, -.5f }, { 1.f, 0.f, 1.f } },
    { { -.5f, .5f }, { 0.f, 0.f, 1.f } },
};

void	Renderer::initBuffers()
{
	glGenBuffers(1, &_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, _vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

	// char*	code = NULL;
	// loadShaderCode(VSHADER_PATH, code);
	GLint vshader = compileShader(vshader_src, GL_VERTEX_SHADER);
	if (!vshader)
		throw Engine::EngineException(VOX_VERTFAIL);
	
	// loadShaderCode(FSHADER_PATH, code);
	GLint fshader = compileShader(fshader_src, GL_FRAGMENT_SHADER);
	if (!fshader)
		throw Engine::EngineException(VOX_FRAGFAIL);

	_shaderprog = glCreateProgram();
	if (!_shaderprog)
		throw Engine::EngineException(VOX_SHDRFAIL);

	glAttachShader(_shaderprog, vshader);
	glAttachShader(_shaderprog, fshader);
	glLinkProgram(_shaderprog);

	glDeleteShader(vshader);
	glDeleteShader(fshader);

	GLint success;
	glGetProgramiv(_shaderprog, GL_LINK_STATUS, &success);
	if (!success)
	{
		char infolog[512] = {0};
		glGetProgramInfoLog(_shaderprog, sizeof(infolog), NULL, infolog);
		std::cerr << static_cast<char*>(infolog) << std::endl;
		throw Engine::EngineException(VOX_SHDRFAIL);
	}

	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
		std::cerr << "OpenGL error: " << error << std::endl;

    const GLint vpos_location = glGetAttribLocation(_shaderprog, "vPos");
    const GLint vcol_location = glGetAttribLocation(_shaderprog, "vCol");
 
    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);
    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, pos));
    glEnableVertexAttribArray(vcol_location);
    glVertexAttribPointer(vcol_location, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, col));
}

Renderer::~Renderer()
{
	glDeleteProgram(_shaderprog);
	glDeleteBuffers(1, &(_vbo));
	glDeleteVertexArrays(1, &(_vao));
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
	mat4x4 mvp;
	GLFWwindow *window = Engine::getInstance()->window;
	const GLint mvp_location = glGetUniformLocation(_shaderprog, "MVP");

	initProjectionMatrix(window, &mvp);
	glUseProgram(_shaderprog);
	glUniformMatrix4fv(mvp_location, 1, GL_FALSE, (const GLfloat*) &mvp);
	glBindVertexArray(_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, vertices.size());
}

void Renderer::initProjectionMatrix(GLFWwindow *window, mat4x4 *mvp) {
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	const float ratio = width / (float) height;

	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);

	mat4x4 m, p, v, mv;
	vec3 tmp;
	mat4x4_identity(m);
	mat4x4_rotate_Z(m, m, rotation);

	Camera *camera = Engine::getInstance()->camera;
	vec3_add(tmp, camera->pos, camera->dir);
	mat4x4_look_at(v, camera->pos, tmp, camera->up);

	mat4x4_perspective(p, 90.0f , ratio, 0.1f, 100.0f);
	mat4x4_mul(mv, v, m);
	mat4x4_mul(*mvp, p, mv);
}