
#include "Renderer.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <iostream>
#include "Camera.hpp"
#include "Engine.hpp"



void setupImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);

    // ImGui::StyleColorsDark();
}

void renderImGui(FPSCounter* fpsCounter, Camera *camera, bool showWireframe)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Create a simple ImGui window
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %i", fpsCounter->getFPS());
    ImGui::Text("Current Camera Position: (%.2f, %.2f, %.2f)", camera->pos[0], camera->pos[1], camera->pos[2]);
    ImGui::Checkbox("Wireframe", &showWireframe);
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
