#include "defines.hpp"
#include "Camera.hpp"
#include "App.hpp"
#include <iostream>

void error_callback(int error, const char* description)
{
	(void)error;
	fprintf(stderr, "Error: %s\n", description);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
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

    App* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    Camera* camera = app->camera;

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

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)scancode;
	(void)mods;

	App* app = static_cast<App*>(glfwGetWindowUserPointer(window));
	Camera* camera = app->camera;

	vec3 step, perp;
	vec3_scale(step, camera->dir, app->getDeltaTime()*32.0f);

	if (key == VOX_KEY_ESCAPE && action == VOX_PRESS)
	{
		app->closeWindow();
		return;
	}
	if (key == VOX_KEY_X && action == VOX_PRESS)
	{
		app->_showWireframe = !app->_showWireframe;
		std::cout << "Toggle wireframe" << std::endl;
	}
	if (app->isKeyDown(VOX_KEY_SPACE))
		std::cout << "Space key pressed" << std::endl;
	if (app->isKeyDown(VOX_KEY_W))
		vec3_add(camera->pos, camera->pos, step);
	if (app->isKeyDown(VOX_KEY_A))
	{
		vec3_mul_cross(perp, step, camera->up);
		vec3_sub(camera->pos, camera->pos, perp);
	}
	if (app->isKeyDown(VOX_KEY_S))
		vec3_sub(camera->pos, camera->pos, step);
	if (app->isKeyDown(VOX_KEY_D))
	{
		vec3_mul_cross(perp, step, camera->up);
		vec3_add(camera->pos, camera->pos, perp);
	}
	if (app->isKeyDown(VOX_KEY_UP))
	{
		camera->pitch += 1.0f;
		if (camera->pitch > 89.0f)
			camera->pitch = 89.0f;
	}
	if (app->isKeyDown(VOX_KEY_DOWN))
	{
		camera->pitch -= 1.0f;
		if (camera->pitch < -89.0f)
			camera->pitch = -89.0f;
	}
	if (app->isKeyDown(VOX_KEY_LEFT))
		camera->yaw -= 1.0f;
	if (app->isKeyDown(VOX_KEY_RIGHT))
		camera->yaw += 1.0f;
	
	vec3 dir;
	float pitch = DEG2RAD(camera->pitch);
    float yaw = DEG2RAD(camera->yaw);
    dir[0] = cosf(yaw) * cosf(pitch);
    dir[1] = sinf(pitch);
    dir[2] = sinf(yaw) * cosf(pitch);
    vec3_norm(camera->dir, dir);
}




