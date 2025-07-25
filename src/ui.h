#pragma once

#include <string_view>

#include "core.h"
#include "renderer.h"

struct Window;

namespace ui {

struct Theme {
	const Font* default_font;

	Color button_color;
	Color text_color;

	float item_spacing;
	Vec2 frame_padding;
};

void begin_frame(const Window& window);
void end_frame();

void set_theme(const Theme& theme);

Vec2 compute_text_size(const Font& font, std::wstring_view text);

bool button(std::wstring_view text);
void text(std::wstring_view text);

}

