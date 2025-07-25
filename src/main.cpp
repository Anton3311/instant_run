#include "platform.h"

#include <glad/glad.h>

#include "renderer.h"
#include "ui.h"

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

	wchar_t text_buffer[16];
	ui::TextInputState input_state{};
	input_state.buffer = Span(text_buffer, 16);

	ui::set_theme(theme);

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
		ui::begin_frame(*window);

		if (ui::button(L"Click me")) {
			printf("hello ");
		}

		ui::text_input(input_state, 128.0);

		ui::end_frame();
		end_frame();

		swap_window_buffers(window);
	}

	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	return 0;
}
