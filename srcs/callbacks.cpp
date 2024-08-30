#include "defines.hpp"
#include "Camera.hpp"
#include "App.hpp"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>  // For glm::radians

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

    glm::vec3 dir;
    float pitch = glm::radians(camera->pitch);
    float yaw = glm::radians(camera->yaw);
    dir.x = cosf(yaw) * cosf(pitch);
    dir.y = sinf(pitch);
    dir.z = sinf(yaw) * cosf(pitch);
    camera->dir = glm::normalize(dir);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    // Get the App instance and Camera
    App* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    Camera* camera = app->camera;

    glm::vec3 step = glm::normalize(camera->dir);  // Ensure direction vector is normalized
    glm::vec3 perp;

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
        camera->pos += step;
    if (app->isKeyDown(VOX_KEY_A))
    {
        perp = glm::normalize(glm::cross(step, camera->up));
        camera->pos -= perp;
    }
    if (app->isKeyDown(VOX_KEY_S))
        camera->pos -= step;
    if (app->isKeyDown(VOX_KEY_D))
    {
        perp = glm::normalize(glm::cross(step, camera->up));
        camera->pos += perp;
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
    
    glm::vec3 dir;
    float pitch = glm::radians(camera->pitch);
    float yaw = glm::radians(camera->yaw);
    dir.x = cosf(yaw) * cosf(pitch);
    dir.y = sinf(pitch);
    dir.z = sinf(yaw) * cosf(pitch);
    camera->dir = glm::normalize(dir);
}
