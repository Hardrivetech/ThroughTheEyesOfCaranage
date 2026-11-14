#pragma once
#include <glm/vec2.hpp>

#include <string>

namespace carnage::input {

// Depends on the renderer (needs its window). Init after it, shut down before it.
// Captures and hides the cursor, using raw motion where supported.
bool init();
void shutdown();

bool down(int glfwKey);     // currently held
bool pressed(int glfwKey);  // went down since the last endTick()

bool mouseDown(int glfwButton);     // currently held
bool mousePressed(int glfwButton);  // went down since the last endTick()

// First key pressed since the last endTick(), or -1. Used by rebind capture.
int lastKeyPressed();

// Text entry (dev console). Characters are only collected while text mode is
// on, so gameplay typing never leaks into a buffer. drainTyped() returns and
// clears everything typed since the last call.
void        setTextMode(bool on);
std::string drainTyped();
// Like pressed(), but also true for OS key-repeat (backspace, arrows).
bool        pressedRepeat(int glfwKey);

// Accumulated mouse movement (counts) since the previous call; resets to zero.
// Call once per rendered frame for lowest-latency look.
glm::vec2 consumeMouseDelta();

// Call once after each fixed update so presses are consumed exactly once,
// even when several render frames pass between ticks.
void endTick();

// Menus: free the cursor (true = captured for mouse-look). Switching discards
// accumulated mouse movement so the view doesn't jump.
void setCursorCaptured(bool captured);

// Cursor position in NDC ([-1,1], +Y up). Only meaningful while not captured.
glm::vec2 cursorNdc();

} // namespace carnage::input
