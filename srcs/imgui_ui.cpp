#include <cstdio>
#include <algorithm>

#include "ImGuiUI.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stdexcept>

namespace {
void applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // 1. Force sharp pixel edges
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;

    // 2. Chunky, integer-aligned sizing (assumes a 2x or 3x pixel scale)
    style.WindowBorderSize  = 2.0f;
    style.FrameBorderSize   = 2.0f;
    style.PopupBorderSize   = 2.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(4.0f, 4.0f);

    // 3. Minecraft Color Scheme
    ImVec4* colors = style.Colors;

    // Window / Dialogue Background (The classic dark screen tint)
    colors[ImGuiCol_WindowBg]          = ImVec4(0.06f, 0.06f, 0.06f, 0.85f); 
    colors[ImGuiCol_PopupBg]           = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);

    // Standard Buttons (Chunky gray block)
    colors[ImGuiCol_Button]            = ImVec4(0.43f, 0.43f, 0.43f, 1.00f); // Minecraft button gray
    colors[ImGuiCol_ButtonHovered]     = ImVec4(0.53f, 0.58f, 0.70f, 1.00f); // Light blue-gray highlight
    colors[ImGuiCol_ButtonActive]      = ImVec4(0.33f, 0.33f, 0.33f, 1.00f); // Dark pressed state

    // Borders (The dark outlinings)
    colors[ImGuiCol_Border]            = ImVec4(0.00f, 0.00f, 0.00f, 1.00f); // Solid black outlines
    colors[ImGuiCol_BorderShadow]      = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);

    // Text (White with Minecraft's distinct drop shadow effect)
    colors[ImGuiCol_Text]              = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
}

void DrawMinecraftBevel(ImVec2 min_p, ImVec2 max_p, bool pressed, float scale = 2.0f) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Core pixel colors
    ImU32 black  = IM_COL32(0, 0, 0, 255);
    ImU32 shadow = pressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(55, 55, 55, 255);  // #373737
    ImU32 light  = pressed ? IM_COL32(55, 55, 55, 255)   : IM_COL32(255, 255, 255, 255); // #FFFFFF

    // Outer Black Border
    draw_list->AddRect(min_p, max_p, black, 0.0f, 0, scale);

    // Inner Bevels (Top/Left and Bottom/Right)
    // Left border
    draw_list->AddLine(ImVec2(min_p.x + scale, min_p.y + scale), ImVec2(min_p.x + scale, max_p.y - scale * 2), light, scale);
    // Top border
    draw_list->AddLine(ImVec2(min_p.x + scale, min_p.y + scale), ImVec2(max_p.x - scale * 2, min_p.y + scale), light, scale);
    // Right border
    draw_list->AddLine(ImVec2(max_p.x - scale, min_p.y + scale), ImVec2(max_p.x - scale, max_p.y - scale), shadow, scale);
    // Bottom border
    draw_list->AddLine(ImVec2(min_p.x + scale, max_p.y - scale), ImVec2(max_p.x - scale, max_p.y - scale), shadow, scale);
}

void renderStartMenu(std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    const float button_width = std::max(1.0f, std::min(400.0f, io.DisplaySize.x - 32.0f));

    // Fullscreen Main Window (Simulates dirt background tile)

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(32, 32, 32, 255)); // Dark dirt tint fallback

    ImGui::Begin("StartMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    // Game Title / Logo Layout
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.15f);
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Fallback safely to default loaded font array entry
    float title_width = ImGui::CalcTextSize("MINECRAFT").x;
    ImGui::SetCursorPosX((io.DisplaySize.x - title_width) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "MINECRAFT C++"); // Yellow title
    ImGui::PopFont();

    // Menu Buttons Column
    constexpr float button_height = 40.0f;
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.45f);

    auto CenteredButton = [&](const char* label) {
        ImGui::SetCursorPosX(std::max(16.0f, (io.DisplaySize.x - button_width) * 0.5f));
        ImVec2 p_min = ImGui::GetCursorScreenPos();

        bool clicked = ImGui::Button(label, ImVec2(button_width, button_height));
        DrawMinecraftBevel(p_min, ImVec2(p_min.x + button_width, p_min.y + button_height), ImGui::IsItemActive());
        return clicked;
    };

    if (CenteredButton("Singleplayer")) {
        actions.push_back({UiActionType::START_GAME, false, {}});
    }
    ImGui::Spacing();
    ImGui::BeginDisabled();
    CenteredButton("Multiplayer");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::BeginDisabled();
    CenteredButton("Options...");
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (CenteredButton("Quit Game"))
        actions.push_back({UiActionType::QUIT_GAME});

    ImGui::End();
    ImGui::PopStyleColor();
}

void renderPauseMenu(std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    const float menu_width = std::max(24.0f, std::min(420.0f, io.DisplaySize.x - 24.0f));

    // Fullscreen semi-transparent black overlay tint
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 16, 16, 180));

    ImGui::Begin("PauseOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
    ImGui::End();
    ImGui::PopStyleColor();

    // Centered Interactive Dialogue Box
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(menu_width,
        std::max(24.0f, std::min(300.0f, io.DisplaySize.y - 24.0f))));

    ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);

    // Title text
    float text_w = ImGui::CalcTextSize("Game Paused").x;
    ImGui::SetCursorPosX((menu_width - text_w) * 0.5f);
    ImGui::Text("Game Paused");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);

    auto PauseButton = [&](const char* label, float width = 400.0f) {
        width = std::max(1.0f, std::min(width, menu_width - 16.0f));
        ImGui::SetCursorPosX((menu_width - width) * 0.5f);
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::Button(label, ImVec2(width, 40.0f));
        DrawMinecraftBevel(p_min, ImVec2(p_min.x + width, p_min.y + 40.0f), ImGui::IsItemActive());
        return clicked;
    };

    if (PauseButton("Back to Game"))
        actions.push_back({UiActionType::RESUME_GAME, false, {}});
    ImGui::Spacing();

    // Side-by-side buttons row
    const float half_button_width = std::max(1.0f, (menu_width - 24.0f) * 0.5f);
    ImGui::SetCursorPosX(8.0f);
    ImVec2 p_min1 = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled();
    ImGui::Button("Achievements", ImVec2(half_button_width, 40.0f));
    DrawMinecraftBevel(p_min1, ImVec2(p_min1.x + half_button_width, p_min1.y + 40.0f), ImGui::IsItemActive());

    ImGui::SameLine(16.0f + half_button_width);
    ImVec2 p_min2 = ImGui::GetCursorScreenPos();
    ImGui::Button("Statistics", ImVec2(half_button_width, 40.0f));
    DrawMinecraftBevel(p_min2, ImVec2(p_min2.x + half_button_width, p_min2.y + 40.0f), ImGui::IsItemActive());
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::BeginDisabled();
    PauseButton("Options...");
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (PauseButton("Save and Quit to Title"))
        actions.push_back({UiActionType::RETURN_TO_TITLE, false, {}});

    ImGui::End();
}

void renderInventory() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, 128));

    ImVec2 inv_size = ImVec2(
        std::max(1.0f, std::min(372.0f, io.DisplaySize.x - 24.0f)),
        std::max(1.0f, std::min(350.0f, io.DisplaySize.y - 24.0f))
    );
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(inv_size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(198, 198, 198, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

    ImGui::Begin("Inventory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    ImGui::TextColored(ImVec4(0.25f, 0.25f, 0.25f, 1.0f), "Inventory");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);

    const float slot_dim = std::max(1.0f, std::min(36.0f, (inv_size.x - 40.0f) / 9.0f));

    auto DrawItemSlot = [&](const char* slot_id) {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + slot_dim, p_min.y + slot_dim);

        ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(139, 139, 139, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 60));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 100));

        ImGui::Button(slot_id, ImVec2(slot_dim, slot_dim));
        const bool pressed = ImGui::IsItemActive();

        ImGui::PopStyleColor(3);
        DrawMinecraftBevel(p_min, p_max, pressed);
    };

    // 1. Storage Inventory Grid: 3 rows of 9 slots
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 9; c++) {
            char id[32]; std::snprintf(id, sizeof(id), "##inv_%d_%d", r, c);
            DrawItemSlot(id);
            if (c < 8) ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);
    ImGui::TextColored(ImVec4(0.25f, 0.25f, 0.25f, 1.0f), "Hotbar");

    // 2. Lower Equipped Hotbar Grid Row
    for (int c = 0; c < 9; c++) {
        char id[32]; std::snprintf(id, sizeof(id), "##hot_%d", c);
        DrawItemSlot(id);
        if (c < 8) ImGui::SameLine(0.0f, 2.0f);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void renderHudHotbar(int selected_slot_index) {
    ImGuiIO& io = ImGui::GetIO();

    float total_w = 362.0f;
    float slot_w = 38.0f;
    float hud_h = 42.0f;

    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - total_w) * 0.5f, io.DisplaySize.y - hud_h - 10.0f));
    ImGui::SetNextWindowSize(ImVec2(total_w, hud_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(60, 60, 60, 180));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));

    ImGui::Begin("HUDHotbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for (int i = 0; i < 9; i++) {
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + slot_w, p_min.y + slot_w);

        draw_list->AddRect(p_min, p_max, IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);

        if (i == selected_slot_index) {
            ImVec2 active_min = ImVec2(p_min.x - 2.0f, p_min.y - 2.0f);
            ImVec2 active_max = ImVec2(p_max.x + 2.0f, p_max.y + 2.0f);
            draw_list->AddRect(active_min, active_max, IM_COL32(255, 255, 255, 255), 0.0f, 0, 4.0f);
        }

        ImGui::SetCursorScreenPos(ImVec2(p_min.x + slot_w + 2.0f, p_min.y));
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void renderCrossHair() {
    // Draw crosshair at screen center
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
    constexpr float crosshairSize = 10.0f;
    const ImU32 whiteColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    constexpr float lineThickness = 1.5f;

    drawList->AddLine(
        ImVec2(center.x - crosshairSize, center.y),
        ImVec2(center.x + crosshairSize, center.y),
        whiteColor,
        lineThickness
    );
    drawList->AddLine(
        ImVec2(center.x, center.y - crosshairSize),
        ImVec2(center.x, center.y + crosshairSize),
        whiteColor,
        lineThickness
    );
}

void renderDebugWindow(const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %i", model.fps);
    ImGui::Text("Current Camera Position: (%.2f, %.2f, %.2f)",
        model.cameraPosition.x, model.cameraPosition.y, model.cameraPosition.z);

    bool wireframe = model.wireframe;
    if (ImGui::Checkbox("Wireframe", &wireframe))
        actions.push_back({UiActionType::SET_WIREFRAME, wireframe, {}});

    std::array<float, 4> color = model.clearColor;
    if (ImGui::ColorEdit4("Color", color.data())) {
        UiAction action{UiActionType::SET_CLEAR_COLOR, false, {}};
        action.color = color;
        actions.push_back(action);
    }
    ImGui::End();
}
}

ImGuiUI::ImGuiUI(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui GLFW backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui OpenGL backend");
    }
    ImGui::StyleColorsDark();
    applyStyle();
    initialized = true;
}

ImGuiUI::~ImGuiUI() {
    shutdown();
}

void ImGuiUI::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

std::vector<UiAction> ImGuiUI::render(const UiFrameModel& model) {
    std::vector<UiAction> actions;
    if (model.state == GameState::START) {
        renderStartMenu(actions);
        return actions;
    }

    if (model.state == GameState::PAUSE)
        renderPauseMenu(actions);
    else if (model.state == GameState::INVENTORY)
        renderInventory();
    else {
        renderHudHotbar(model.selectedHotbarSlot);
        renderCrossHair();
    }

    if (model.showDebug)
        renderDebugWindow(model, actions);
    return actions;
}

void ImGuiUI::endFrame() {
    ImGui::Render();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    glViewport(0, 0, static_cast<GLsizei>(displaySize.x), static_cast<GLsizei>(displaySize.y));
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiUI::wantsMouseCapture() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiUI::wantsKeyboardCapture() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiUI::shutdown() {
    if (!initialized)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized = false;
}

std::unique_ptr<UI> createImGuiUI(GLFWwindow* window) {
    return std::make_unique<ImGuiUI>(window);
}
