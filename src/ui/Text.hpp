#pragma once
#include "core/DrawTypes.hpp"

#include <glm/glm.hpp>

#include <string_view>
#include <vector>

namespace carnage::ui {

// 5x7 bitmap font drawn as overlay rects (horizontal runs are merged).
// `px` is the height of one font pixel in NDC-y units, so a glyph is 7*px
// tall. Upper case only (lower case is mapped up); unknown characters are
// skipped but still advance.
float textWidth(std::string_view text, float px, float aspect);  // NDC-x
bool  hasGlyph(char c);  // true if `c` (case-insensitively) can be drawn
void  drawText(std::vector<OverlayRect>& out, std::string_view text, glm::vec2 topLeft,
               float px, float aspect, glm::vec4 color);

} // namespace carnage::ui
