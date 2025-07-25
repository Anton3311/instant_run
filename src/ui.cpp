#include "ui.h"

#include "platform.h"

#include <cstring>

namespace ui {

struct ItemState {
	Rect bounds;
};

enum class MouseButtonState {
	None,
	Pressed,
	Released,
};

struct State {
	Theme theme;

	Vec2 cursor;
	
	Vec2 mouse_position;
	MouseButtonState mouse_button_states[MOUSE_BUTTON_COUNT];

	ItemState last_item;
};

static State s_state;

//
// UI Functions
//

static void add_item(Rect bounds) {
	s_state.last_item.bounds = bounds;

	s_state.cursor.y += bounds.max.y - bounds.min.y + s_state.theme.item_spacing;
}

static bool is_item_hoevered() {
	return rect_contains_point(s_state.last_item.bounds, s_state.mouse_position);
}

void begin_frame(const Window& window) {
	s_state.cursor = Vec2{};
	s_state.last_item = {};

	std::memset(s_state.mouse_button_states, 0, sizeof(s_state.mouse_button_states));

	Span<const WindowEvent> events = get_window_events(&window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::MouseMoved: {
			UVec2 pos = events[i].data.mouse_moved.position;
			s_state.mouse_position = Vec2 { static_cast<float>(pos.x), static_cast<float>(pos.y) };
			break;
		}
		case WindowEventKind::MousePressed: {
			s_state.mouse_button_states[(size_t)events[i].data.mouse_pressed.button] = MouseButtonState::Pressed;
			break;
		}
		case WindowEventKind::MouseReleased: {
			s_state.mouse_button_states[(size_t)events[i].data.mouse_pressed.button] = MouseButtonState::Released;
			break;
		}
		}
	}
}

void end_frame() {

}

void set_theme(const Theme& theme) { 
	s_state.theme = theme;
}

Vec2 compute_text_size(const Font& font, std::wstring_view text) {
	Vec2 char_position{};
	float text_width = 0.0f;

	for (size_t i = 0; i < text.size(); i++) {
		uint32_t c = text[i];

		if (c < font.char_range_start || c >= font.char_range_start + font.glyph_count) {
			continue;
		}

		stbtt_aligned_quad quad{};
		stbtt_GetBakedQuad(font.glyphs,
				font.atlas.width,
				font.atlas.height,
				c - font.char_range_start,
				&char_position.x,
				&char_position.y,
				&quad,
				1);

		Vec2 char_size = Vec2(quad.x1 - quad.x0, quad.y1 - quad.y0);
		text_width = char_position.x + char_size.x;
	}

	return Vec2 { text_width, font.size };
}

bool button(std::wstring_view text) {
	Vec2 text_size = compute_text_size(*s_state.theme.default_font, text);
	Vec2 button_size = text_size + s_state.theme.frame_padding * 2.0f;

	Rect item_bounds = { .min = s_state.cursor, .max = s_state.cursor + button_size };
	add_item(item_bounds);

	bool hovered = is_item_hoevered();
	bool pressed = s_state.mouse_button_states[(size_t)MouseButton::Left] == MouseButtonState::Pressed;

	Color button_color = s_state.theme.button_color;
	if (hovered) {
		button_color = Color(255, 0, 0, 255);
	}

	draw_rect(item_bounds, button_color);
	draw_text(text, item_bounds.min + s_state.theme.frame_padding, *s_state.theme.default_font, s_state.theme.text_color);

	return pressed && hovered;
}

void text(std::wstring_view text) {

}

}
