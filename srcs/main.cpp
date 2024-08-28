/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/08/28 23:36:57 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <glad/gl.h>
#include "Engine.hpp"
#include "Chunk.hpp"
#include "vertex.hpp"
#include <iostream>
#include <vector>

static void error_callback(int error, const char* description)
{
	(void)error;
	fprintf(stderr, "Error: %s\n", description);
}

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static float lastX = 0.0f;
    static float lastY = 0.0f;

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.015f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    Camera* camera = engine->camera;

    camera->yaw   += xoffset;
    camera->pitch += yoffset;

	if (camera->pitch > 89.0f)
		camera->pitch = 89.0f;
	if (camera->pitch < -89.0f)
		camera->pitch = -89.0f;

    vec3 dir;
    float pitch = DEG2RAD(camera->pitch);
    float yaw = DEG2RAD(camera->yaw);
    dir[0] = cosf(yaw) * cosf(pitch);
    dir[1] = sinf(pitch);
    dir[2] = sinf(yaw) * cosf(pitch);
	(void)dir;
    // vec3_norm(camera->dir, dir);
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
	if (engine->isKeyDown(VOX_KEY_UP))
	{
		camera->pitch += 1.0f;
		if (camera->pitch > 89.0f)
			camera->pitch = 89.0f;
	}
	if (engine->isKeyDown(VOX_KEY_DOWN))
	{
		camera->pitch -= 1.0f;
		if (camera->pitch < -89.0f)
			camera->pitch = -89.0f;
	}
	if (engine->isKeyDown(VOX_KEY_LEFT))
		camera->yaw -= 1.0f;
	if (engine->isKeyDown(VOX_KEY_RIGHT))
		camera->yaw += 1.0f;
	
	vec3 dir;
	float pitch = DEG2RAD(camera->pitch);
    float yaw = DEG2RAD(camera->yaw);
    dir[0] = cosf(yaw) * cosf(pitch);
    dir[1] = sinf(pitch);
    dir[2] = sinf(yaw) * cosf(pitch);
    vec3_norm(camera->dir, dir);
}

int main(void)
{

    // Engine::setSetting(VOX_FULLSCREEN, true);

	Engine *engine = Engine::initEngine(1440, 900, "Hello World", false);
	if (!engine)
	{
		return (EXIT_FAILURE);
	}
	
	engine->setErrorCallback(error_callback);
	engine->setKeyCallback(key_callback);
	// (void)mouse_callback;
	// engine->setCursorPosCallback(mouse_callback);

	Renderer *renderer = engine->renderer;
	ChunkManager chunkManager(4242);

	while (!glfwWindowShouldClose(engine->window))
	{
		chunkManager.updateChunks(engine->camera->pos);

		for (auto it = chunkManager.chunks.begin(); it != chunkManager.chunks.end(); ++it)
		{
			Chunk& chunk = chunkManager.loadChunk(it->first);
			std::vector<Vertex> vertices = generateMesh(chunk);
			// std::cout << "Vertices size: " << vertices.size() << std::endl;
			renderer->render(vertices);
		}

		engine->run();
	}
	engine->terminateEngine();
	delete engine;

	exit(EXIT_SUCCESS);
}