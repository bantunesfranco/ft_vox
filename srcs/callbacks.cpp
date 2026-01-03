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
    static double lastX = 0.0f;
    static double lastY = 0.0f;

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    constexpr double sensitivity = 0.015f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    const auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    const auto& camera = app->camera;

    camera->yaw   += static_cast<float>(xoffset);
    camera->pitch += static_cast<float>(yoffset);

    if (camera->pitch > 89.0f)
        camera->pitch = 89.0f;
    if (camera->pitch < -89.0f)
        camera->pitch = -89.0f;

    const float pitch = glm::radians(camera->pitch);
    const float yaw = glm::radians(camera->yaw);
    const glm::vec3 dir{cosf(yaw) * cosf(pitch),sinf(pitch),sinf(yaw) * cosf(pitch)};
    camera->dir = glm::normalize(dir);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    const auto app = static_cast<App*>(glfwGetWindowUserPointer(window));

    if (key == VOX_KEY_ESCAPE && action == VOX_PRESS)
    {
        app->closeWindow();
    }
    else if (key == VOX_KEY_X && action == VOX_PRESS)
    {
        app->_showWireframe = !app->_showWireframe;
        App::toggleWireframe(app->_showWireframe);
        std::cout << "Toggle wireframe" << std::endl;
    }
    else if (key == VOX_KEY_Z && action == VOX_PRESS)
    {
        static bool mouseMode = false;
        glfwSetInputMode(window, GLFW_CURSOR, mouseMode? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        mouseMode = !mouseMode;
    }
}
