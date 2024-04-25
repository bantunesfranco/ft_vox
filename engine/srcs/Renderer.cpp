
#include <iostream>
#include <fstream>
#include <string>
#include "Engine.hpp"
#include "vertex.hpp"

#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

static const char *vshader_src = "#version 330 core\n"
"in vec3 ourColor;\n"
"in vec2 TexCoord;\n"
"out vec4 FragColor;\n"
"uniform sampler2D ourTexture;\n"
"uniform bool useTexture;\n"
"void main()\n"
"{\n"
"    if (useTexture) {\n"
"        FragColor = texture(ourTexture, TexCoord);\n"
"    } else {\n"
"        FragColor = vec4(ourColor, 1.0);\n"
"    }\n"
"}\n";

static const char* fshader_src = "#version 330 core\n"
"in vec2 TexCoord;\n"
"flat in int TexIndex;\n"
"in vec3 vCol;\n"
"out vec4 FragColor;\n"
"uniform bool useTexture;\n"
"uniform sampler2D Texture0;\n"
"uniform sampler2D Texture1;\n"
"uniform sampler2D Texture2;\n"
"uniform sampler2D Texture3;\n"
"uniform sampler2D Texture4;\n"
"uniform sampler2D Texture5;\n"
"uniform sampler2D Texture6;\n"
"uniform sampler2D Texture7;\n"
"uniform sampler2D Texture8;\n"
"uniform sampler2D Texture9;\n"
"uniform sampler2D Texture10;\n"
"uniform sampler2D Texture11;\n"
"uniform sampler2D Texture12;\n"
"uniform sampler2D Texture13;\n"
"uniform sampler2D Texture14;\n"
"uniform sampler2D Texture15;\n"
"void main()\n"
"{\n"
"    vec4 outColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"    switch (int(TexIndex)) {\n"
"        case 0: outColor = texture(Texture0, TexCoord); break;\n"
"        case 1: outColor = texture(Texture1, TexCoord); break;\n"
"        case 2: outColor = texture(Texture2, TexCoord); break;\n"
"        case 3: outColor = texture(Texture3, TexCoord); break;\n"
"        case 4: outColor = texture(Texture4, TexCoord); break;\n"
"        case 5: outColor = texture(Texture5, TexCoord); break;\n"
"        case 6: outColor = texture(Texture6, TexCoord); break;\n"
"        case 7: outColor = texture(Texture7, TexCoord); break;\n"
"        case 8: outColor = texture(Texture8, TexCoord); break;\n"
"        case 9: outColor = texture(Texture9, TexCoord); break;\n"
"        case 10: outColor = texture(Texture10, TexCoord); break;\n"
"        case 11: outColor = texture(Texture11, TexCoord); break;\n"
"        case 12: outColor = texture(Texture12, TexCoord); break;\n"
"        case 13: outColor = texture(Texture13, TexCoord); break;\n"
"        case 14: outColor = texture(Texture14, TexCoord); break;\n"
"        case 15: outColor = texture(Texture15, TexCoord); break;\n"
"        default: outColor = vec4(1.0, 0.0, 0.0, 1.0); break;\n"
"    }\n"
"    FragColor = outColor;\n"
"}\n";

// static const char* vshader_src =
// "#version 330\n"
// "uniform mat4 MVP;\n"
// "in vec3 vCol;\n"
// "in vec2 vPos;\n"
// "out vec3 color;\n"
// "void main()\n"
// "{\n"
// "    gl_Position = MVP * vec4(vPos, 0.0, 1.0);\n"
// "    color = vCol;\n"
// "}\n";
 
// static const char* fshader_src =
// "#version 330\n"
// "in vec3 color;\n"
// "out vec4 fragment;\n"
// "void main()\n"
// "{\n"
// "    fragment = vec4(color, 1.0);\n"
// "}\n";

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
	
	glfwSetFramebufferSizeCallback(engine->window, framebuffer_callback);
	glfwSetWindowUserPointer(engine->window, engine);
	gladLoadGL(glfwGetProcAddress);
	glfwSwapInterval(1);


	GLint fshader = 0;
	GLint vshader = 0;
	
	// char*	code = NULL;

	// loadShaderCode(FSHADER_PATH, code);
	vshader = compileShader(vshader_src, GL_VERTEX_SHADER);
	if (!vshader)
		throw Engine::EngineException(VOX_VERTFAIL);
	
	// loadShaderCode(VSHADER_PATH, code);
	fshader = compileShader(fshader_src, GL_FRAGMENT_SHADER);
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
	glBindVertexArray(this->_vao);
	glGenBuffers(1, &this->_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, this->_vbo);

	// // Vertex XYZ coordinates
	// glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), NULL);
	// glEnableVertexAttribArray(0);

	// // UV Coordinates
	// glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(sizeof(float) * 3));
	// glEnableVertexAttribArray(1);

	// // Texture index
	// glVertexAttribIPointer(2, 1, GL_BYTE, sizeof(Vertex), (void *)(sizeof(float) * 5));
	// glEnableVertexAttribArray(2);

	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture0"), 0);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture1"), 1);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture2"), 2);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture3"), 3);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture4"), 4);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture5"), 5);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture6"), 6);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture7"), 7);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture8"), 8);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture9"), 9);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture10"), 10);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture11"), 11);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture12"), 12);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture13"), 13);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture14"), 14);
	// glUniform1i(glGetUniformLocation(_shaderprog, "Texture15"), 15);
}

uint32_t Renderer::compileShader(const char* code, int32_t type)
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
	// Define the vertices of the square
	float vertices[] = {
		-0.5f, -0.5f, 0.0f, // bottom left
		0.5f, -0.5f, 0.0f,  // bottom right
		0.5f, 0.5f, 0.0f,   // top right
		-0.5f, 0.5f, 0.0f   // top left
	};

	// Define the indices of the square
	unsigned int indices[] = {
		0, 1, 2, // first triangle
		2, 3, 0  // second triangle
	};

	// Bind the vertex array object
	glBindVertexArray(this->_vao);

	// Bind the vertex buffer object
	glBindBuffer(GL_ARRAY_BUFFER, this->_vbo);

	// Set the vertex data
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// Bind the element buffer object
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->_ebo);

	// Set the element data
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// Set the vertex attribute pointers
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Draw the square
	// glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	glad_glDrawBuffer()

	// Unbind the vertex array object
	glBindVertexArray(0);
}