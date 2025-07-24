#include "platform.h"

#include <glad/glad.h>

#include "renderer.h"

int main()
{
	Window* window = create_window(800, 300, L"Instant Run");

	initialize_renderer(window);

	Font font = load_font_from_file("C:/Windows/Fonts/Times.ttf", 18.0f);

	while (!window_should_close(window)) {
		poll_window_events(window);

		begin_frame();

		draw_rect({ Vec2 { 0.0f, 0.0f }, Vec2 { 20.0f, 20.0f } }, WHITE);
		draw_rect({ Vec2 { 10.0f, 4.0f }, Vec2 { 20.0f, 60.0f } }, WHITE);

		draw_text(L"Hello world", Vec2 { 0.0f, 100.0f }, font, WHITE);

		end_frame();

		swap_window_buffers(window);
	}

	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	return 0;
}
