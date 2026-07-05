#pragma once
#include "CharacterSelect.hpp"
#include "MessageLog.hpp"
#include "PlayerState.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Window layout data: position and size
struct WindowLayout {
    std::string name;
    float posX = 0.0f;
    float posY = 0.0f;
    float sizeX = 0.0f;
    float sizeY = 0.0f;
    bool isCollapsed = false;
    bool isValid = false; // false means use default positioning
};

// Callback type for when layout is saved/loaded (for getting actual window positions)
using WindowLayoutCallback = WindowLayout (*)(const char* windowName);

// Add a window name to be tracked for layout management
void registerWindowForReset(const char* windowName);

// Set a callback to get current window positions/sizes
// This is called by main.cpp when saving layout
void setWindowLayoutCallback(WindowLayoutCallback callback);

// Returns true if windows should reset their positions and sizes
bool windowLayoutNeedsReset();

// Returns pointer to saved layout data if load was triggered (nullptr otherwise)
// Caller should iterate and apply positions/sizes
const WindowLayout* getPendingLayout();
size_t getPendingLayoutCount();
const char* getPendingLayoutName();

// Clear any pending layout actions (call after processing a frame)
void clearPendingLayoutAction();

// Reset all registered windows to default positions and sizes
void resetWindowLayout();

// Save current window layout to a JSON file
// Returns true on success
bool saveWindowLayout(const char* filename);

// Load window layout from a JSON file and apply it
// Returns true on success
bool loadWindowLayout(const char* filename);

// Check if a layout file exists
bool hasSavedLayout(const char* filename);

// Pin state management for individual windows
bool isWindowPinned(const char* windowName);
void toggleWindowPin(const char* windowName);
bool anyWindowsPinned();

// Reusable pin button. Call inside a window after Begin().
// Returns current pinned state (true = pinned).
bool PinButton(const char* windowName);

// Main window display orchestrator.
// Handles layout management, pin state gating, and calls individual display functions.
// Visibility is controlled by WindowManager (mode) plus pin state in Normal mode.
void displayAllWindows(const Player& player, const GameMap& map,
    NetworkManager& networkManager, Renderer& renderer,
    const MessageLog& messageLog,
    const char* layoutFilename);

void displayPlayer(const Player&);

void displayEquipment(const Player&);

void displayMap(const GameMap&, const Player&);

void displayMessageLog(const MessageLog&);

void networkMenu(NetworkManager&);

// Character selection window — shown when a newgame-choice ui-push arrives.
// Renders species/background/weapon options as clickable buttons.
void characterSelectWindow(const NewgameChoice& choice, NetworkManager& net);

void renderMenu(Renderer&);

// Debug/settings window with layout management controls
void settingsMenu(const char* layoutFilename = "window_layout.json");
