#include <filesystem>
#include <string>
#include <vector>

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
// FIXME: doesn't work
uint32_t compute_edit_distance(const std::wstring_view& string, const std::wstring_view& pattern)
{
	for (size_t i = 0; i < string.size(); i++)
		dp[i][0] = i;

	for (size_t i = 0; i < pattern.size(); i++)
		dp[0][i] = i;

	for (size_t i = 1; i <= string.size(); i++)
	{
		for (size_t j = 1; j <= pattern.size(); j++)
		{
			uint32_t sub_cost = (string[i - 1] == pattern[j - 1]) ? 0 : 1;

			dp[i][j] = std::min<uint32_t>(
				dp[i - 1][j] + 1,
				std::min<uint32_t>(
					dp[i][j - 1] + 1,
					dp[i - 1][j - 1] + sub_cost));
		}
	}

	return dp[string.size()][pattern.size()];
}

void update_search_result(std::wstring_view search_pattern,
		const std::vector<Entry>& entries,
		std::vector<ResultEntry>& result) {
	result.clear();

	for (size_t i = 0; i < entries.size(); i++) {
		const Entry& entry = entries[i];

		uint32_t score = compute_edit_distance(entry.name, search_pattern);
		result.push_back({ (uint32_t)i, score });
	}

	std::sort(result.begin(), result.end(), [&](auto a, auto b) -> bool {
		return a.score < b.score;
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
	Window* window = create_window(800, 300, L"Instant Run");

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
	size_t selected_result_entry = 0;

	update_search_result({}, entries, matches);

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
			matches.clear();

			std::wstring_view search_pattern(text_buffer, input_state.text_length);
			update_search_result(search_pattern, entries, matches);

			selected_result_entry = 0;
		}

		for (size_t i = 0; i < matches.size(); i++) {
			bool is_selected = i == selected_result_entry;
			const Entry& entry = entries[matches[i].entry_index];

			ui::colored_text(entry.name, is_selected ? Color { 0, 255, 0, 255 } : WHITE);
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
