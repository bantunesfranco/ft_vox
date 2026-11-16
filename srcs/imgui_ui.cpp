#include <cstdio>
#include <algorithm>
#include <cstdint>

#include "ImGuiUI.hpp"
#include "Block.hpp"
#include "BlockTextures.hpp"
#include "Item.hpp"

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

struct AtlasRect { float x; float y; float width; float height; };

ImTextureID textureId(const uintptr_t texture) {
    return ImTextureID{texture};
}

void addAtlasImage(ImDrawList* drawList, const GLuint texture, const AtlasRect rect,
    const float atlasSize, const ImVec2 min, const ImVec2 max) {
    const ImVec2 uv0(rect.x / atlasSize, rect.y / atlasSize);
    const ImVec2 uv1((rect.x + rect.width) / atlasSize, (rect.y + rect.height) / atlasSize);
    drawList->AddImage(textureId(texture), min, max, uv0, uv1);
}

AtlasRect itemIcon(const ItemId item) {
    switch (item) {
        case ItemId::GrassBlock: return {1408.0f, 528.0f, 16.0f, 16.0f};
        case ItemId::Dirt: return {1168.0f, 720.0f, 16.0f, 16.0f};
        case ItemId::Stone: return {1680.0f, 1312.0f, 16.0f, 16.0f};
        case ItemId::Sand: return {1440.0f, 1216.0f, 16.0f, 16.0f};
        case ItemId::AmethystBlock: return {512.0f, 288.0f, 16.0f, 16.0f};
        case ItemId::Snow: return {672.0f, 1312.0f, 16.0f, 16.0f};
        default: break;
    }
    // Blocks with generated textures: their side texture in the atlas.
    static const BlockRegistry registry;
    const BlockId block = ItemRegistry::get(item).placedBlock;
    const std::uint16_t texture = registry.get(block).textureSide;
    if (texture >= BlockTextures::FIRST_GENERATED) {
        const auto spot = BlockTextures::atlasSpot(texture);
        return {static_cast<float>(spot.x), static_cast<float>(spot.y), 16.0f, 16.0f};
    }
    return {};
}

void drawStack(const UiItemStack& stack, const ImVec2 min, const float slotSize,
    const GLuint blockAtlas, ImDrawList* drawList) {
    if (stack.empty())
        return;
    const float itemSize = slotSize * 0.72f;
    const float inset = (slotSize - itemSize) * 0.5f;
    const ImVec2 itemMin(min.x + inset, min.y + inset);
    const ImVec2 itemMax(itemMin.x + itemSize, itemMin.y + itemSize);
    addAtlasImage(drawList, blockAtlas, itemIcon(stack.item), 2048.0f, itemMin, itemMax);
    if (stack.count > 1) {
        char countText[8];
        std::snprintf(countText, sizeof(countText), "%u", static_cast<unsigned>(stack.count));
        const ImVec2 textSize = ImGui::CalcTextSize(countText);
        drawList->AddText(
            ImVec2(min.x + slotSize - textSize.x - 3.0f, min.y + slotSize - textSize.y - 1.0f),
            IM_COL32(255, 255, 255, 255),
            countText
        );
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%.*s", static_cast<int>(stack.name.size()), stack.name.data());
}

bool drawMenuButton(const GLuint guiAtlas, const char* label, const char* id,
    const ImVec2 size, const bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::EndDisabled();

    AtlasRect buttonRect{607.0f, 513.0f, 200.0f, 20.0f};
    if (!enabled)
        buttonRect = {809.0f, 513.0f, 200.0f, 20.0f};
    else if (hovered || active)
        buttonRect = {1.0f, 535.0f, 200.0f, 20.0f};

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    addAtlasImage(drawList, guiAtlas, buttonRect, 1024.0f, min, max);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text),
        label
    );
    return enabled && clicked;
}

UiAction inventoryClickAction(const int slot, const bool secondary) {
    UiAction action{UiActionType::INVENTORY_SLOT_CLICK, false, {}};
    action.slot = slot;
    action.secondaryClick = secondary;
    return action;
}

void renderStartMenu(const GLuint guiAtlas, std::vector<UiAction>& actions) {
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
    const float title_width = ImGui::CalcTextSize("MINECRAFT C++").x;
    ImGui::SetCursorPosX((io.DisplaySize.x - title_width) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "MINECRAFT C++"); // Yellow title
    ImGui::PopFont();

    // Menu Buttons Column
    constexpr float button_height = 40.0f;
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.45f);

    auto centeredButton = [&](const char* label, const bool enabled = true) {
        ImGui::SetCursorPosX(std::max(16.0f, (io.DisplaySize.x - button_width) * 0.5f));
        return drawMenuButton(guiAtlas, label, label, ImVec2(button_width, button_height), enabled);
    };

    if (centeredButton("Singleplayer")) {
        actions.push_back({UiActionType::OPEN_WORLDS, false, {}});
    }
    ImGui::Spacing();
    if (centeredButton("Multiplayer")) {
        actions.push_back({UiActionType::START_GAME_MP, false, {}});
    }
    ImGui::Spacing();
    centeredButton("Options...", false);
    ImGui::Spacing();
    if (centeredButton("Quit Game"))
        actions.push_back({UiActionType::QUIT_GAME, false, {}});

    ImGui::End();
    ImGui::PopStyleColor();
}

void renderPauseMenu(const GLuint guiAtlas, std::vector<UiAction>& actions) {
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

    auto pauseButton = [&](const char* label, const char* id, float width = 400.0f, bool enabled = true) {
        width = std::max(1.0f, std::min(width, menu_width - 16.0f));
        ImGui::SetCursorPosX((menu_width - width) * 0.5f);
        return drawMenuButton(guiAtlas, label, id, ImVec2(width, 40.0f), enabled);
    };

    constexpr float gap = 8.0f;
    const float half_width = std::max(1.0f, (std::min(400.0f, menu_width - 16.0f) - gap) * 0.5f);
    auto halfButton = [&](const char* label, const char* id, bool enabled = true) {
        ImGui::SetCursorPosX((menu_width - half_width * 2.0f - gap) * 0.5f);
        return drawMenuButton(guiAtlas, label, id, ImVec2(half_width, 40.0f), enabled);
    };

    if (pauseButton("Back to Game", "resume"))
        actions.push_back({UiActionType::RESUME_GAME, false, {}});
    ImGui::Spacing();

    // Side-by-side row: two half-width buttons, the pair centered.
    halfButton("Achievements", "achievements", false);
    ImGui::SameLine(0.0f, gap);
    drawMenuButton(guiAtlas, "Statistics", "statistics", ImVec2(half_width, 40.0f), false);

    ImGui::Spacing();
    pauseButton("Options...", "options", 400.0f, false);
    ImGui::Spacing();
    if (pauseButton("Save and Quit to Title", "title"))
        actions.push_back({UiActionType::RETURN_TO_TITLE, false, {}});

    ImGui::End();
}


void renderMultiplayerMenu(const GLuint guiAtlas, std::array<char, 128>& address,
    std::array<char, 33>& playerName, const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    const float button_width = std::max(1.0f, std::min(400.0f, io.DisplaySize.x - 32.0f));

    // Fullscreen Main Window (Simulates dirt background tile)

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(32, 32, 32, 255)); // Dark dirt tint fallback

    ImGui::Begin("MultiplayerMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    // Game Title / Logo Layout
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.15f);
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Fallback safely to default loaded font array entry
    float title_width = ImGui::CalcTextSize("Multiplayer").x;
    ImGui::SetCursorPosX((io.DisplaySize.x - title_width) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Multiplayer"); // Yellow title
    ImGui::PopFont();

    // Name / address form
    constexpr float button_height = 40.0f;
    const float column_x = std::max(16.0f, (io.DisplaySize.x - button_width) * 0.5f);
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.32f);

    ImGui::SetCursorPosX(column_x);
    ImGui::TextUnformatted("Player name");
    ImGui::SetCursorPosX(column_x);
    ImGui::SetNextItemWidth(button_width);
    ImGui::InputText("##playerName", playerName.data(), playerName.size());

    ImGui::SetCursorPosX(column_x);
    ImGui::TextUnformatted("Server address (host or host:port)");
    ImGui::SetCursorPosX(column_x);
    ImGui::SetNextItemWidth(button_width);
    ImGui::InputText("##address", address.data(), address.size());

    if (!model.lastError.empty()) {
        ImGui::SetCursorPosX(column_x);
        ImGui::PushTextWrapPos(column_x + button_width);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", model.lastError.c_str());
        ImGui::PopTextWrapPos();
    }

    // Menu Buttons Column
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    auto centeredButton = [&](const char* label, const bool enabled = true) {
        ImGui::SetCursorPosX(column_x);
        return drawMenuButton(guiAtlas, label, label, ImVec2(button_width, button_height), enabled);
    };

    const bool hasName = playerName[0] != '\0';
    ImGui::SetCursorPosX(column_x);
    if (model.hostWorldName.empty())
        ImGui::TextDisabled("Host: no world selected (choose one in Singleplayer)");
    else
        ImGui::Text("Host: %s (change it in Singleplayer)", model.hostWorldName.c_str());
    if (centeredButton("Host", hasName && !model.hostWorldName.empty())) {
        UiAction action{UiActionType::HOST_MP, false, {}};
        action.text2 = playerName.data();
        actions.push_back(std::move(action));
    }
    ImGui::Spacing();
    if (centeredButton("Join", hasName && address[0] != '\0')) {
        UiAction action{UiActionType::JOIN_MP, false, {}};
        action.text2 = playerName.data();
        action.text1 = address.data();
        actions.push_back(std::move(action));
    }
    ImGui::Spacing();
    if (centeredButton("Return to Title"))
        actions.push_back({UiActionType::RETURN_TO_TITLE, false, {}});

    ImGui::End();
    ImGui::PopStyleColor();
}

// Full-screen menu background + centered yellow title; End with endMenuScreen().
void beginMenuScreen(const char* id, const char* title) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(32, 32, 32, 255));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::SetCursorPosY(io.DisplaySize.y * 0.08f);
    ImGui::SetCursorPosX((io.DisplaySize.x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%s", title);
}

void endMenuScreen() {
    ImGui::End();
    ImGui::PopStyleColor();
}

void renderMenuError(const UiFrameModel& model, const float x, const float width) {
    if (model.lastError.empty())
        return;
    ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(x + width);
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", model.lastError.c_str());
    ImGui::PopTextWrapPos();
}

void renderWorldSelect(const GLuint guiAtlas, const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    const float width = std::max(1.0f, std::min(600.0f, io.DisplaySize.x - 32.0f));
    const float x = std::max(16.0f, (io.DisplaySize.x - width) * 0.5f);
    constexpr float gap = 8.0f;
    const float half = (width - gap) * 0.5f;
    constexpr float rowHeight = 40.0f;

    beginMenuScreen("WorldSelect", "Select World");

    // The list: name on top, folder and seed below, damaged worlds greyed out.
    ImGui::SetCursorPos(ImVec2(x, io.DisplaySize.y * 0.14f));
    const float listHeight = std::max(80.0f, io.DisplaySize.y * 0.56f);
    ImGui::BeginChild("worlds", ImVec2(width, listHeight), ImGuiChildFlags_Borders);
    if (model.worlds.empty())
        ImGui::TextDisabled("No worlds yet. Create one!");
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(model.worlds.size()); ++i) {
        const UiWorldEntry& world = model.worlds[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Selectable("##world", i == model.selectedWorld, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(0.0f, 40.0f))) {
            UiAction select{UiActionType::SELECT_WORLD, false, {}};
            select.slot = i;
            actions.push_back(select);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && world.readable) {
                UiAction play{UiActionType::PLAY_WORLD, false, {}};
                play.slot = i;
                actions.push_back(play);
            }
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImU32 nameColor = world.readable ? IM_COL32(240, 240, 240, 255) : IM_COL32(130, 130, 130, 255);
        drawList->AddText(ImVec2(min.x + 8.0f, min.y + 3.0f), nameColor, world.name.c_str());
        drawList->AddText(ImVec2(min.x + 8.0f, min.y + 21.0f), IM_COL32(150, 150, 150, 255), world.detail.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();

    renderMenuError(model, x, width);

    const bool hasSelection = model.selectedWorld >= 0
        && model.selectedWorld < static_cast<int>(model.worlds.size());
    const bool canPlay = hasSelection && model.worlds[static_cast<std::size_t>(model.selectedWorld)].readable;
    const auto button = [&](const char* label, const float offset, const bool enabled) {
        ImGui::SetCursorPosX(x + offset);
        return drawMenuButton(guiAtlas, label, label, ImVec2(half, rowHeight), enabled);
    };

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (button("Play Selected World", 0.0f, canPlay)) {
        UiAction play{UiActionType::PLAY_WORLD, false, {}};
        play.slot = model.selectedWorld;
        actions.push_back(play);
    }
    ImGui::SameLine(0.0f, gap);
    if (button("Create New World", half + gap, true))
        actions.push_back({UiActionType::OPEN_CREATE_WORLD, false, {}});

    ImGui::Spacing();
    if (button("Delete", 0.0f, hasSelection))
        ImGui::OpenPopup("Delete world?");
    ImGui::SameLine(0.0f, gap);
    if (button("Back", half + gap, true))
        actions.push_back({UiActionType::RETURN_TO_TITLE, false, {}});

    // Deleting cannot be undone: ask first.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete world?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        if (hasSelection)
            ImGui::Text("'%s' will be lost forever! (A long time!)",
                        model.worlds[static_cast<std::size_t>(model.selectedWorld)].name.c_str());
        ImGui::Spacing();
        if (drawMenuButton(guiAtlas, "Delete", "confirm-delete", ImVec2(160.0f, rowHeight), hasSelection)) {
            UiAction remove{UiActionType::DELETE_WORLD, false, {}};
            remove.slot = model.selectedWorld;
            actions.push_back(remove);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, gap);
        if (drawMenuButton(guiAtlas, "Cancel", "cancel-delete", ImVec2(160.0f, rowHeight)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    endMenuScreen();
}

void renderWorldCreate(const GLuint guiAtlas, std::array<char, 64>& name, std::array<char, 64>& seed,
    const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    const float width = std::max(1.0f, std::min(400.0f, io.DisplaySize.x - 32.0f));
    const float x = std::max(16.0f, (io.DisplaySize.x - width) * 0.5f);
    constexpr float gap = 8.0f;
    const float half = (width - gap) * 0.5f;

    beginMenuScreen("WorldCreate", "Create New World");

    ImGui::SetCursorPosY(io.DisplaySize.y * 0.28f);
    ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted("World Name");
    ImGui::SetCursorPosX(x);
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##worldName", name.data(), name.size());

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted("Seed for the World Generator");
    ImGui::SetCursorPosX(x);
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##worldSeed", "Leave blank for a random seed", seed.data(), seed.size());

    renderMenuError(model, x, width);

    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::SetCursorPosX(x);
    if (drawMenuButton(guiAtlas, "Create New World", "create", ImVec2(half, 40.0f))) {
        UiAction create{UiActionType::CREATE_WORLD, false, {}};
        create.text1 = name.data();
        create.text2 = seed.data();
        actions.push_back(std::move(create));
        seed.fill('\0');                    // the next world gets a fresh random seed by default
    }
    ImGui::SameLine(0.0f, gap);
    if (drawMenuButton(guiAtlas, "Cancel", "cancel", ImVec2(half, 40.0f)))
        actions.push_back({UiActionType::OPEN_WORLDS, false, {}});

    endMenuScreen();
}

void renderInventory(const GLuint guiAtlas, const GLuint blockAtlas,
    const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, 128));

    ImVec2 inv_size = ImVec2(
        std::max(1.0f, std::min(600.0f, io.DisplaySize.x - 24.0f)),
        std::max(1.0f, std::min(300.0f, io.DisplaySize.y - 24.0f))
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

    const float slot_dim = std::max(1.0f, std::min(52.0f, (inv_size.x - 40.0f) / 9.0f));

    auto drawItemSlot = [&](const char* slot_id, const int slotIndex) {
        ImVec2 p_min = ImGui::GetCursorScreenPos();

        const bool leftClicked = ImGui::InvisibleButton(slot_id, ImVec2(slot_dim, slot_dim));
        const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const bool pressed = ImGui::IsItemActive();
        const ImVec2 slotMax(p_min.x + slot_dim, p_min.y + slot_dim);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        addAtlasImage(drawList, guiAtlas, {597.0f, 281.0f, 18.0f, 18.0f},
            1024.0f, p_min, slotMax);
        if (ImGui::IsItemHovered() || pressed)
            drawList->AddRect(p_min, slotMax, IM_COL32(255, 255, 255, pressed ? 220 : 120));
        drawStack(model.inventorySlots[static_cast<std::size_t>(slotIndex)], p_min, slot_dim,
            blockAtlas, drawList);
        if (leftClicked)
            actions.push_back(inventoryClickAction(slotIndex, false));
        else if (rightClicked)
            actions.push_back(inventoryClickAction(slotIndex, true));
    };

    // 1. Storage Inventory Grid: 3 rows of 9 slots
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 9; c++) {
            char id[32]; std::snprintf(id, sizeof(id), "##inv_%d_%d", r, c);
            drawItemSlot(id, 9 + r * 9 + c);
            if (c < 8) ImGui::SameLine(0.0f, 1.0f);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);
    ImGui::TextColored(ImVec4(0.25f, 0.25f, 0.25f, 1.0f), "Hotbar");

    // 2. Lower Equipped Hotbar Grid Row
    for (int c = 0; c < 9; c++) {
        char id[32]; std::snprintf(id, sizeof(id), "##hot_%d", c);
        drawItemSlot(id, c);
        if (c < 8) ImGui::SameLine(0.0f, 1.0f);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    if (!model.cursorStack.empty()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* foreground = ImGui::GetForegroundDrawList();
        foreground->AddRectFilled(mouse, ImVec2(mouse.x + 34.0f, mouse.y + 34.0f), IM_COL32(70, 70, 70, 235));
        const float inset = 8.0f;
        const ImVec2 iconMin(mouse.x + inset, mouse.y + inset);
        const AtlasRect icon = itemIcon(model.cursorStack.item);
        addAtlasImage(foreground, blockAtlas, icon, 2048.0f, iconMin,
            ImVec2(mouse.x + 34.0f - inset, mouse.y + 34.0f - inset));
        if (model.cursorStack.count > 1) {
            char countText[8];
            std::snprintf(countText, sizeof(countText), "%u", static_cast<unsigned>(model.cursorStack.count));
            foreground->AddText(ImVec2(mouse.x + 18.0f, mouse.y + 20.0f), IM_COL32_WHITE, countText);
        }
    }
}

void renderHudHotbar(const GLuint guiAtlas, const GLuint blockAtlas, const UiFrameModel& model) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 hotbarSize(364.0f, 44.0f);
    const ImVec2 hotbarMin((io.DisplaySize.x - hotbarSize.x) * 0.5f,
        io.DisplaySize.y - hotbarSize.y - 10.0f);
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    addAtlasImage(drawList, guiAtlas, {714.0f, 1.0f, 182.0f, 22.0f},
        1024.0f, hotbarMin, ImVec2(hotbarMin.x + hotbarSize.x, hotbarMin.y + hotbarSize.y));

    const float pitch = hotbarSize.x / 9.0f;
    for (int i = 0; i < 9; ++i) {
        const float centerX = hotbarMin.x + pitch * (static_cast<float>(i) + 0.5f);
        if (i == model.selectedHotbarSlot) {
            const ImVec2 selectionMin(centerX - 24.0f, hotbarMin.y - 2.0f);
            addAtlasImage(drawList, guiAtlas, {540.0f, 262.0f, 24.0f, 23.0f},
                1024.0f, selectionMin, ImVec2(selectionMin.x + 48.0f, selectionMin.y + 46.0f));
        }
        const ImVec2 slotMin(centerX - 16.0f, hotbarMin.y + 6.0f);
        drawStack(model.inventorySlots[static_cast<std::size_t>(i)], slotMin, 32.0f,
            blockAtlas, drawList);
    }
}

void renderCrossHair(const GLuint guiAtlas) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
    constexpr float size = 15.0f;
    addAtlasImage(drawList, guiAtlas, {714.0f, 266.0f, 15.0f, 15.0f}, 1024.0f,
        ImVec2(center.x - size * 0.5f, center.y - size * 0.5f),
        ImVec2(center.x + size * 0.5f, center.y + size * 0.5f));
}

// Names over other players: a translucent plate with the text centered on
// the projected head position, fading out with distance.
void renderNameTags(const std::vector<UiNameTag>& tags) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();   // under menus, over the world
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    for (const UiNameTag& tag : tags) {
        const float fade = std::clamp(1.0f - (tag.distance - 32.0f) / 32.0f, 0.25f, 1.0f);
        const ImVec2 size = ImGui::CalcTextSize(tag.name.c_str());
        const ImVec2 anchor((tag.ndcX * 0.5f + 0.5f) * display.x,
                            (0.5f - tag.ndcY * 0.5f) * display.y);  // NDC +y is up, pixels +y is down
        const ImVec2 textPos(anchor.x - size.x * 0.5f, anchor.y - size.y);
        const auto alpha = [fade](const float a) { return static_cast<int>(a * fade); };

        drawList->AddRectFilled(ImVec2(textPos.x - 4.0f, textPos.y - 2.0f),
            ImVec2(textPos.x + size.x + 4.0f, textPos.y + size.y + 2.0f),
            IM_COL32(0, 0, 0, alpha(110.0f)), 2.0f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, alpha(255.0f)), tag.name.c_str());
    }
}

void renderStatus(const std::string& text) {
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pos((displaySize.x - size.x) * 0.5f, displaySize.y * 0.40f);
    drawList->AddRectFilled(ImVec2(pos.x - 12.0f, pos.y - 8.0f),
        ImVec2(pos.x + size.x + 12.0f, pos.y + size.y + 8.0f), IM_COL32(0, 0, 0, 160), 4.0f);
    drawList->AddText(pos, IM_COL32(255, 255, 255, 255), text.c_str());
}

void renderDebugWindow(const UiFrameModel& model, std::vector<UiAction>& actions) {
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::Begin("Debug Window");
    ImGui::Text("FPS: %i", model.fps);
    if (!model.seedText.empty())
        ImGui::Text("Seed: %s", model.seedText.c_str());
    ImGui::Text("Current Camera Position: (%.2f, %.2f, %.2f)",
        model.cameraPosition[0], model.cameraPosition[1], model.cameraPosition[2]);

    if (!model.players.empty()) {
        ImGui::Text("Players online: %zu", model.players.size() + 1);
        for (const std::string& name : model.players)
            ImGui::BulletText("%s", name.c_str());
    }

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

ImGuiUI::ImGuiUI(GLFWwindow* window, const GLuint guiTexture, const GLuint blockTexture)
	: guiAtlas(guiTexture), blockAtlas(blockTexture) {
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
        renderStartMenu(guiAtlas, actions);
        return actions;
    }
    if (model.state == GameState::MULTIPLAYER) {
        renderMultiplayerMenu(guiAtlas, joinAddress, playerName, model, actions);
        return actions;
    }
    if (model.state == GameState::WORLD_SELECT) {
        renderWorldSelect(guiAtlas, model, actions);
        return actions;
    }
    if (model.state == GameState::WORLD_CREATE) {
        renderWorldCreate(guiAtlas, newWorldName, newWorldSeed, model, actions);
        return actions;
    }

    renderNameTags(model.nameTags);

    if (model.state == GameState::PAUSE)
        renderPauseMenu(guiAtlas, actions);
    else if (model.state == GameState::INVENTORY)
        renderInventory(guiAtlas, blockAtlas, model, actions);
    else {
        renderHudHotbar(guiAtlas, blockAtlas, model);
        renderCrossHair(guiAtlas);
    }

    if (!model.statusText.empty())
        renderStatus(model.statusText);
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

std::unique_ptr<UI> createImGuiUI(GLFWwindow* window, const GLuint guiAtlas, const GLuint blockAtlas) {
	return std::make_unique<ImGuiUI>(window, guiAtlas, blockAtlas);
}
