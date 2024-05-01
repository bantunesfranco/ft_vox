/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/05/01 14:41:22 by bfranco       ########   odam.nl         */
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

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static double lastX = 0.0f;
    static double lastY = 0.0f;
    static bool firstMouse = true;

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f; // adjust to your liking
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    Camera* camera = engine->camera;

    camera->yaw   += xoffset;
    camera->pitch += yoffset;
    camera->mousePos[0] = xpos;
    camera->mousePos[1] = ypos;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
    if (camera->pitch > 89.0f)
        camera->pitch = 89.0f;
    if (camera->pitch < -89.0f)
        camera->pitch = -89.0f;
    
    camera->yaw = fmod(camera->yaw + xoffset, 360.0f);

    vec3 front;
    float pitch = camera->pitch / 2.f * M_PI / 180.0f;
    float yaw = camera->yaw / 2.f * M_PI / 180.0f;
    front[0] = cosf(yaw) * cosf(pitch);
    front[1] = sinf(pitch);
    front[2] = sinf(yaw) * cosf(pitch);
    vec3_norm(camera->dir, front);
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
        vec3_sub(camera->pos, camera->pos, perp);
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

    Engine::setSetting(VOX_FULLSCREEN, true);

    Engine *engine = Engine::initEngine(1440, 900, "Hello World", false);
    if (!engine)
    {
        return (EXIT_FAILURE);
    }

    engine->setErrorCallback(error_callback);
    engine->setKeyCallback(key_callback);
    engine->setCursorPosCallback(mouse_callback);
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