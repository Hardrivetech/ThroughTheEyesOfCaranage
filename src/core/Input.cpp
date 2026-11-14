#include "core/Input.hpp"
#include "core/Renderer.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <string>

namespace carnage::input {

namespace {

constexpr int kKeyCount    = GLFW_KEY_LAST + 1;
constexpr int kButtonCount = GLFW_MOUSE_BUTTON_LAST + 1;

std::array<bool, kKeyCount>    g_down{};
std::array<bool, kKeyCount>    g_pressed{};
std::array<bool, kKeyCount>    g_repeat{};
std::string                    g_typed;
bool                           g_textMode = false;
std::array<bool, kButtonCount> g_mouseDown{};
std::array<bool, kButtonCount> g_mousePressed{};

double g_lastX = 0.0, g_lastY = 0.0;
int    g_lastKey = -1;
double g_dx = 0.0, g_dy = 0.0;

bool validKey(int key)    { return key >= 0 && key < kKeyCount; }
bool validButton(int btn) { return btn >= 0 && btn < kButtonCount; }

void onKey(GLFWwindow*, int key, int, int action, int) {
    if (!validKey(key)) return;
    if (action == GLFW_REPEAT) {
        g_repeat[key] = true;
    } else if (action == GLFW_PRESS) {
        g_down[key]    = true;
        g_pressed[key] = true;
        g_repeat[key]  = true;
        g_lastKey      = key;
    } else if (action == GLFW_RELEASE) {
        g_down[key] = false;
    }
}

void onChar(GLFWwindow*, unsigned int cp) {
    if (g_textMode && cp >= 32 && cp < 127 && g_typed.size() < 64)
        g_typed.push_back(static_cast<char>(cp));
}

void onMouseButton(GLFWwindow*, int button, int action, int) {
    if (!validButton(button)) return;
    if (action == GLFW_PRESS) {
        g_mouseDown[button]    = true;
        g_mousePressed[button] = true;
    } else if (action == GLFW_RELEASE) {
        g_mouseDown[button] = false;
    }
}

void onCursorPos(GLFWwindow*, double x, double y) {
    g_dx += x - g_lastX;
    g_dy += y - g_lastY;
    g_lastX = x;
    g_lastY = y;
}

} // namespace

bool init() {
    GLFWwindow* w = renderer::window();
    if (!w) return false;

    g_down.fill(false);
    g_pressed.fill(false);
    g_mouseDown.fill(false);
    g_mousePressed.fill(false);

    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwGetCursorPos(w, &g_lastX, &g_lastY);
    g_dx = g_dy = 0.0;

    glfwSetKeyCallback(w, onKey);
    glfwSetCharCallback(w, onChar);
    glfwSetMouseButtonCallback(w, onMouseButton);
    glfwSetCursorPosCallback(w, onCursorPos);
    return true;
}

void shutdown() {
    if (GLFWwindow* w = renderer::window()) {
        glfwSetKeyCallback(w, nullptr);
        glfwSetCharCallback(w, nullptr);
        glfwSetMouseButtonCallback(w, nullptr);
        glfwSetCursorPosCallback(w, nullptr);
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

bool down(int key)          { return validKey(key) && g_down[key]; }
bool pressed(int key)       { return validKey(key) && g_pressed[key]; }
int  lastKeyPressed()       { return g_lastKey; }
bool pressedRepeat(int key) { return validKey(key) && g_repeat[key]; }
void setTextMode(bool on)   { g_textMode = on; g_typed.clear(); }
std::string drainTyped()    { std::string s; s.swap(g_typed); return s; }
bool mouseDown(int btn)     { return validButton(btn) && g_mouseDown[btn]; }
bool mousePressed(int btn)  { return validButton(btn) && g_mousePressed[btn]; }

glm::vec2 consumeMouseDelta() {
    const glm::vec2 d{static_cast<float>(g_dx), static_cast<float>(g_dy)};
    g_dx = g_dy = 0.0;
    return d;
}

void endTick() {
    g_lastKey = -1;
    g_pressed.fill(false);
    g_repeat.fill(false);
    g_mousePressed.fill(false);
}

void setCursorCaptured(bool captured) {
    GLFWwindow* w = renderer::window();
    if (!w) return;
    glfwSetInputMode(w, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    glfwGetCursorPos(w, &g_lastX, &g_lastY);
    g_dx = g_dy = 0.0;
}

glm::vec2 cursorNdc() {
    GLFWwindow* w = renderer::window();
    if (!w) return {0.0f, 0.0f};
    double x = 0.0, y = 0.0;
    int    ww = 1, wh = 1;
    glfwGetCursorPos(w, &x, &y);
    glfwGetWindowSize(w, &ww, &wh);
    return {static_cast<float>(x / std::max(ww, 1)) * 2.0f - 1.0f,
            1.0f - static_cast<float>(y / std::max(wh, 1)) * 2.0f};
}

} // namespace carnage::input
