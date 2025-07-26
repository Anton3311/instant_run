#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#include "platform.h"
#include "renderer.h"
#include "ui.h"

struct Entry {
	std::wstring name;
	std::filesystem::path path;
};

struct ResultEntry {
	uint32_t entry_index;
	uint32_t score;

	RangeU32 highlights;
};

struct ResultViewState {
	uint32_t selected_index;
	uint32_t scroll_offset;
	uint32_t visible_item_count;
	
	std::vector<ResultEntry> matches;
	std::vector<RangeU32> highlights;
};

bool starts_with(std::wstring_view string, std::wstring_view start_pattern) {
	if (string.length() < start_pattern.length()) {
		return false;
	}

	size_t min_length = std::min(string.length(), start_pattern.length());
	for (size_t i = 0; i < min_length; i++) {
		if (string[i] != start_pattern[i]) {
			return false;
		}
	}

	return true;
}

uint32_t dp[100][100];
uint32_t compute_edit_distance(const std::wstring_view& string, const std::wstring_view& pattern)
{
	std::memset(dp, 0, sizeof(dp));

	for (size_t i = 0; i <= string.size(); i++)
		dp[i][0] = i;

	for (size_t i = 0; i <= pattern.size(); i++)
		dp[0][i] = i;

	for (size_t j = 1; j <= pattern.size(); j++) {
		for (size_t i = 1; i <= string.size(); i++) {
			uint32_t substitution_cost;

			if (string[i - 1] == pattern[j - 1]) {
				substitution_cost = 0;
			} else {
				substitution_cost = 1;
			}

			uint32_t deletion = dp[i - 1][j] + 1 + 2;
			uint32_t insertion = dp[i][j - 1] + 1;
			uint32_t substitution = dp[i - 1][j - 1] + substitution_cost;

			dp[i][j] = std::min(substitution, std::min(deletion, insertion));
		}
	}

	return dp[string.size()][pattern.size()];
}

inline wchar_t to_lower_case(wchar_t c) {
	if (c >= L'A' && c <= 'Z') {
		return c - L'A' + 'a';
	}
	return c;
}

uint32_t compute_logest_common_subsequence(
		const std::wstring_view& string,
		const std::wstring_view& pattern,
		std::vector<RangeU32>& sequence_ranges,
		RangeU32& highlight_range) {
	if (string.length() < pattern.length()) {
		return 0;
	}

	uint32_t result = 0;

	highlight_range.start = static_cast<uint32_t>(sequence_ranges.size());

	for (size_t i = 0; i < (string.length() - pattern.length() + 1); i++) {
		uint32_t sequence_length = 0;

		for (size_t j = 0; j < pattern.size(); j++) {
			if (to_lower_case(string[i + j]) == to_lower_case(pattern[j])) {
				sequence_length += 1;
			} else {
				break;
			}
		}

		if (sequence_length > 0) {
			sequence_ranges.push_back(RangeU32 { static_cast<uint32_t>(i), sequence_length });

			highlight_range.count += 1;
		}

		result = std::max(result, sequence_length);
	}

	return result;
}

void update_search_result(std::wstring_view search_pattern,
		const std::vector<Entry>& entries,
		std::vector<ResultEntry>& result,
		std::vector<RangeU32>& sequence_ranges) {
	result.clear();
	sequence_ranges.clear();

	for (size_t i = 0; i < entries.size(); i++) {
		const Entry& entry = entries[i];

		RangeU32 highlight_range{};

		uint32_t score = compute_logest_common_subsequence(
				entry.name,
				search_pattern,
				sequence_ranges,
				highlight_range);

		result.push_back({ (uint32_t)i, score, highlight_range });
	}

	std::sort(result.begin(), result.end(), [&](auto a, auto b) -> bool {
		return a.score > b.score;
	});
}

float compute_result_view_item_height() {
	const ui::Theme& theme = ui::get_theme();
	return theme.default_font->size + theme.frame_padding.y * 2.0f;
}

void draw_result_entry(const ResultEntry& match,
		const Entry& entry,
		const ResultViewState& state,
		Color text_color,
		Color highlight_color) {
	const ui::Theme& theme = ui::get_theme();

	Vec2 available_region = ui::get_available_layout_region_size();
	Vec2 widget_size = Vec2 { available_region.x, compute_result_view_item_height() } + theme.frame_padding * 2.0f;

	ui::add_item(widget_size);

	Rect item_bounds = ui::get_item_bounds();

	{
		bool hovered = ui::is_item_hovered();

		Color widget_color = theme.widget_color;
		if (hovered) {
			widget_color = theme.widget_hovered_color;
		}

		draw_rect(item_bounds, widget_color);
	}

	Vec2 saved_cursor = ui::get_cursor();
	ui::set_cursor(item_bounds.min + theme.frame_padding);

	ui::LayoutConfig layout_config{};
	layout_config.padding = theme.frame_padding;

	ui::begin_horizontal_layout(&layout_config);

	uint32_t cursor = 0;
	for (uint32_t i = match.highlights.start; i < match.highlights.start + match.highlights.count; i++) {
		RangeU32 highlight_range = state.highlights[i];

		if (cursor != highlight_range.start) {
			std::wstring_view t = std::wstring_view(entry.name)
				.substr(cursor, highlight_range.start - cursor);

			ui::colored_text(t, text_color);
		}

		std::wstring_view highlighted_text = std::wstring_view(entry.name)
			.substr(highlight_range.start, highlight_range.count);

		ui::colored_text(highlighted_text, highlight_color);

		cursor = highlight_range.start + highlight_range.count;
	}

	if (cursor < entry.name.length()) {
		std::wstring_view t = std::wstring_view(entry.name)
			.substr(cursor);

		ui::colored_text(t, text_color);
	}

	ui::end_horizontal_layout();

	ui::set_cursor(saved_cursor);
}

void append_entry(std::vector<Entry>& entries, const std::filesystem::path& path) {
	Entry& entry = entries.emplace_back();
	entry.name = path.filename().replace_extension("").wstring();
	entry.path = path;
}

void walk_directory(const std::filesystem::path& path, std::vector<Entry>& entries) {
	for (std::filesystem::path child : std::filesystem::directory_iterator(path)) {
		if (std::filesystem::is_directory(child)) {
			walk_directory(child, entries);
		} else {
			append_entry(entries, child);
		}
	}
}

void process_result_view_key_event(ResultViewState& state, KeyCode key) {
	size_t result_count = state.matches.size();

	switch (key) {
	case KeyCode::ArrowUp:
		state.selected_index = (state.selected_index + result_count - 1) % result_count;
		break;
	case KeyCode::ArrowDown:
		state.selected_index = (state.selected_index + 1) % result_count;
		break;
	}
}

void update_result_view_scroll(ResultViewState& state) {
	uint32_t visible_range_end = state.scroll_offset + state.visible_item_count;
	bool is_selection_visible = state.selected_index >= state.scroll_offset && state.selected_index < visible_range_end;

	if (is_selection_visible) {
		return;
	}

	if (state.selected_index >= visible_range_end) {
		state.scroll_offset += state.selected_index - visible_range_end + 1;
	} else if (state.selected_index < state.scroll_offset) {
		state.scroll_offset = state.selected_index;
	}
}

int main()
{
	Window* window = create_window(800, 500, L"Instant Run");

	initialize_renderer(window);

	Font font = load_font_from_file("./assets/Roboto/Roboto-Regular.ttf", 22.0f);
	ui::Theme theme{};
	theme.default_font = &font;

	theme.window_background = color_from_hex(0x242222FF);

	theme.widget_color = color_from_hex(0x242222FF);
	theme.widget_hovered_color = color_from_hex(0x4F4F56FF);
	theme.widget_pressed_color = color_from_hex(0x37373AFF);

	theme.separator_color = color_from_hex(0x37373AFF);
	theme.text_color = WHITE;
	theme.prompt_text_color = color_from_hex(0x9E9E9EFF);
	theme.default_layout_config.item_spacing = 4.0f;
	theme.default_layout_config.padding = Vec2 { 12.0f, 12.0f };
	theme.frame_padding = Vec2 { 12.0f, 8.0f };

	Color highlight_color = color_from_hex(0xE6A446FF);

	constexpr size_t INPUT_BUFFER_SIZE = 128;
	wchar_t text_buffer[INPUT_BUFFER_SIZE];
	ui::TextInputState input_state{};
	input_state.buffer = Span(text_buffer, INPUT_BUFFER_SIZE);

	ui::initialize(*window);
	ui::set_theme(theme);

	ui::Options& options = ui::get_options();

	std::vector<Entry> entries;

	try {
		std::vector<std::filesystem::path> known_folders = get_start_menu_folder_path();
		for (const auto& known_folder : known_folders) {
			walk_directory(known_folder, entries);
		}
	} catch (std::exception e) {
		std::cout << e.what() << '\n';
	}

	ResultViewState result_view_state{};

	update_search_result({}, entries, result_view_state.matches, result_view_state.highlights);

	while (!window_should_close(window)) {
		poll_window_events(window);

		Span<const WindowEvent> events = get_window_events(window);
		for (size_t i = 0; i < events.count; i++) {
			switch (events[i].kind) {
			case WindowEventKind::Key: {
				auto& key_event = events[i].data.key;
				if (key_event.action == InputAction::Pressed) {
					switch (key_event.code) {
					case KeyCode::Escape:
						close_window(*window);
						break;
					case KeyCode::F3:
						options.debug_layout = !options.debug_layout;
						break;
					default:
						process_result_view_key_event(result_view_state, key_event.code);
						break;
					}
				}
				break;
		    }
			}
		}

		begin_frame();
		ui::begin_frame();
		ui::begin_vertical_layout();

		float text_field_width = ui::get_available_layout_region_size().x;
		ui::push_next_item_fixed_size(text_field_width);

		if (ui::text_input(input_state, L"Search ...")) {
			std::wstring_view search_pattern(text_buffer, input_state.text_length);
			update_search_result(search_pattern, entries, result_view_state.matches, result_view_state.highlights);

			result_view_state.selected_index = 0;
		}

		if (ui::button(L"Clear")) {
			input_state.text_length = 0;
			update_search_result(L"", entries, result_view_state.matches, result_view_state.highlights);
		}

		ui::separator();

		float available_height = ui::get_available_layout_space();
		float item_height = compute_result_view_item_height() + theme.default_layout_config.item_spacing;

		result_view_state.visible_item_count = std::ceil(available_height / item_height);

		update_result_view_scroll(result_view_state);

		uint32_t visible_item_count = std::min(
				result_view_state.visible_item_count,
				(uint32_t)result_view_state.matches.size() - result_view_state.scroll_offset);

		for (uint32_t i = result_view_state.scroll_offset; i < result_view_state.scroll_offset + visible_item_count; i++) {
			bool is_selected = i == result_view_state.selected_index;

			const ResultEntry& match = result_view_state.matches[i];
			const Entry& entry = entries[match.entry_index];

			Color text_color = is_selected ? Color { 0, 255, 0, 255 } : WHITE;

			draw_result_entry(match, entry, result_view_state, text_color, highlight_color);
		}

		ui::end_vertical_layout();

		ui::end_frame();
		end_frame();

		swap_window_buffers(window);
	}

	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	return 0;
}
