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

void update_search_result(std::wstring_view search_pattern, const std::vector<Entry>& entries, std::vector<size_t>& result) {
	result.clear();

	for (size_t i = 0; i < entries.size(); i++) {
		const Entry& entry = entries[i];
		if (starts_with(entry.name, search_pattern) || starts_with(entry.path.wstring(), search_pattern)) {
			result.push_back(i);
		}
	}
}

int main()
{
	Window* window = create_window(800, 300, L"Instant Run");

	initialize_renderer(window);

	Font font = load_font_from_file("C:/Windows/Fonts/Times.ttf", 18.0f);
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
	entries.push_back(Entry { .name = L"Chrome", .path = "chrome.exe" });
	entries.push_back(Entry { .name = L"GIMP", .path = "gimp.exe" });
	entries.push_back(Entry { .name = L"Obsidian", .path = "obsidian.exe" });
	entries.push_back(Entry { .name = L"Visual Studio", .path = "visual_stdio.exe" });
	entries.push_back(Entry { .name = L"raddbg", .path = "raddbg.exe" });

	std::vector<size_t> matches;

	update_search_result({}, entries, matches);

	while (!window_should_close(window)) {
		poll_window_events(window);

		Span<const WindowEvent> events = get_window_events(window);
		for (size_t i = 0; i < events.count; i++) {
			switch (events[i].kind) {
			case WindowEventKind::Key: {
				auto& key_event = events[i].data.key;
				if (key_event.action == InputAction::Pressed && key_event.code == KeyCode::Escape) {
					close_window(*window);
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
		}

		for (size_t match : matches) {
			const Entry& entry = entries[match];

			ui::text(entry.name);
			ui::text(entry.path.wstring());
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
