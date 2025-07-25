#pragma once

#include <stdint.h>
#include <string_view>

#include "core.h"

enum class MouseButton {
	Left,
	Right,
	Middle,
};

enum class WindowEventKind {
	MouseMoved,
	MousePressed,
	MouseReleased,
	CharTyped,
};

struct WindowEvent {
	WindowEventKind kind;

	union {
		struct {
			UVec2 position;
		} mouse_moved;

		struct {
			MouseButton mouse_button;
		} mouse_pressed;

		struct {
			MouseButton mouse_button;
		} mouse_released;

		struct {
			wchar_t c;
		} CharTyped;
	} data;
};

struct Window;

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title);

void swap_window_buffers(Window* window);

bool window_should_close(const Window* window);
void poll_window_events(Window* window);

Span<const WindowEvent> get_window_events(const Window* window);

UVec2 get_window_framebuffer_size(const Window* window);

void destroy_window(Window* window);
