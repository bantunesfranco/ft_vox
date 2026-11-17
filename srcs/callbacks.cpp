#include "defines.hpp"
#include "Camera.hpp"
#include "App.hpp"

#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>  // For glm::radians

void error_callback(int error, const char* description)
{
    (void)error;
    fprintf(stderr, "Error: %s\n", description);
}

void scroll_callback(GLFWwindow* window, [[maybe_unused]] double xoffset, double yoffset) {
    App* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->isInGame())
        return;
    if (app->wantsMouseCapture())
        return;

    const int delta = static_cast<int>(std::round(yoffset));
    if (delta == 0)
        return;

    app->cycleHotbar(-delta);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    const auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->camera)
        return;

    static double lastX = 0.0;
    static double lastY = 0.0;
    static bool wasInGame = false;
    if (!app->isInGame()) {
        lastX = xpos;
        lastY = ypos;
        wasInGame = false;
        return;
    }
    if (!wasInGame) {
        lastX = xpos;
        lastY = ypos;
        wasInGame = true;
        return;
    }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    constexpr double sensitivity = 0.025f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    const auto& camera = app->camera;

    camera->yaw   += static_cast<float>(xoffset);
    camera->pitch += static_cast<float>(yoffset);

    if (camera->pitch > 89.0f)
        camera->pitch = 89.0f;
    if (camera->pitch < -89.0f)
        camera->pitch = -89.0f;

    camera->dir = lookDirection(camera->yaw, camera->pitch);
}

void mouse_button_callback(GLFWwindow* window, const int button, const int action, int mods)
{
    (void)mods;
    if (action != GLFW_PRESS)
        return;

    const auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;

    if (app->isInGame() && !app->wantsMouseCapture()) {
        if (button == VOX_MOUSE_BUTTON_LEFT)
            app->destroyBlock();
        else if (button == VOX_MOUSE_BUTTON_RIGHT)
            app->placeBlock();
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    const auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;

    const bool gameInput =
        app->isInGame()
        && !app->wantsKeyboardCapture();

    if (key == VOX_KEY_ESCAPE && action == VOX_PRESS) {
        app->togglePauseMenu();
    } else if (key == VOX_KEY_E && action == VOX_PRESS) {
        app->toggleInventoryMenu();
    } else if (key == VOX_KEY_F11 && action == VOX_PRESS) {
        app->toggleFullscreen();
    } else if (gameInput && key >= VOX_KEY_1 && key <= VOX_KEY_9 && action == VOX_PRESS) {
        app->selectHotbarSlot(static_cast<std::uint16_t>(key - VOX_KEY_1));
    } else if (gameInput && key == VOX_KEY_SPACE && action == VOX_PRESS) {
        app->jump();
    } else if (gameInput && key == VOX_KEY_W && action == VOX_PRESS) {
        app->forwardPressed();
    } else if (gameInput && key == VOX_KEY_X && action == VOX_PRESS) {
        app->toggleWireframeDebug();
    } else if (gameInput && key == VOX_KEY_Z && action == VOX_PRESS) {
        app->toggleFlyMode();
    }
}
