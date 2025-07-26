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
	const Window* window;
	Theme theme;

	Vec2 cursor;
	
	Vec2 mouse_position;
	MouseButtonState mouse_button_states[MOUSE_BUTTON_COUNT];

	bool has_typed_char;
	wchar_t typed_char;

	ItemState last_item;
};

static State s_ui_state;

//
// UI Functions
//

void initialize(const Window& window) {
	s_ui_state.window = &window;
}

void add_item(Rect bounds) {
	s_ui_state.last_item.bounds = bounds;

	s_ui_state.cursor.y += bounds.max.y - bounds.min.y + s_ui_state.theme.item_spacing;
}

void add_item(Vec2 size) {
	s_ui_state.last_item.bounds = Rect { s_ui_state.cursor, s_ui_state.cursor + size };

	s_ui_state.cursor.y += size.y + s_ui_state.theme.item_spacing;
}

bool is_item_hoevered() {
	return rect_contains_point(s_ui_state.last_item.bounds, s_ui_state.mouse_position);
}

void set_cursor(Vec2 position) {

}

Vec2 get_item_size() {
	Rect bounds = s_ui_state.last_item.bounds;
	return bounds.max - bounds.min;
}

void begin_frame() {
	s_ui_state.cursor = Vec2{};
	s_ui_state.last_item = {};

	std::memset(s_ui_state.mouse_button_states, 0, sizeof(s_ui_state.mouse_button_states));

	s_ui_state.has_typed_char = false;

	Span<const WindowEvent> events = get_window_events(s_ui_state.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::MouseMoved: {
			UVec2 pos = events[i].data.mouse_moved.position;
			s_ui_state.mouse_position = Vec2 { static_cast<float>(pos.x), static_cast<float>(pos.y) };
			break;
		}
		case WindowEventKind::MousePressed: {
			s_ui_state.mouse_button_states[(size_t)events[i].data.mouse_pressed.button] = MouseButtonState::Pressed;
			break;
		}
		case WindowEventKind::MouseReleased: {
			s_ui_state.mouse_button_states[(size_t)events[i].data.mouse_pressed.button] = MouseButtonState::Released;
			break;
		}
		case WindowEventKind::CharTyped:
			s_ui_state.has_typed_char = true;
			s_ui_state.typed_char = events[i].data.char_typed.c;
			break;
		}
	}
}

void end_frame() {

}

void set_theme(const Theme& theme) { 
	s_ui_state.theme = theme;
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
	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text);
	Vec2 button_size = text_size + s_ui_state.theme.frame_padding * 2.0f;

	Rect item_bounds = { .min = s_ui_state.cursor, .max = s_ui_state.cursor + button_size };
	add_item(item_bounds);

	bool hovered = is_item_hoevered();
	bool pressed = s_ui_state.mouse_button_states[(size_t)MouseButton::Left] == MouseButtonState::Pressed;

	Color button_color = s_ui_state.theme.button_color;
	if (hovered) {
		button_color = Color(255, 0, 0, 255);
	}

	draw_rect(item_bounds, button_color);
	draw_text(text, item_bounds.min + s_ui_state.theme.frame_padding, *s_ui_state.theme.default_font, s_ui_state.theme.text_color);

	return pressed && hovered;
}

bool text_input(TextInputState& input_state, float width) {
	// Process Input
	
	bool changed = false;
	Span<const WindowEvent> events = get_window_events(s_ui_state.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::Key:
		{
			auto& key_event = events[i].data.key;
			if (key_event.action == InputAction::Pressed) {
				switch (key_event.code) {
				case KeyCode::Backspace:
					if (input_state.text_length > 0) {
						input_state.text_length -= 1;
						changed = true;
					} 
					break;
				}
			}

			break;
		}
		case WindowEventKind::CharTyped:
		{
			auto& char_event = events[i].data.char_typed;
			
			if (input_state.text_length < input_state.buffer.count) {
				// HACK: Allow any char
				
				uint32_t char_range_end = s_ui_state.theme.default_font->char_range_start
					+ s_ui_state.theme.default_font->glyph_count;

				if ((uint32_t)char_event.c >= s_ui_state.theme.default_font->char_range_start
						&& (uint32_t)char_event.c < char_range_end) {
					input_state.buffer.values[input_state.text_length] = char_event.c;
					input_state.text_length += 1;

					changed = true;
				}
			}

			break;
		}
		}
	}

	// Draw

	std::wstring_view text(input_state.buffer.values, input_state.text_length);
	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text);

	Vec2 text_field_size = text_size + s_ui_state.theme.frame_padding * 2.0f;

	Rect bounds = { s_ui_state.cursor, s_ui_state.cursor + text_field_size };

	add_item(bounds);

	draw_rect(bounds, s_ui_state.theme.button_color);
	draw_text(text, bounds.min + s_ui_state.theme.frame_padding, *s_ui_state.theme.default_font, s_ui_state.theme.text_color);

	Vec2 text_cursor_position = bounds.min + s_ui_state.theme.frame_padding;
	text_cursor_position.x += text_size.x;

	draw_rect(Rect { text_cursor_position, text_cursor_position + Vec2 { 2.0f, text_size.y } }, WHITE);

	return changed;
}

void colored_text(std::wstring_view text, Color color) {
	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text);

	Vec2 text_position = s_ui_state.cursor;

	add_item(text_size);
	draw_text(text, text_position, *s_ui_state.theme.default_font, color);
}

void text(std::wstring_view text) {
	colored_text(text, s_ui_state.theme.text_color);
}

}
