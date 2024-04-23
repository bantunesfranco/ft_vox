/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/04/23 21:37:03 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "Engine.hpp"
#include "vertex.hpp"

#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

static void error_callback(int error, const char* description)
{
    (void)error;
    fprintf(stderr, "Error: %s\n", description);
}
 
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
 
int main(void)
{

    Engine *engine = Engine::initEngine(800, 600, "Hello World", false);
    if (!engine)
    {
        return (EXIT_FAILURE);
    }

    glfwSetErrorCallback(error_callback);
    glfwSetKeyCallback(engine->window, key_callback);
 
    // NOTE: OpenGL error checks have been omitted for brevity
 
    GLuint program = engine->renderer->getShaderProg();
    const GLint mvp_location = glGetUniformLocation(program, "MVP");
    const GLint vpos_location = glGetAttribLocation(program, "vPos");
    const GLint vcol_location = glGetAttribLocation(program, "vCol");
 
    GLuint vertex_array;
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);
    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, pos));
    glEnableVertexAttribArray(vcol_location);
    glVertexAttribPointer(vcol_location, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*) offsetof(Vertex, col));
 
    while (!glfwWindowShouldClose(engine->window))
    {
        int width, height;
        glfwGetFramebufferSize(engine->window, &width, &height);
        const float ratio = width / (float) height;
 
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
 
        mat4x4 m, p, mvp;
        mat4x4_identity(m);
        mat4x4_rotate_Z(m, m, (float) glfwGetTime());
        mat4x4_ortho(p, -ratio, ratio, -1.f, 1.f, 1.f, -1.f);
        mat4x4_mul(mvp, p, m);
 
        glUseProgram(program);
        glUniformMatrix4fv(mvp_location, 1, GL_FALSE, (const GLfloat*) &mvp);
        glBindVertexArray(vertex_array);
        glDrawArrays(GL_TRIANGLES, 0, 3);
 
        glfwSwapBuffers(engine->window);
        glfwPollEvents();
    }
    
    engine->terminateEngine();
    delete engine;

    exit(EXIT_SUCCESS);
}