#include "ui.h"

#include "platform.h"

#include <cstring>
#include <vector>

namespace ui {

struct ItemState {
	Rect bounds;
};

enum class LayoutKind {
	Vertical,
	Horizontal,
};

struct LayoutState {
	LayoutKind kind;
	Rect bounds;
	Rect content_bounds;
	Vec2 cursor;
	LayoutConfig config;

	SizeConstraint next_item_size_constraint;
	float next_item_fixed_size;
};

enum class MouseButtonState {
	None,
	Pressed,
	Released,
};

struct State {
	const Window* window;
	Theme theme;
	Options options;

	Vec2 cursor;
	
	Vec2 mouse_position;
	MouseButtonState mouse_button_states[MOUSE_BUTTON_COUNT];

	bool has_typed_char;
	wchar_t typed_char;

	ItemState last_item;

	std::vector<LayoutState> layout_stack;
	LayoutState layout;
};

static State s_ui_state;

//
// UI Functions
//

void initialize(const Window& window) {
	s_ui_state.window = &window;
}

void add_item(Vec2 size) {
	LayoutState& layout = s_ui_state.layout;
	s_ui_state.last_item.bounds = Rect { layout.cursor, layout.cursor + size };

	switch (layout.kind) {
	case LayoutKind::Vertical:
		layout.cursor.y += size.y + layout.config.item_spacing;
		break;
	case LayoutKind::Horizontal:
		layout.cursor.x += size.x + layout.config.item_spacing;
		break;
	}

	Rect padded_item_rect = s_ui_state.last_item.bounds;
	padded_item_rect.max += layout.config.padding;

	layout.bounds = combine_rects(layout.bounds, padded_item_rect);
}

bool is_item_hovered() {
	return rect_contains_point(s_ui_state.last_item.bounds, s_ui_state.mouse_position);
}

Rect get_item_bounds() {
	return s_ui_state.last_item.bounds;
}

Vec2 get_item_size() {
	Rect bounds = s_ui_state.last_item.bounds;
	return bounds.max - bounds.min;
}

Vec2 get_cursor() {
	return s_ui_state.layout.cursor;
}

void append_item_spacing(float spacing) {
	switch (s_ui_state.layout.kind) {
	case LayoutKind::Vertical:
		s_ui_state.layout.cursor.y += spacing;
		break;
	case LayoutKind::Horizontal:
		s_ui_state.layout.cursor.x += spacing;
		break;
	}
}

void set_cursor(Vec2 position) {
	s_ui_state.layout.cursor = position;
}

bool is_mouse_button_pressed(MouseButton button) {
	return s_ui_state.mouse_button_states[(size_t)button] == MouseButtonState::Pressed;
}

float get_available_layout_space() {
	const LayoutState& layout = s_ui_state.layout;

	switch (s_ui_state.layout.kind) {
	case LayoutKind::Vertical:
		return static_cast<float>(layout.content_bounds.max.y) - layout.cursor.y;
	case LayoutKind::Horizontal:
		return static_cast<float>(layout.content_bounds.max.x) - layout.cursor.x;
	}

	return 0.0f;
}

Vec2 get_available_layout_region_size() {
	return s_ui_state.layout.content_bounds.max
		- s_ui_state.layout.cursor;
}

void push_next_item_fixed_size(float fixed_size) {
	s_ui_state.layout.next_item_size_constraint = SizeConstraint::Fixed;
	s_ui_state.layout.next_item_fixed_size = fixed_size;
}

void begin_frame() {
	PROFILE_FUNCTION();
	s_ui_state.layout.cursor = Vec2{};
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
		case WindowEventKind::Key:
			// TODO: Handle
			break;
		}
	}

	UVec2 window_size = get_window_framebuffer_size(s_ui_state.window);
	float window_width = static_cast<float>(window_size.x);
	float window_height = static_cast<float>(window_size.y);

	draw_rect(Rect { Vec2 { 0.0f, 0.0f }, Vec2 { window_width, window_height } }, s_ui_state.theme.window_background);

	s_ui_state.layout.content_bounds = Rect { Vec2{}, Vec2 { window_width, window_height } };
	begin_vertical_layout();
}

void end_frame() {
	PROFILE_FUNCTION();
	end_vertical_layout();
}

const Theme& get_theme() {
	return s_ui_state.theme;
}

void set_theme(const Theme& theme) { 
	s_ui_state.theme = theme;
}

Options& get_options() {
	return s_ui_state.options;
}

Vec2 compute_text_size(const Font& font, std::wstring_view text) {
	PROFILE_FUNCTION();

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
		text_width = char_position.x;
	}

	return Vec2 { text_width, font.size };
}

float get_default_widget_height() {
	return s_ui_state.theme.default_font->size + s_ui_state.theme.frame_padding.y * 2.0f;
}

bool button(std::wstring_view text) {
	PROFILE_FUNCTION();

	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text);
	Vec2 button_size = text_size + s_ui_state.theme.frame_padding * 2.0f;

	add_item(button_size);
	Rect item_bounds = s_ui_state.last_item.bounds;

	bool hovered = is_item_hovered();
	bool pressed = s_ui_state.mouse_button_states[(size_t)MouseButton::Left] == MouseButtonState::Pressed;

	Color button_color = s_ui_state.theme.widget_color;
	if (pressed && hovered) {
		button_color = s_ui_state.theme.widget_pressed_color;
	} else if (hovered) {
		button_color = s_ui_state.theme.widget_hovered_color;
	}

	draw_rounded_rect(item_bounds, button_color, s_ui_state.theme.frame_corner_radius);
	draw_text(text,
			item_bounds.min + s_ui_state.theme.frame_padding,
			*s_ui_state.theme.default_font,
			s_ui_state.theme.text_color);

	return pressed && hovered;
}

bool icon_button(const Texture& texture, Rect uv_rect, const WidgetStyle* style) {
	if (style == nullptr) {
		style = &s_ui_state.theme.default_button_style;
	}

	float button_size = get_default_widget_height();

	add_item(Vec2 { button_size, button_size });

	Rect bounds = s_ui_state.last_item.bounds;
	Vec2 icon_size = Vec2 { s_ui_state.theme.icon_size, s_ui_state.theme.icon_size };
	Vec2 icon_origin = bounds.center() - icon_size * 0.5f;

	Rect icon_rect = Rect { icon_origin, icon_origin + icon_size };

	bool hovered = is_item_hovered();
	bool pressed = s_ui_state.mouse_button_states[(size_t)MouseButton::Left] == MouseButtonState::Pressed;

	Color button_color = s_ui_state.theme.widget_color;
	Color icon_color = s_ui_state.theme.icon_color;

	if (pressed && hovered) {
		button_color = style->pressed_color;
		icon_color = style->content_pressed_color;
	} else if (hovered) {
		button_color = style->hovered_color;
		icon_color = style->content_hovered_color;
	}

	draw_rounded_rect(bounds, button_color, s_ui_state.theme.frame_corner_radius);
	draw_rect(icon_rect, icon_color, texture, uv_rect);

	return pressed && hovered;
}

void icon(const Texture& texture, Rect uv_rect) {
	float item_size = get_default_widget_height();

	add_item(Vec2 { item_size, item_size });

	Rect bounds = s_ui_state.last_item.bounds;
	Vec2 icon_size = Vec2 { s_ui_state.theme.icon_size, s_ui_state.theme.icon_size };
	Vec2 icon_origin = bounds.center() - icon_size * 0.5f;

	Rect icon_rect = Rect { icon_origin, icon_origin + icon_size };

	draw_rect(icon_rect, s_ui_state.theme.icon_color, texture, uv_rect);
}

void image(const Texture& texture, Vec2 size, Rect uv_rect, Color tint) {
	add_item(size);

	Rect bounds = s_ui_state.last_item.bounds;

	draw_rect(bounds, tint, texture, uv_rect);
}

static bool text_input_behaviour(TextInputState& input_state) {
	PROFILE_FUNCTION();

	bool changed = false;
	Span<const WindowEvent> events = get_window_events(s_ui_state.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::Key: {
			auto& key_event = events[i].data.key;
			if (key_event.action == InputAction::Pressed) {
				switch (key_event.code) {
				case KeyCode::Backspace:
					if (input_state.text_length > 0) {
						input_state.text_length -= 1;
						input_state.cursor_position -= 1;
						changed = true;
					} 
					break;
				case KeyCode::ArrowLeft:
					if (input_state.cursor_position > 0) {
						input_state.cursor_position -= 1;
					}
					break;
				case KeyCode::ArrowRight:
					if (input_state.cursor_position < input_state.text_length) {
						input_state.cursor_position += 1;
					}
					break;
				default:
					break;
				}
			}

			break;
		}
		case WindowEventKind::CharTyped: {
			auto& char_event = events[i].data.char_typed;
			
			if (input_state.text_length < input_state.buffer.count) {
				// HACK: Allow any char
				
				uint32_t char_range_end = s_ui_state.theme.default_font->char_range_start
					+ s_ui_state.theme.default_font->glyph_count;

				if ((uint32_t)char_event.c >= s_ui_state.theme.default_font->char_range_start
						&& (uint32_t)char_event.c < char_range_end) {
					input_state.buffer.values[input_state.text_length] = char_event.c;
					input_state.text_length += 1;
					input_state.cursor_position += 1;

					changed = true;
				}
			}

			break;
		}
		default:
			break;
		}
	}

	return changed;
}

bool text_input(TextInputState& input_state, std::wstring_view prompt) {
	PROFILE_FUNCTION();

	// Process Input
	
	bool changed = text_input_behaviour(input_state);

	// Draw

	std::wstring_view text(input_state.buffer.values, input_state.text_length);

	// Compute text size
	std::wstring_view text_before_cursor = text.substr(0, input_state.cursor_position);
	std::wstring_view text_after_cursor = text.substr(input_state.cursor_position);

	Vec2 text_before_cursor_size = compute_text_size(*s_ui_state.theme.default_font, text_before_cursor);
	Vec2 text_after_cursor_size = compute_text_size(*s_ui_state.theme.default_font, text_after_cursor);

	Vec2 text_size = Vec2 { text_before_cursor_size.x + text_after_cursor_size.x, s_ui_state.theme.default_font->size };
	Vec2 text_field_size{};

	switch (s_ui_state.layout.next_item_size_constraint) {
	case SizeConstraint::WrapContent:
	{
		Vec2 prompt_size = compute_text_size(*s_ui_state.theme.default_font, prompt);
		text_field_size = max(text_size, prompt_size) + s_ui_state.theme.frame_padding * 2.0f;
		break;
	}
	case SizeConstraint::Fixed:
		text_field_size = Vec2 {
			s_ui_state.layout.next_item_fixed_size,
				s_ui_state.theme.default_font->size + s_ui_state.theme.frame_padding.y * 2.0f
		};

		// Reset constraint
		s_ui_state.layout.next_item_size_constraint = SizeConstraint::WrapContent;
		break;
	}

	add_item(text_field_size);

	Rect bounds = s_ui_state.last_item.bounds;

	draw_rect(bounds, s_ui_state.theme.widget_color);

	Vec2 text_position = bounds.min + s_ui_state.theme.frame_padding;
	if (text.length() > 0) {
		draw_text(text, text_position, *s_ui_state.theme.default_font, s_ui_state.theme.text_color);
	} else if (prompt.length() > 0) {
		draw_text(prompt, text_position, *s_ui_state.theme.default_font, s_ui_state.theme.prompt_text_color);
	}

	Vec2 text_cursor_position = text_position + Vec2 { text_before_cursor_size.x, 0.0f };
	draw_rect(Rect { text_cursor_position, text_cursor_position + Vec2 { 2.0f, text_size.y } }, WHITE);

	return changed;
}

void colored_text(std::wstring_view text, Color color) {
	PROFILE_FUNCTION();
	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text);

	add_item(text_size);
	Vec2 text_position = s_ui_state.last_item.bounds.min;

	draw_text(text, text_position, *s_ui_state.theme.default_font, color);
}

void text(std::wstring_view text) {
	colored_text(text, s_ui_state.theme.text_color);
}

static constexpr float SEPARATOR_THICKNESS = 2.0f;

void separator() {
	Vec2 available_space = get_available_layout_region_size();

	switch (s_ui_state.layout.kind) {
	case LayoutKind::Vertical:
		add_item(Vec2 { available_space.x, SEPARATOR_THICKNESS });
		break;
	case LayoutKind::Horizontal:
		add_item(Vec2 { SEPARATOR_THICKNESS, available_space.y });
		break;
	}

	Rect bounds = s_ui_state.last_item.bounds;
	draw_rect(bounds, s_ui_state.theme.separator_color);
}

//
// Layout
//

static void pop_layout() {
	Rect current_layout_bounds = s_ui_state.layout.bounds;
	LayoutConfig prev_layout_config = s_ui_state.layout.config;

	s_ui_state.layout = s_ui_state.layout_stack.back();
	s_ui_state.layout_stack.pop_back();

	add_item(current_layout_bounds.max - current_layout_bounds.min);

	if (s_ui_state.options.debug_layout) {
		draw_rect_lines(current_layout_bounds, Color { 255, 0, 255, 255 });

		draw_rect_lines(current_layout_bounds, Color { 255, 0, 255, 255 });

		if (prev_layout_config.padding != Vec2{}) {
			Rect inner_rect = current_layout_bounds;
			inner_rect.min += prev_layout_config.padding;
			inner_rect.max -= prev_layout_config.padding;
			draw_rect_lines(inner_rect, Color { 128, 0, 128, 255 });
		}
	}
}

void set_layout_item_spacing(float item_spacing) {
	s_ui_state.layout.config.item_spacing = item_spacing;
}

void begin_vertical_layout(const LayoutConfig* config) {
	if (config == nullptr) {
		config = &s_ui_state.theme.default_layout_config;
	}

	Vec2 cursor = s_ui_state.layout.cursor;

	Rect content_bounds{};
	content_bounds.min = cursor + config->padding;
	content_bounds.max = s_ui_state.layout.content_bounds.max - config->padding;
	
	s_ui_state.layout_stack.push_back(s_ui_state.layout);
	
	s_ui_state.layout = LayoutState {
		.kind = LayoutKind::Vertical,
		.bounds = Rect{ cursor, cursor + config->padding * 2.0f },
		.content_bounds = content_bounds,
		.cursor = content_bounds.min,
		.config = *config
	};
}

void end_vertical_layout() {
	pop_layout();
}

void begin_horizontal_layout(const LayoutConfig* config) {
	if (config == nullptr) {
		config = &s_ui_state.theme.default_layout_config;
	}

	Vec2 cursor = s_ui_state.layout.cursor;

	Rect content_bounds{};
	content_bounds.min = cursor + config->padding;
	content_bounds.max = s_ui_state.layout.content_bounds.max - config->padding;

	s_ui_state.layout_stack.push_back(s_ui_state.layout);

	s_ui_state.layout = LayoutState {
		.kind = LayoutKind::Horizontal,
		.bounds = Rect{ cursor, cursor + config->padding * 2.0f },
		.content_bounds = content_bounds,
		.cursor = content_bounds.min,
		.config = *config
	};
}

void end_horizontal_layout() {
	pop_layout();
}

}
