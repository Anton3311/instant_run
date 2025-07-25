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

struct TextInputState {
	size_t cursor_position;
	size_t text_length;
	Span<wchar_t> buffer;
};

void initialize(const Window& window);

void begin_frame();
void end_frame();

void set_theme(const Theme& theme);

Vec2 compute_text_size(const Font& font, std::wstring_view text);

//
// UI Item
//

// TODO: Pass the size instead of the full bounds
void add_item(Rect bounds);
void add_item(Vec2 size);
bool is_item_hoevered();

//
// Widgets
//

bool button(std::wstring_view text);
bool text_input(TextInputState& input_state, float width);

void colored_text(std::wstring_view text, Color color);
void text(std::wstring_view text);

}

