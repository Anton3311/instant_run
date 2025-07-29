#pragma once

#include <stdint.h>
#include <string_view>
#include <filesystem>
#include <vector>

#include "core.h"

//
// Window
//

enum class MouseButton {
	Left = 0,
	Right = 1,
	Middle = 2,
};

static constexpr size_t MOUSE_BUTTON_COUNT = 3;

enum class WindowEventKind {
	MouseMoved,
	MousePressed,
	MouseReleased,
	Key,
	CharTyped,
};

enum class InputAction {
	Pressed,
	Released,
};

enum class KeyCode {
	Escape,
	Enter,
	Backspace,

	ArrowUp,
	ArrowDown,
	ArrowLeft,
	ArrowRight,

	F3,
};

struct WindowEvent {
	WindowEventKind kind;

	union {
		struct {
			UVec2 position;
		} mouse_moved;

		struct {
			MouseButton button;
		} mouse_pressed;

		struct {
			MouseButton button;
		} mouse_released;

		struct {
			InputAction action;
			KeyCode code;
		} key;

		struct {
			wchar_t c;
		} char_typed;
	} data;
};

struct Window;

void initialize_platform();
void shutdown_platform();

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title);

void swap_window_buffers(Window* window);

bool window_should_close(const Window* window);
void poll_window_events(Window* window);

Span<const WindowEvent> get_window_events(const Window* window);

UVec2 get_window_framebuffer_size(const Window* window);

void close_window(Window& window);

void destroy_window(Window* window);

//
// File system
//

std::vector<std::filesystem::path> get_start_menu_folder_path();

struct Bitmap {
	uint32_t width;
	uint32_t height;
	uint32_t* pixels;
};

Bitmap get_file_icon(const std::filesystem::path& path);
std::filesystem::path read_symlink_path(const std::filesystem::path& path);
std::filesystem::path read_shortcut_path(const std::filesystem::path& path);
