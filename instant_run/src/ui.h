#pragma once

#include "core.h"
#include "renderer.h"
#include "platform.h"

#include <string_view>
#include <limits.h>

struct Window;

namespace ui {

struct Options {
	bool debug_layout;
	bool debug_layout_overflow;
	bool debug_item_bounds;
};

enum class SizeConstraint {
	WrapContent,
	Fixed,
};

enum class AxisAlignment {
	Start,
	Center,
	End,
};

struct LayoutConfig {
	float item_spacing;
	Vec2 padding;
	bool allow_overflow;
	AxisAlignment cross_axis_align;
};

struct WidgetStyle {
	Color color;
	Color hovered_color;
	Color pressed_color;

	Color content_color;
	Color content_hovered_color;
	Color content_pressed_color;
};

struct Theme {
	const Font* default_font;

	Color window_background;

	Color text_color;
	Color prompt_text_color;

	Color separator_color;

	Color widget_color;
	Color widget_hovered_color;
	Color widget_pressed_color;

	Color icon_color;
	Color icon_hovered_color;
	Color icon_pressed_color;

	WidgetStyle default_button_style;

	float icon_size;

	LayoutConfig default_layout_config;

	float frame_corner_radius;
	Vec2 frame_padding;
};

//
// Text input
//

struct TextRange {
	size_t start;
	size_t end;
};

struct TextInputState {
	size_t selection_start;
	size_t selection_end; // thats where the cursor is
	size_t text_length;
	Span<wchar_t> buffer;
};

inline std::wstring_view text_input_state_get_text(const TextInputState& state) {
	return std::wstring_view(state.buffer.values, state.text_length);
}

inline TextRange text_input_state_get_selection_range(const TextInputState& state) {
	if (state.selection_start >= state.selection_end) {
		return { state.selection_end, state.selection_start };
	} else {
		return { state.selection_start, state.selection_end };
	}
}

inline std::wstring_view text_input_state_get_selected_text(const TextInputState& state) {
	TextRange selection = text_input_state_get_selection_range(state);
	return text_input_state_get_text(state).substr(selection.start, selection.end - selection.start);
}

inline void text_input_state_clear(TextInputState& state) {
	state.selection_start = 0;
	state.selection_end = 0;
	state.text_length = 0;
}

// `lang_agnostic_input` - uses char input translated to the default keyboard layout
bool text_input_behaviour(TextInputState& input_state, bool lang_agnostic_input = false);

void initialize(const Window& window, Arena& arena);

void begin_frame();
void end_frame();

const Theme& get_theme();
void set_theme(const Theme& theme);
Options& get_options();

float get_default_font_height();

Vec2 compute_text_size(const Font& font, std::wstring_view text, float max_width = FLT_MAX);

// Returns the total size of the line which consists of multiple strings.
//
// `sizes` recieves the size of each string passed in the `strings` array
// `start_positions` recieves the starting position of each part
Vec2 compute_text_size(const Font& font,
		const std::wstring_view* strings,
		Vec2* sizes,
		Vec2* start_positions,
		size_t string_count,
		float max_width);

//
// UI Item
//

void add_item(Vec2 size);
bool is_item_hovered();
bool is_rect_hovered(const Rect& rect);
Rect get_item_bounds();
Vec2 get_item_size();
Vec2 get_cursor();
void append_item_spacing(float spacing);
void set_cursor(Vec2 position);

bool is_mouse_button_pressed(MouseButton button);

float get_available_layout_space();
Vec2 get_available_layout_region_size();

void push_next_item_fixed_size(float fixed_size);

//
// Widgets
//

float get_default_widget_height();

bool button(std::wstring_view text);
bool icon_button(const Texture& texture,
		Rect uv_rect,
		const WidgetStyle* style = nullptr,
		const float* prefered_size = nullptr);
void icon(const Texture& texture, Rect uv_rect);
void image(const Texture& texture, Vec2 size, Rect uv_rect, Color tint = WHITE);
bool text_input(TextInputState& input_state, std::wstring_view prompt = {}); 

void colored_text(const std::wstring_view* text_parts, const Color* colors, size_t text_part_count);
void text(std::wstring_view text);

void separator();

//
// Layout
//

void set_layout_item_spacing(float item_spacing);

void begin_vertical_layout(const LayoutConfig* config = nullptr);
void end_vertical_layout();

// `prefered_height` includes padding, the actual max content bounds will have height - padding.y * 2.0f
void begin_horizontal_layout(const LayoutConfig* config = nullptr, const float* prefered_height = nullptr);
void begin_fixed_horizontal_layout(Vec2 prefered_size, const LayoutConfig* config);
void end_horizontal_layout();

// Returns layout's content bounds + padding
Rect get_max_layout_bounds();

}

