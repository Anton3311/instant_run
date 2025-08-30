#pragma once

#include <stdint.h>
#include <string_view>
#include <filesystem>
#include <vector>

#include "core.h"
#include "math.h"

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

	// This event has no data
	FocusLost,
};

enum class InputAction {
	Pressed,
	Released,
};

enum class KeyCode {
	A,
	C,
	V,
	X,

	Escape,
	Enter,
	Backspace,
	Delete,

	ArrowUp,
	ArrowDown,
	ArrowLeft,
	ArrowRight,

	Home,
	End,

	F3,
};

enum class KeyModifiers {
	None = 0,
	Control = 1 << 0,
	Shift = 1 << 1,
	Alt = 1 << 2
};

IMPL_ENUM_FLAGS(KeyModifiers);

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
			KeyModifiers modifiers;
		} key;

		struct {
			wchar_t c;
		} char_typed;
	} data;
};

struct Window;

void platform_initialize();
void platform_shutdown();

void platform_initialize_thread();
void platform_shutdown_thread();

void platform_set_this_thread_affinity_mask(uint64_t mask);

void platform_log_error_message();

typedef void* ModuleHandle;

ModuleHandle platform_load_library(const char* path);
void platform_unload_library(ModuleHandle module);

void* platform_get_function_address(ModuleHandle module, const char* function_name);

//
// Keybaord Hook
//

struct KeyboardHook;
struct HookConfig;
using KeyboardHookHandle = KeyboardHook*;

KeyboardHookHandle keyboard_hook_init(Arena& allocator, const HookConfig& hook_config);
void keyboard_hook_shutdown(KeyboardHookHandle hook);


//
// Windows
//

Window* window_create(uint32_t width, uint32_t height, std::wstring_view title);

void window_show(Window* window);
void window_hide(Window* window);
void window_focus(Window* window);

void window_swap_buffers(Window* window);

bool window_should_close(const Window* window);
void window_poll_events(Window* window);
void window_wait_for_events(Window* window);

Span<const WindowEvent> window_get_events(const Window* window);

UVec2 window_get_framebuffer_size(const Window* window);
void window_close(Window& window);
void window_destroy(Window* window);

bool window_copy_text_to_clipboard(const Window& window, std::wstring_view text);
std::wstring_view window_read_clipboard_text(const Window& window, Arena& allocator);

//
// File system
//

enum class UserFolderKind {
	Desktop = 1,
	StartMenu = 2,
	Programs = 4,
};

IMPL_ENUM_FLAGS(UserFolderKind);

std::vector<std::filesystem::path> get_user_folders(UserFolderKind folders);

struct InstalledAppDesc {
	const wchar_t* id;
	std::wstring_view logo_uri;
	std::wstring_view display_name;
};

struct InstalledAppsQueryState;

// Schedules the jobs that query the installed applications
//
// Uses the `temp_arena` for allocated the query state.
// Returns `nullptr` in case of failure.
//
// Not safe to clear the `temp_arena` until the `platform_finish_installed_apps_query` has completed.
InstalledAppsQueryState* platform_begin_installed_apps_query(Arena& temp_arena);

// Waits until all the jobs scheduled by `platform_begin_installed_apps_query` and then collects the result.
std::vector<InstalledAppDesc> platform_finish_installed_apps_query(InstalledAppsQueryState* query_state,
		Arena& job_execution_arena);

bool platform_launch_installed_app(const wchar_t* app_id);

struct Bitmap {
	uint32_t width;
	uint32_t height;
	uint32_t* pixels;
};

typedef void* SystemIconHandle;

static constexpr uint32_t INVALID_ICON_ID = UINT32_MAX;

SystemIconHandle fs_query_file_icon(const std::filesystem::path& path);
uint32_t fs_query_file_icon_id(const std::filesystem::path& path);
void fs_release_file_icon(SystemIconHandle icon);
Bitmap fs_extract_icon_bitmap(SystemIconHandle icon, Arena& bitmap_allocator);

Bitmap get_file_icon(const std::filesystem::path& path, Arena& arena);
std::filesystem::path fs_resolve_shortcut(const std::filesystem::path& path);

enum class RunFileResult {
	Ok,
	OutOfMemory,
	PathNotFound,
	BadFormat,
	AccessDenied,
	OtherError,
};

RunFileResult platform_run_file(const std::filesystem::path& path, bool run_as_admin);
