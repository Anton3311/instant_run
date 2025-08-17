#include "ui.h"

#include "platform.h"

#include <cstring>
#include <vector>
#include <limits.h>

// TODO: rename `content_bounds` to `max_content_bounds`

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
	std::vector<Rect> layout_overflow_rects;
	LayoutState layout;
};

static State s_ui_state;

//
// UI Functions
//

void initialize(const Window& window) {
	s_ui_state.window = &window;
}

void compute_overflow_rects(Rect item_rect, Rect max_content_bounds) {
	if (!s_ui_state.options.debug_layout_overflow)
		return;

	// TODO: Compute left and top overflow rects
	
	if (item_rect.max.y > max_content_bounds.max.y) {
		s_ui_state.layout_overflow_rects.push_back(Rect {
			Vec2 { item_rect.min.x, max_content_bounds.max.y },
			item_rect.max,
		});
	}

	if (item_rect.max.x > max_content_bounds.max.x) {
		s_ui_state.layout_overflow_rects.push_back(Rect {
			Vec2 { max_content_bounds.min.x, item_rect.min.y },
			item_rect.max,
		});
	}
}

// Applies the current layout to the `item_rect`, without modifing the layout state
Rect layout_item_rect(Rect item_rect) {
	Vec2 available_space = get_available_layout_region_size();

	Vec2 item_size = item_rect.size();
	Rect result = item_rect;

	const LayoutState& layout = s_ui_state.layout;
	switch (layout.kind) {
	case LayoutKind::Vertical:
		// For a vertical layout (a column), AxisAlignment::Start means align items left,
		// AxisAlignment::Center - center, AxisAlignment - right
		switch (layout.config.cross_axis_align) {
		case AxisAlignment::Start:
			// Do nothing, already aligned 
			break;
		}

		break;
	case LayoutKind::Horizontal:
		// For a horizontal layout (a row), AxisAlignment::Start means align items to the top,
		// AxisAlignment::Center - center, AxisAlignment - to the bottom
		switch (layout.config.cross_axis_align) {
		case AxisAlignment::Start:
			// Do nothing, already aligned 
			break;
		case AxisAlignment::Center: {
			float max_row_height = layout.content_bounds.height();
			float offset = (max_row_height - item_size.y) / 2.0f;
			result.min.y += offset;
			result.max.y += offset;
			break;
		}
		}

		break;
	}

	return result;
}

void add_item(Vec2 size) {
	PROFILE_FUNCTION();
	LayoutState& layout = s_ui_state.layout;
	s_ui_state.last_item.bounds = layout_item_rect(Rect { layout.cursor, layout.cursor + size });

	switch (layout.kind) {
	case LayoutKind::Vertical:
		layout.cursor.y += size.y + layout.config.item_spacing;
		break;
	case LayoutKind::Horizontal:
		layout.cursor.x += size.x + layout.config.item_spacing;
		break;
	}

	Rect item_rect = s_ui_state.last_item.bounds;
	Rect max_content_bounds = layout.content_bounds;

	if (!layout.config.allow_overflow) {
		compute_overflow_rects(item_rect, max_content_bounds);
	}

	// Clamp the item rect to the `max_content_bounds`
	Rect padded_item_rect = Rect {
		max(item_rect.min, max_content_bounds.min),
		min(item_rect.max, max_content_bounds.max)
	};

	padded_item_rect.max += layout.config.padding;

	layout.bounds = combine_rects(layout.bounds, padded_item_rect);

	if (s_ui_state.options.debug_item_bounds) {
		draw_rect_lines(s_ui_state.last_item.bounds, Color { 0, 128, 0, 255 });
	}
}

bool is_item_hovered() {
	return rect_contains_point(s_ui_state.last_item.bounds, s_ui_state.mouse_position);
}

bool is_rect_hovered(const Rect& rect) {
	return rect_contains_point(rect, s_ui_state.mouse_position);
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

	Span<const WindowEvent> events = window_get_events(s_ui_state.window);
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

	UVec2 window_size = window_get_framebuffer_size(s_ui_state.window);
	float window_width = static_cast<float>(window_size.x);
	float window_height = static_cast<float>(window_size.y);

	draw_rect(Rect { Vec2 { 0.0f, 0.0f }, Vec2 { window_width, window_height } }, s_ui_state.theme.window_background);

	s_ui_state.layout.content_bounds = Rect { Vec2{}, Vec2 { window_width, window_height } };
	begin_vertical_layout();
}

void end_frame() {
	PROFILE_FUNCTION();
	end_vertical_layout();
	
	for (const Rect& overflow_rect : s_ui_state.layout_overflow_rects) {
		draw_rect(overflow_rect, Color { 255, 0, 255, 100 });
	}

	s_ui_state.layout_overflow_rects.clear();
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

float get_default_font_height() {
	return font_get_height(*s_ui_state.theme.default_font);
}

Vec2 compute_text_size(const Font& font, std::wstring_view text, float max_width) {
	PROFILE_FUNCTION();

	Vec2 char_position{};
	float text_width = 0.0f;

	float scale = stbtt_ScaleForPixelHeight(&font.info, font.size);

	for (size_t i = 0; i < text.size(); i++) {
		uint32_t c = text[i];
		uint32_t glyph_index = font_get_glyph_index(font, c);
		if (glyph_index == UINT32_MAX) {
			continue;
		}

		float previous_char_x = char_position.x;

		stbtt_aligned_quad quad{};
		stbtt_GetBakedQuad(font.glyphs,
				font.atlas.width,
				font.atlas.height,
				glyph_index,
				&char_position.x,
				&char_position.y,
				&quad,
				1);

		if (char_position.x > max_width) {
			// Revert the last char changes
			char_position.x = previous_char_x;
			break;
		}

		int32_t kerning_advance = 0;
		if (i + 1 < text.size()) {
			kerning_advance = stbtt_GetCodepointKernAdvance(&font.info, text[i], text[i + 1]);
			char_position.x += (float)(kerning_advance) * scale;
		}

		text_width = char_position.x;
	}

	return Vec2 { text_width, font_get_height(font) };
}

float get_default_widget_height() {
	return font_get_height(*s_ui_state.theme.default_font) + s_ui_state.theme.frame_padding.y * 2.0f;
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

bool icon_button(const Texture& texture, Rect uv_rect, const WidgetStyle* style, const float* prefered_size) {
	if (style == nullptr) {
		style = &s_ui_state.theme.default_button_style;
	}

	float button_size = prefered_size ? (*prefered_size) : get_default_widget_height();

	add_item(Vec2 { button_size, button_size });

	Rect bounds = s_ui_state.last_item.bounds;
	Vec2 icon_size = Vec2 { s_ui_state.theme.icon_size, s_ui_state.theme.icon_size };
	Vec2 icon_origin = bounds.center() - icon_size * 0.5f;

	Rect icon_rect = Rect { icon_origin, icon_origin + icon_size };

	bool hovered = is_item_hovered();
	bool pressed = s_ui_state.mouse_button_states[(size_t)MouseButton::Left] == MouseButtonState::Pressed;

	Color button_color = style->color;
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

//
// Text Input
//

enum class TextInputActionDirection {
	Left,
	Right
};

inline static bool is_word_separator(wchar_t c) {
	switch (c) {
	case L',':
	case L'.':
	case L':':
	case L';':
	case L'|':
	case L'<':
	case L'>':

	case L'!':
	case L'@':
	case L'#':
	case L'$':
	case L'%':
	case L'^':
	case L'&':
	case L'*':
	case L'(':
	case L')':

	case L'[':
	case L']':
	case L'{':
	case L'}':

	case L'\'':
	case L'"':
	case L'`':

	// whitespace
	case L' ':
	case L'\f':
	case L'\n':
	case L'\r':
	case L'\t':
	case L'\v':
		return true;
	}
	return false;
}

// look for a word boundary when moving the cursor to the right
inline static size_t find_right_word_boundary(std::wstring_view text, size_t position) {
	PROFILE_FUNCTION();

	size_t read_pos = position;

	if (is_word_separator(text[position])) {
		// The cursor is placed on the whitespace char.
		// In such a case find the left boundary of the first word to the right
		//
		// Hello    world
		//       ^  ^
		//       |  |- found boundary
		//       |- cursor position
		read_pos += 1;
		while (read_pos < text.length()) {
			if (is_word_separator(text[read_pos])) {
				read_pos += 1;
			} else {
				break;
			}
		}
	} else {
		// The cursor is placed in the middle of the word.
		// In this case, skip until the end of the current word, and then skip all the subsequent whitespace.
		//
		// Hello hello   world
		//         ^     ^
		//         |     |- found boundary
		//         |- cursor position

		while (read_pos < text.length()) {
			if (is_word_separator(text[read_pos])) {
				break;
			}

			read_pos += 1;
		}

		while (read_pos < text.length()) {
			if (!is_word_separator(text[read_pos])) {
				break;
			}

			read_pos += 1;
		}
	}

	return read_pos;
}

// look for a word boundary when moving the cursor to the left
inline static size_t find_left_word_boundary(std::wstring_view text, size_t position) {
	PROFILE_FUNCTION();

	if (position == 0) {
		return 0;
	}

	size_t read_pos = position;
	if (is_word_separator(text[read_pos - 1])) {
		// The cursor is placed on a seperator char
		// Skip all the separator chars, and then move until the start of the next word (in the left direction)
		//
		// Hello hello   world
		//       ^     ^
		//       |     |- cursor position
		//       |- found boundary
		while (read_pos > 0) {
			if (is_word_separator(text[read_pos - 1])) {
				read_pos -= 1;
			} else {
				break;
			}
		}

		while (read_pos > 0) {
			if (is_word_separator(text[read_pos - 1])) {
				break;
			} else {
				read_pos -= 1;
			}
		}
	} else {
		// Hello hello   world
		//               ^  ^
		//               |  |- cursor position
		//               |- found boundary
		while (read_pos > 0) {
			if (is_word_separator(text[read_pos - 1])) {
				break;
			}

			read_pos -= 1;
		}
	}
	
	return read_pos;
}

inline static void text_input_delete_range(TextInputState& input_state, TextRange deletion_range) {
	if (deletion_range.start == deletion_range.end) {
		return;
	}

	size_t text_length_after_selection = input_state.text_length - deletion_range.end;
	for (size_t i = 0; i < text_length_after_selection; i++) {
		input_state.buffer[deletion_range.start + i] = input_state.buffer[deletion_range.end + i];
	}

	size_t deletion_range_length = deletion_range.end - deletion_range.start;
	input_state.text_length -= deletion_range_length;
	input_state.selection_start = deletion_range.start;
	input_state.selection_end = deletion_range.start;
}

inline static bool text_input_delete(TextInputState& input_state,
		TextInputActionDirection direction,
		bool align_to_word_boundary) {
	TextRange deletion_range{};

	if (input_state.selection_start != input_state.selection_end) {
		deletion_range = text_input_state_get_selection_range(input_state);
	} else {
		std::wstring_view text = text_input_state_get_text(input_state);
		size_t cursor_position = input_state.selection_end;

		switch (direction) {
		case TextInputActionDirection::Left:
			// Can't delete to the left, because the cursor is at the start of the line
			if (cursor_position == 0) {
				return false;
			}

			if (align_to_word_boundary) {
				deletion_range.start = find_left_word_boundary(text, cursor_position);
				deletion_range.end = cursor_position;
			} else {
				deletion_range.start = cursor_position - 1;
				deletion_range.end = cursor_position;
			}

			break;
		case TextInputActionDirection::Right:
			// Can't delete to the right, because the cursor is at the end of the line
			if (cursor_position == input_state.text_length) {
				return false;
			}

			if (align_to_word_boundary) {
				deletion_range.start = cursor_position;
				deletion_range.end = find_right_word_boundary(text, cursor_position);
			} else {
				deletion_range.start = cursor_position;
				deletion_range.end = cursor_position + 1;
			}

			break;
		}
	}

	if (deletion_range.start == deletion_range.end) {
		return false;
	}

	text_input_delete_range(input_state, deletion_range);

	return true;
}

inline static void text_input_move_cursor(TextInputState& input_state,
		TextInputActionDirection direction,
		bool extend_selection,
		bool align_to_word_boundary) {

	// The user wants to move the cursor without keeping the selection,
	// but there is a non-empty text selection,
	// thus we need to collaspe the selection without moving the cursor.
	if (!extend_selection && input_state.selection_start != input_state.selection_end) {
		input_state.selection_start = input_state.selection_end;
		return;
	}

	switch (direction) {
	case TextInputActionDirection::Left:
		if (input_state.selection_end == 0) {
			break;
		}

		if (align_to_word_boundary) {
			input_state.selection_end = find_left_word_boundary(
					text_input_state_get_text(input_state),
					input_state.selection_end);
		} else {
			input_state.selection_end -= 1;
		}
		break;
	case TextInputActionDirection::Right:
		if (input_state.selection_end == input_state.text_length) {
			break;
		}

		if (align_to_word_boundary) {
			input_state.selection_end = find_right_word_boundary(
					text_input_state_get_text(input_state),
					input_state.selection_end);
		} else {
			input_state.selection_end += 1;
		}
		break;
	}

	if (!extend_selection) {
		input_state.selection_start = input_state.selection_end;
	}
}

static bool text_input_behaviour(TextInputState& input_state) {
	PROFILE_FUNCTION();

	bool changed = false;
	Span<const WindowEvent> events = window_get_events(s_ui_state.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::Key: {
			auto& key_event = events[i].data.key;
			if (key_event.action == InputAction::Pressed) {
				switch (key_event.code) {
				case KeyCode::Backspace:
				case KeyCode::Delete:
					changed |= text_input_delete(input_state, 
							key_event.code == KeyCode::Backspace
								? TextInputActionDirection::Left
								: TextInputActionDirection::Right,
							HAS_FLAG(key_event.modifiers, KeyModifiers::Control));
					break;
				case KeyCode::ArrowLeft:
				case KeyCode::ArrowRight:
					text_input_move_cursor(input_state,
							key_event.code == KeyCode::ArrowLeft
								? TextInputActionDirection::Left
								: TextInputActionDirection::Right,
							HAS_FLAG(key_event.modifiers, KeyModifiers::Shift),
							HAS_FLAG(key_event.modifiers, KeyModifiers::Control));
					break;
				case KeyCode::Home:
				case KeyCode::End:
					input_state.selection_end = key_event.code == KeyCode::Home
						? 0
						: input_state.text_length;

					if (!HAS_FLAG(key_event.modifiers, KeyModifiers::Shift)) {
						input_state.selection_start = input_state.selection_end;
					}
					break;
				case KeyCode::A:
					if (HAS_FLAG(key_event.modifiers, KeyModifiers::Control)) {
						input_state.selection_start = 0;
						input_state.selection_end = input_state.text_length;
					}
					break;
				case KeyCode::C:
					if (HAS_FLAG(key_event.modifiers, KeyModifiers::Control)) {
						window_copy_text_to_clipboard(
								*s_ui_state.window,
								text_input_state_get_selected_text(input_state));
					}
					break;
				case KeyCode::X:
					if (HAS_FLAG(key_event.modifiers, KeyModifiers::Control)) {
						TextRange selection_range = text_input_state_get_selection_range(input_state);

						if (selection_range.start != selection_range.end) {
							window_copy_text_to_clipboard(
									*s_ui_state.window,
									text_input_state_get_selected_text(input_state));

							text_input_delete_range(input_state, selection_range);
							changed = true;
						}
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

				uint32_t glyph_index = font_get_glyph_index(*s_ui_state.theme.default_font, char_event.c);
				if (glyph_index != UINT32_MAX && input_state.selection_start == input_state.selection_end) {
					size_t cursor_position = input_state.selection_end;
					size_t after_cursor_text_length = input_state.text_length - cursor_position;
					for (int64_t i = input_state.text_length; i > cursor_position; i--) {
						input_state.buffer.values[i] = input_state.buffer.values[i - 1];
					}

					input_state.buffer.values[cursor_position] = char_event.c;
					input_state.text_length += 1;
					input_state.selection_end += 1;
					input_state.selection_start = input_state.selection_end;

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
	std::wstring_view text_before_selection;
	std::wstring_view text_inside_selection;
	std::wstring_view text_after_selection;

	{
		size_t text_selection_start = input_state.selection_start;
		size_t text_selection_end = input_state.selection_end;

		if (text_selection_start > text_selection_end) {
			std::swap(text_selection_start, text_selection_end);
		}

		text_before_selection = text.substr(0, text_selection_start);
		text_inside_selection = text.substr(text_selection_start, text_selection_end - text_selection_start);
		text_after_selection = text.substr(text_selection_end);
	}

	Vec2 text_before_selection_size = compute_text_size(*s_ui_state.theme.default_font, text_before_selection);
	Vec2 text_inside_selection_size = compute_text_size(*s_ui_state.theme.default_font, text_inside_selection);
	Vec2 text_after_selection_size = compute_text_size(*s_ui_state.theme.default_font, text_after_selection);

	Vec2 text_size = Vec2 {
		text_before_selection_size.x + text_inside_selection_size.x + text_after_selection_size.x,
		s_ui_state.theme.default_font->size
	};

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

	// draw selection
	if (input_state.selection_start != input_state.selection_end) {
		float selection_width = text_before_selection_size.x + text_inside_selection_size.x;

		Rect text_selection_rect{};
		text_selection_rect.min.x = text_position.x + text_before_selection_size.x;
		text_selection_rect.min.y = text_position.y;
		text_selection_rect.max.x = text_position.x + selection_width;
		text_selection_rect.max.y = text_position.y + text_size.y;

		draw_rect(text_selection_rect, Color { 0, 0, 255, 255 });
	}

	// draw text
	if (text.length() > 0) {
		draw_text(text, text_position, *s_ui_state.theme.default_font, s_ui_state.theme.text_color);
	} else if (prompt.length() > 0) {
		draw_text(prompt, text_position, *s_ui_state.theme.default_font, s_ui_state.theme.prompt_text_color);
	}

	// draw cursor
	{
		float cursor_offset = 0.0f;
		if (input_state.selection_start >= input_state.selection_end) {
			// cursor is on the left boundary of the selection
			cursor_offset = text_before_selection_size.x;
		} else {
			// cursor is on the right boundary of the selection
			cursor_offset = text_before_selection_size.x + text_inside_selection_size.x;
		}

		Vec2 text_cursor_position = Vec2 { text_position.x + cursor_offset, text_position.y };
		Vec2 cursor_size = Vec2 { 2.0f, text_size.y }; // cursor has the same height as the text
		draw_rect(Rect { text_cursor_position, text_cursor_position + cursor_size }, WHITE);
	}

	return changed;
}

void colored_text(std::wstring_view text, Color color) {
	PROFILE_FUNCTION();

	float available_space = get_available_layout_space();
	Vec2 text_size = compute_text_size(*s_ui_state.theme.default_font, text, available_space);

	add_item(text_size);
	Vec2 text_position = s_ui_state.last_item.bounds.min;

	draw_text(text, text_position, *s_ui_state.theme.default_font, color, available_space);
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

		if (prev_layout_config.padding != Vec2{}) {
			draw_rect_lines(s_ui_state.layout.content_bounds, Color { 0, 128, 0, 255 });
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

void begin_horizontal_layout(const LayoutConfig* config, const float* prefered_height) {
	if (config == nullptr) {
		config = &s_ui_state.theme.default_layout_config;
	}

	Vec2 cursor = s_ui_state.layout.cursor;

	Rect content_bounds{};
	content_bounds.min = cursor + config->padding;
	content_bounds.max = s_ui_state.layout.content_bounds.max - config->padding;

	Rect bounds = Rect{ cursor, cursor + config->padding * 2.0f };

	if (prefered_height != nullptr) {
		bounds.min = cursor;
		bounds.max.x = cursor.x;
		bounds.max.y = cursor.y + *prefered_height;

		// Here might occur an overflow of the parent layout,
		// when `prefered_height + config->padding.y * 2.0f` doesn't fit in the `max_content_bounds` of the parent
		content_bounds.min.y = bounds.min.y + config->padding.y;
		content_bounds.max.y = bounds.max.y - config->padding.y;
	}

	s_ui_state.layout_stack.push_back(s_ui_state.layout);

	s_ui_state.layout = LayoutState {
		.kind = LayoutKind::Horizontal,
		.bounds = bounds,
		.content_bounds = content_bounds,
		.cursor = content_bounds.min,
		.config = *config
	};
}

void begin_fixed_horizontal_layout(Vec2 prefered_size, const LayoutConfig* config) {
	if (config == nullptr) {
		config = &s_ui_state.theme.default_layout_config;
	}

	Vec2 cursor = s_ui_state.layout.cursor;

	Rect content_bounds{};
	content_bounds.min = cursor + config->padding;
	content_bounds.max = content_bounds.min + prefered_size - config->padding;
	
	// NOTE: padding is already included once in the bounds.min
	Rect bounds{};
	bounds.min = cursor;
	bounds.max = bounds.min + prefered_size + config->padding;

	s_ui_state.layout_stack.push_back(s_ui_state.layout);

	s_ui_state.layout = LayoutState {
		.kind = LayoutKind::Horizontal,
		.bounds = bounds,
		.content_bounds = content_bounds,
		.cursor = content_bounds.min,
		.config = *config
	};
}

void end_horizontal_layout() {
	pop_layout();
}

Rect get_max_layout_bounds() {
	auto& layout = s_ui_state.layout;
	Rect bounds = layout.content_bounds;
	bounds.min -= layout.config.padding;
	bounds.max += layout.config.padding;
	return bounds;
}

}
