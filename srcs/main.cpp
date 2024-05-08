/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/05/08 16:15:13 by bfranco       ########   odam.nl         */
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
    double yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.015f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    Camera* camera = engine->camera;

    camera->yaw   += xoffset;
    camera->pitch += yoffset;
    
    double x;
    glfwGetWindowSize(window, (int*)&x, NULL);

    std::cout << "lastx " << lastX << std::endl;
    if (lastX <= 0)
        glfwSetCursorPos(window, x, lastY);
    else if (lastX >= x)
        glfwSetCursorPos(window, 0, lastY);
    
    if (camera->pitch > 89.0f)
        camera->pitch = 89.0f;
    if (camera->pitch < -89.0f)
        camera->pitch = -89.0f;
    
    glfwGetCursorPos(window, &x, NULL);
    std::cout << x << std::endl;
    
    vec3 front;
    float pitch = camera->pitch * M_PI / 180.0f;
    float yaw = camera->yaw * M_PI / 180.0f;
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

    vec3 step, perp;
    vec3_scale(step, camera->dir, engine->getDeltaTime()*32.0f);

    if (key == VOX_KEY_ESCAPE && action == VOX_PRESS)
        engine->closeWindow();
    if (engine->isKeyDown(VOX_KEY_SPACE))
        std::cout << "Space key pressed" << std::endl;
    if (engine->isKeyDown(VOX_KEY_W))
        vec3_add(camera->pos, camera->pos, step);
    if (engine->isKeyDown(VOX_KEY_A))
    {
        vec3_mul_cross(perp, step, camera->up);
        vec3_sub(camera->pos, camera->pos, perp);
    }
    if (engine->isKeyDown(VOX_KEY_S))
        vec3_sub(camera->pos, camera->pos, step);
    if (engine->isKeyDown(VOX_KEY_D))
    {
        vec3_mul_cross(perp, step, camera->up);
        vec3_add(camera->pos, camera->pos, perp);
    }
    if (engine->isKeyDown(VOX_KEY_RIGHT))
        rotation -= engine->getDeltaTime()*64000.0f;
    if (engine->isKeyDown(VOX_KEY_LEFT))
        rotation += engine->getDeltaTime()*64000.0f;
}
 
int main(void)
{

    // Engine::setSetting(VOX_FULLSCREEN, true);
    
    Engine *engine = Engine::initEngine(1920, 1080, "Hello World", false);
    if (!engine)
    {
        return (EXIT_FAILURE);
    }

    engine->setErrorCallback(error_callback);
    engine->setKeyCallback(key_callback);
    engine->setCursorPosCallback(mouse_callback);
    while (!glfwWindowShouldClose(engine->window))
	{

        engine->setFrameTime();
		engine->renderer->render();
		glfwSwapBuffers(engine->window);
		glfwPollEvents();
	}
    engine->terminateEngine();
    delete engine;

    exit(EXIT_SUCCESS);
}