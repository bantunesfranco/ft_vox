/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/04/24 16:08:37 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <glad/gl.h>
#include "Engine.hpp"
#include "vertex.hpp"



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

    Engine *engine = Engine::initEngine(1440, 900, "Hello World", false);
    if (!engine)
    {
        return (EXIT_FAILURE);
    }

    glfwSetErrorCallback(error_callback);
    glfwSetKeyCallback(engine->window, key_callback);
 
    while (!glfwWindowShouldClose(engine->window))
    {
        engine->renderer->render();
    }
    
    engine->terminateEngine();
    delete engine;

    exit(EXIT_SUCCESS);
}