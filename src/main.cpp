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

	for (size_t i = 0; i < (string.length() - pattern.length()); i++) {
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

void walk_directory(const std::filesystem::path& path, std::vector<Entry>& entries) {
	for (std::filesystem::path child : std::filesystem::directory_iterator(path)) {
		if (std::filesystem::is_directory(child)) {
			walk_directory(child, entries);
		} else {
			Entry& entry = entries.emplace_back();
			entry.name = child.filename().wstring();
			entry.path = child;
		}
	}
}

int main()
{
	Window* window = create_window(800, 500, L"Instant Run");

	initialize_renderer(window);

	Font font = load_font_from_file("./assets/Roboto/Roboto-Regular.ttf", 18.0f);
	ui::Theme theme{};
	theme.default_font = &font;
	theme.button_color = Color(100, 100, 100, 255);
	theme.text_color = WHITE;
	theme.item_spacing = 2.0f;
	theme.frame_padding = Vec2 { 4.0f, 4.0f };

	constexpr size_t INPUT_BUFFER_SIZE = 128;
	wchar_t text_buffer[INPUT_BUFFER_SIZE];
	ui::TextInputState input_state{};
	input_state.buffer = Span(text_buffer, INPUT_BUFFER_SIZE);

	ui::initialize(*window);
	ui::set_theme(theme);

	std::vector<Entry> entries;

	walk_directory("C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs", entries);

	std::vector<ResultEntry> matches;
	std::vector<RangeU32> highlights;
	size_t selected_result_entry = 0;

	update_search_result({}, entries, matches, highlights);

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
					case KeyCode::ArrowUp:
						selected_result_entry = (selected_result_entry + matches.size() - 1) % matches.size();
						break;
					case KeyCode::ArrowDown:
						selected_result_entry = (selected_result_entry + 1) % matches.size();
						break;
					}
				}
				break;
		    }
			}
		}

		begin_frame();
		ui::begin_frame();

		if (ui::text_input(input_state, 128.0)) {
			std::wstring_view search_pattern(text_buffer, input_state.text_length);
			update_search_result(search_pattern, entries, matches, highlights);

			selected_result_entry = 0;
		}

		Color highlight_color = Color { 255, 0, 255, 255 };

		for (size_t i = 0; i < matches.size(); i++) {
			bool is_selected = i == selected_result_entry;

			const ResultEntry& match = matches[i];
			const Entry& entry = entries[match.entry_index];

			Color text_color = is_selected ? Color { 0, 255, 0, 255 } : WHITE;

			ui::colored_text(entry.name, text_color);
		
			for (uint32_t i = match.highlights.start; i < match.highlights.start + match.highlights.count; i++) {
				RangeU32 highlight_range = highlights[i];

				std::wstring_view highlighted_text = std::wstring_view(entry.name)
					.substr(highlight_range.start, highlight_range.count);

				ui::colored_text(highlighted_text, highlight_color);
			}
		}

		ui::end_frame();
		end_frame();

		swap_window_buffers(window);
	}

	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	return 0;
}
