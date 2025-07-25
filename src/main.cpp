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

	ui::set_theme(theme);

	while (!window_should_close(window)) {
		poll_window_events(window);

		begin_frame();
		ui::begin_frame(*window);

		if (ui::button(L"Click me")) {
			printf("hello ");
		}

		draw_rect({ Vec2 { 0.0f, 0.0f }, Vec2 { 20.0f, 20.0f } }, WHITE);
		draw_rect({ Vec2 { 10.0f, 4.0f }, Vec2 { 20.0f, 60.0f } }, WHITE);

		draw_text(L"Hello world", Vec2 { 0.0f, 100.0f }, font, WHITE);

		ui::end_frame();
		end_frame();

		swap_window_buffers(window);
	}

	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	return 0;
}
