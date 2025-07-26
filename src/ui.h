#pragma once

#include <string_view>

#include "core.h"
#include "renderer.h"

struct Window;

namespace ui {

struct Options {
	bool debug_layout;
};

enum class SizeConstraint {
	WrapContent,
	Fixed,
};

struct Theme {
	const Font* default_font;

	Color text_color;
	Color prompt_text_color;

	Color separator_color;

	Color widget_color;
	Color widget_hovered_color;
	Color widget_pressed_color;

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

const Theme& get_theme();
void set_theme(const Theme& theme);
Options& get_options();

Vec2 compute_text_size(const Font& font, std::wstring_view text);

//
// UI Item
//

void add_item(Vec2 size);
bool is_item_hovered();
Rect get_item_bounds();
Vec2 get_item_size();
Vec2 get_cursor();
void set_cursor(Vec2 position);

float get_available_layout_space();
Vec2 get_available_layout_region_size();

void push_next_item_fixed_size(float fixed_size);

//
// Widgets
//

bool button(std::wstring_view text);
bool text_input(TextInputState& input_state, std::wstring_view prompt = {}); 

void colored_text(std::wstring_view text, Color color);
void text(std::wstring_view text);

void separator();

//
// Layout
//

void set_layout_item_spacing(float item_spacing);

void begin_vertical_layout();
void end_vertical_layout();

void begin_horizontal_layout();
void end_horizontal_layout();

}

