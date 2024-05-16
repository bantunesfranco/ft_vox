
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
"in vec3 vPos;\n"
"out vec3 color;\n"
"void main()\n"
"{\n"
"    gl_Position = MVP * vec4(vPos, 1.0);\n"
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

static std::vector<Vertex> vertices;

// Function to generate vertices for a block at a given position
void generateBlockVertices(std::vector<Vertex>& vertices, const vec3& position, const vec3& color) {
	
    // Define the positions of the vertices relative to the block's center
	const vec3 positions[16] = {
		// x     y     z
		{-0.5, -0.5,  0.5}, // 0  left          First Strip
		{-0.5,  0.5,  0.5}, // 1
		{-0.5, -0.5, -0.5}, // 2
		{-0.5,  0.5, -0.5}, // 3
		{ 0.5, -0.5, -0.5}, // 4  back
		{ 0.5,  0.5, -0.5}, // 5
		{ 0.5, -0.5,  0.5}, // 6  right
		{ 0.5,  0.5,  0.5}, // 7
		{ 0.5,  0.5, -0.5}, // 5  top           Second Strip
		{-0.5,  0.5, -0.5}, // 3
		{ 0.5,  0.5,  0.5}, // 7
		{-0.5,  0.5,  0.5}, // 1
		{ 0.5, -0.5,  0.5}, // 6  front
		{-0.5, -0.5,  0.5}, // 0
		{ 0.5, -0.5, -0.5}, // 4  bottom
		{-0.5, -0.5, -0.5}  // 2
	};

    // Define the indices of the vertices that make up each face of the block
	const int faceIndices[24] = {
		0, 1, 2, 3, // Front face
		7, 6, 5, 4, // Back face
		// 3, 2, 6, 7, // Top face
		// 4, 5, 1, 0, // Bottom face
		3, 4, 1, 6, // Right face
		4, 0, 3, 7, // Left face
	};

	vec3 colors[6] = {{0.0f, 0.0f, 1.0f},{1.0f, 0.0f, 0.0f},{0.0f, 1.0f, 0.0f},{1.0f, 0.0f, 1.0f},{1.0f, 1.0f, .0f},{0.0f, 1.0f, 1.0f}};
	// Generate vertices for each face of the block
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {

            // Calculate the index of the current vertex
            int index = faceIndices[i * 4 + j];

            // Calculate the position of the vertex in world space
            vec3 pos = {
                position[0] + positions[index][0],
                position[1] + positions[index][1],
                position[2] + positions[index][2]
            };

			Vertex vertex;
			vec3 lol = {colors[i][0], colors[i][1], colors[i][2]};
			(void)color;
			
			vec3_dup(vertex.pos, pos);
			vec3_dup(vertex.col, lol);

            // Set the position and color of the vertex
            vertices.push_back(vertex);
        }
    }
}

void generatePyramid(std::vector<Vertex>& vertices) {
    // Clear the vertices vector
    vertices.clear();

    // Generate vertices for a 3x3x3 pyramid of blocks
	for (int y = 0; y >= 0; --y) {
		for (int x = -y; x <= y; ++x) {
			for (int z = -y; z <= y; ++z) {
				// Calculate the position of the block
				vec3 pos = {
					x * BLOCK_SIZE,
					y * BLOCK_SIZE,
					z * BLOCK_SIZE
				};
				
				// Define color
				const vec3 blockColor = {(float)x, 1.f, (float)z}; // Red color

				// Generate vertices for the block at the current position
				generateBlockVertices(vertices, pos, blockColor);
			}
		}
	}
}


void	Renderer::initBuffers()
{
	// Generate vertices for a block at the origin
	generatePyramid(vertices);

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
    glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE,
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

	// glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	initProjectionMatrix(window, &mvp);

	glUseProgram(_shaderprog);
	glUniformMatrix4fv(mvp_location, 1, GL_FALSE, (const GLfloat*) &mvp);
	glBindVertexArray(_vao);

	    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // Disable face culling
	glFrontFace(GL_CCW); // Set the front face to be the one with vertices in clockwise order

    // Clear color and depth buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (int i = 0; i < (int)vertices.size(); i+=4)
		glDrawArrays(GL_TRIANGLE_STRIP, i, 4);
}

void Renderer::initProjectionMatrix(GLFWwindow *window, mat4x4 *mvp) {
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	const float ratio = static_cast<float>(width) / static_cast<float>(height);

	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);

	mat4x4 m, p, v, mv;
	vec3 tmp;
	mat4x4_identity(m);

	Camera *camera = Engine::getInstance()->camera;
	vec3_add(tmp, camera->pos, camera->dir);
	mat4x4_look_at(v, camera->pos, tmp, camera->up);

	mat4x4_perspective(p, DEG2RAD(90.0f), ratio, 0.1f, 100.0f);
	mat4x4_mul(mv, v, m);
	mat4x4_mul(*mvp, p, mv);
}