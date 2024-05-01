/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/05/01 09:27:13 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <glad/gl.h>
#include "Engine.hpp"
#include "vertex.hpp"
#include <iostream>

float rotation = 0.f;

static void error_callback(int error, const char* description)
{
    (void)error;
    fprintf(stderr, "Error: %s\n", description);
}
 
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    Camera* camera = engine->camera;

    if (key == VOX_KEY_ESCAPE && action == VOX_PRESS)
        Engine::getInstance()->closeWindow();
    if (key == VOX_KEY_SPACE && (action == VOX_PRESS || action == VOX_REPEAT))
        std::cout << "Space key pressed" << std::endl;
    if (key == VOX_KEY_W && (action == VOX_PRESS || action == VOX_REPEAT))
        vec3_add(camera->pos, camera->pos, camera->dir);
    if (key == VOX_KEY_A && (action == VOX_PRESS || action == VOX_REPEAT))
    {
        vec3 perp;
        vec3_mul_cross(perp, camera->dir, camera->up);
        vec3_add(camera->pos, camera->pos, perp);
    }
    if (key == VOX_KEY_S && (action == VOX_PRESS || action == VOX_REPEAT))
        vec3_sub(camera->pos, camera->pos, camera->dir);
    if (key == VOX_KEY_D && (action == VOX_PRESS || action == VOX_REPEAT))
    {
        vec3 perp;
        vec3_mul_cross(perp, camera->dir, camera->up);
        vec3_add(camera->pos, camera->pos, perp);
    }
    if (key == VOX_KEY_RIGHT && (action == VOX_PRESS || action == VOX_REPEAT))
        rotation += 0.05;
    if (key == VOX_KEY_LEFT && (action == VOX_PRESS || action == VOX_REPEAT))
        rotation -= 0.05;
}
 
int main(void)
{

    Engine *engine = Engine::initEngine(1440, 900, "Hello World", false);
    if (!engine)
    {
        return (EXIT_FAILURE);
    }

    engine->setErrorCallback(error_callback);
    engine->setKeyCallback(key_callback);
    while (!glfwWindowShouldClose(engine->window))
	{
		engine->renderer->render();
		glfwSwapBuffers(engine->window);
		glfwPollEvents();
	}
    engine->terminateEngine();
    delete engine;

    exit(EXIT_SUCCESS);
}