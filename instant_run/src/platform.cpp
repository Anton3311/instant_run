#include "platform.h"

#include "hook_config.h"
#include "log.h"
#include "job_system.h"

#include <string>

#include <Windows.h>
#include <Shlobj.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objbase.h>
#include <glad/glad.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <credentialprovider.h>
#include <sddl.h>
#include <appxpackaging.h>
#include <shlwapi.h>
#include <shobjidl_core.h>

#define NOMINMAX

enum class ShortcutResolverState {
	NotCreated,
	Created,
	Invalid,
};

struct ShortcutResolver {
	IPersistFile* persistent_file_interface; 
	IShellLink* shell_link_interface;
	ShortcutResolverState state = ShortcutResolverState::NotCreated;
};

thread_local ShortcutResolver t_shortcut_resolver;

static void shortcut_resolver_create_for_thread() {
	PROFILE_FUNCTION();
	t_shortcut_resolver = {};

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called. 
    HRESULT hres = CoCreateInstance(CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_IShellLink,
			(LPVOID*)&t_shortcut_resolver.shell_link_interface); 

    if (SUCCEEDED(hres)) { 
        // Get a pointer to the IPersistFile interface. 
        hres = t_shortcut_resolver.shell_link_interface->QueryInterface(IID_IPersistFile,
				(void**)&t_shortcut_resolver.persistent_file_interface); 
        
        if (SUCCEEDED(hres)) {
			t_shortcut_resolver.state = ShortcutResolverState::Created;
		} else {
			t_shortcut_resolver.state = ShortcutResolverState::Invalid;
		}
	} else {
		t_shortcut_resolver.state = ShortcutResolverState::Invalid;
	}
}

static void shortcut_resolver_release() {
	if (t_shortcut_resolver.persistent_file_interface) {
		t_shortcut_resolver.persistent_file_interface->Release();
	}

	if (t_shortcut_resolver.shell_link_interface) {
		t_shortcut_resolver.shell_link_interface->Release();
	}

	t_shortcut_resolver = {};
}

void platform_initialize() {
	PROFILE_FUNCTION();

	platform_initialize_thread();
}

void platform_shutdown() {
	PROFILE_FUNCTION();

	platform_shutdown_thread();
}

void platform_initialize_thread() {
	PROFILE_FUNCTION();

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	
	shortcut_resolver_create_for_thread();
}

void platform_shutdown_thread() {
	PROFILE_FUNCTION();

	shortcut_resolver_release();

	CoUninitialize();
}

void platform_set_this_thread_affinity_mask(uint64_t mask) {
	PROFILE_FUNCTION();

	HANDLE this_thread = GetCurrentThread();
	DWORD_PTR result = SetThreadAffinityMask(this_thread, (DWORD_PTR)mask);

	if (!result) {
		platform_log_error_message();
	}
}

void platform_log_error_message() {
	PROFILE_FUNCTION();

	DWORD error_code = GetLastError();

	wchar_t* message = nullptr;
	size_t message_length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&message,
			0,
			NULL);

	log_error(std::wstring_view(message, message_length));

	LocalFree(message);
}

ModuleHandle platform_load_library(const char* path) {
	PROFILE_FUNCTION();
	return LoadLibraryA(path);
}

void platform_unload_library(ModuleHandle module) {
	PROFILE_FUNCTION();
	FreeLibrary((HINSTANCE)module);
}

void* platform_get_function_address(ModuleHandle module, const char* function_name) {
	PROFILE_FUNCTION();
	
	return (void*)GetProcAddress((HINSTANCE)module, function_name);
}

//
// Keybaord Hook
//

struct KeyboardHook {
	std::thread hook_thread;
	HINSTANCE hook_module;
	HOOKPROC hook_proc;
	HHOOK hook_handle;

	DWORD hook_thread_id;

	std::mutex hook_mutex;
	std::condition_variable hook_var;
};

static const char* KEYBOARD_HOOK_FUNCTION_NAME = "keyboard_hook";
static const char* KEYBOARD_HOOK_INIT_FUNCTION_NAME = "init_keyboard_hook";
static const wchar_t* KEYBOARD_HOOK_WORKER_THREAD_NAME = L"low_level_keyboard_hook_thread_worker";

void keyboard_hook_thread_worker(KeyboardHookHandle hook) {
	PROFILE_NAME_THREAD(KEYBOARD_HOOK_WORKER_THREAD_NAME);

	Arena arena{};
	arena.capacity = kb_to_bytes(4);

	log_init_thread(arena, KEYBOARD_HOOK_WORKER_THREAD_NAME);

	{
		PROFILE_SCOPE("initialize_keyboard_hook");
		hook->hook_thread_id = GetThreadId(GetCurrentThread());
		hook->hook_handle = SetWindowsHookEx(WH_KEYBOARD_LL, hook->hook_proc, hook->hook_module, 0);

		if (!hook->hook_handle) {
			exit(EXIT_FAILURE);
		}

		hook->hook_var.notify_all();
	}

	MSG msg{};
	while (GetMessageA(&msg, 0, 0, 0)) {
		if (msg.message == WM_QUIT) {
			break;
		}
	}

	{
		PROFILE_SCOPE("notify_about_hook_deinit");
		hook->hook_var.notify_all();
	}

	log_shutdown_thread();
	arena_release(arena);
}

KeyboardHookHandle keyboard_hook_init(Arena& allocator, const HookConfig& hook_config) {
	PROFILE_FUNCTION();

	HINSTANCE hook_module = LoadLibraryA("instant_run.dll");
	if (!hook_module) {
		return nullptr;
	}

	InitKeyboardHookFunction init_hook = (InitKeyboardHookFunction)GetProcAddress(
			hook_module,
			KEYBOARD_HOOK_INIT_FUNCTION_NAME);

	HOOKPROC hook_proc = (HOOKPROC)GetProcAddress(hook_module, KEYBOARD_HOOK_FUNCTION_NAME);
	if (!hook_proc) {
		return nullptr;
	}

	init_hook(hook_config);

	KeyboardHook* hook = arena_alloc<KeyboardHook>(allocator);
	new(hook) KeyboardHook();

	hook->hook_module = hook_module;
	hook->hook_proc = hook_proc;
	hook->hook_thread = std::thread(keyboard_hook_thread_worker, hook);

	{
		PROFILE_SCOPE("wait_for_hook_worker_enable");
		std::unique_lock<std::mutex> lock(hook->hook_mutex);
		hook->hook_var.wait(lock);
	}

	return hook;
}

void keyboard_hook_shutdown(KeyboardHookHandle hook) {
	PROFILE_FUNCTION();

 	if (!UnhookWindowsHookEx(hook->hook_handle)) {
		log_error(L"failed to unhook keyboard hook");
		platform_log_error_message();
	}

	if (!PostThreadMessageA(hook->hook_thread_id, WM_QUIT, 0, 0)) {
		log_error(L"failed to post thread quit message");
		platform_log_error_message();
	}

	{
		std::unique_lock<std::mutex> lock(hook->hook_mutex);
		hook->hook_var.wait(lock);
	}

	hook->hook_thread.join();

	FreeLibrary(hook->hook_module);

	hook->~KeyboardHook();
}

//
// OpenGL
//

static const wchar_t* WINDOW_CLASS_NAME = L"InstantRun";

constexpr int32_t WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int32_t WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int32_t WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;

using wglCreateContextAttribsARBFunction = HGLRC WINAPI (HDC hdc, HGLRC hShareContext, const int *attribList);
wglCreateContextAttribsARBFunction* wglCreateContextAttribsARB;

using  wglChoosePixelFormatARBFunction = BOOL WINAPI(HDC hdc,
	const int* piAttribIList,
	const FLOAT* pfAttribFList,
	UINT nMaxFormats,
	int* piFormats,
	UINT* nNumFormats);

wglChoosePixelFormatARBFunction* wglChoosePixelFormatARB;

using wglSwapIntervalEXTFunction = BOOL WINAPI(int interval);
wglSwapIntervalEXTFunction* wglSwapIntervalEXT;

//
// Window
//

static constexpr size_t EVENT_BUFFER_SIZE = 8;

struct Window {
	std::wstring title;
	uint32_t width;
	uint32_t height;

	HWND handle;

	bool should_close;

	WindowEvent events[EVENT_BUFFER_SIZE];
	size_t event_count;
};

static HMODULE s_opengl_module;

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam);
bool create_opengl_context(Window* window);
bool init_opengl(Window* window);
bool translate_key_code(WPARAM virtual_key_code, KeyCode& output);

Window* window_create(uint32_t width, uint32_t height, std::wstring_view title) {
	PROFILE_FUNCTION();
	Window* window = new Window();
	window->title = title;
	window->width = width;
	window->height = height;

	WNDCLASSW window_class{};
	window_class.lpfnWndProc = window_procedure;
	window_class.hInstance = GetModuleHandleA(nullptr);
	window_class.lpszClassName = WINDOW_CLASS_NAME;
	window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

	if (!RegisterClassW(&window_class))
	{
		return nullptr;
	}

	int32_t window_x = CW_USEDEFAULT;
	int32_t window_y = CW_USEDEFAULT;

	{
		RECT monitor_work_area{};
		if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &monitor_work_area, 0)) {
			uint32_t work_area_width = monitor_work_area.right - monitor_work_area.left;
			uint32_t work_area_height = monitor_work_area.bottom - monitor_work_area.top;

			window_x = monitor_work_area.left + (work_area_width - width) / 2;
			window_y = monitor_work_area.top + (work_area_height - height) / 2;
		}
	}

	window->handle = CreateWindowExW(0,
		WINDOW_CLASS_NAME,
		window->title.c_str(),
		WS_POPUP,
		window_x,
		window_y,
		static_cast<int>(window->width),
		static_cast<int>(window->height),
		nullptr,
		nullptr,
		GetModuleHandleW(nullptr),
		nullptr);

	if (window->handle == nullptr) {
		return nullptr;
	}

	LONG_PTR style = GetWindowLongPtr(window->handle, GWL_STYLE);
	style |= WS_THICKFRAME;
	style &= ~WS_CAPTION;
	SetWindowLongPtr(window->handle, GWL_STYLE, style);

	MARGINS margins{ 1, 1, 1, 1 };
	DwmExtendFrameIntoClientArea(window->handle, &margins);

	SetWindowPos(window->handle, NULL, 0, 0, width, height, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOCOPYBITS);

	SetWindowLongPtrW(window->handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	create_opengl_context(window);
	init_opengl(window);

	return window;
}

void window_show(Window* window) {
	ShowWindow(window->handle, SW_SHOW);
}

void window_hide(Window* window) {
	ShowWindow(window->handle, SW_HIDE);
}

void window_focus(Window* window) {
	EnableWindow(window->handle, true);

	if (!BringWindowToTop(window->handle)) {
		log_error(L"failed to bring window to top");
		platform_log_error_message();
		return;
	}

	if (!SetForegroundWindow(window->handle)) {
		log_error(L"failed to set foreground window");
		return;
	}

	SetFocus(window->handle);

	if (GetLastError() == 0x57) {
		log_error(L"failed to focus window: ");
		platform_log_error_message();
		return;
	}
}

void window_swap_buffers(Window* window) {
	PROFILE_FUNCTION();
	SwapBuffers(GetDC(window->handle));
}

bool window_should_close(const Window* window) {
	return window->should_close;
}

void window_poll_events(Window* window) {
	PROFILE_FUNCTION();
	window->event_count = 0;

	MSG message{};
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

void window_wait_for_events(Window* window) {
	PROFILE_FUNCTION();
	window->event_count = 0;

	MSG message{};
	if (GetMessageA(&message, nullptr, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}

	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

Span<const WindowEvent> window_get_events(const Window* window) {
	return Span(window->events, window->event_count);
}

UVec2 window_get_framebuffer_size(const Window* window) {
	RECT rect;
	GetWindowRect(window->handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	return { window->width, window->height };
	return UVec2 { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

void window_close(Window& window) {
	window.should_close = true;
}

void window_destroy(Window* window) {
	// TODO: Delete all the resources
	delete window;
}

bool window_copy_text_to_clipboard(const Window& window, std::wstring_view text) {
	PROFILE_FUNCTION();

	if (text.length() == 0) {
		return true;
	}

	if (!OpenClipboard(window.handle)) {
		platform_log_error_message();
		return false;
	}

	bool result = false;
	HGLOBAL global_copy = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(text[0]));

	if (global_copy != nullptr) {
		wchar_t* str_copy = (wchar_t*)GlobalLock(global_copy);
		memcpy(str_copy, text.data(), text.length() * sizeof(text[0]));
		str_copy[text.length()] = 0;

		GlobalUnlock(global_copy);

		if (!SetClipboardData(CF_UNICODETEXT, global_copy)) {
			platform_log_error_message();
			result = false;
		} else {
			result = true;
		}
	}

	if (!CloseClipboard()) {
		platform_log_error_message();
		return false;
	}

	return result;
}

std::wstring_view window_read_clipboard_text(const Window& window, Arena& allocator) {
	PROFILE_FUNCTION();

	if (!OpenClipboard(window.handle)) {
		platform_log_error_message();
		return {};
	}

	if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
		return {};
	}

	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	if (!data) {
		CloseClipboard();
		return {};
	}

	wchar_t* string = (wchar_t*)GlobalLock(data);

	std::wstring_view string_copy = wstr_duplicate(string, allocator);

	GlobalUnlock(data);

	if (!CloseClipboard()) {
		platform_log_error_message();
	}

	return string_copy;
}

static KeyModifiers get_key_modifiers() {
	KeyModifiers result = KeyModifiers::None;

	if (GetAsyncKeyState(VK_CONTROL)) {
		result |= KeyModifiers::Control;
	}

	if (GetAsyncKeyState(VK_SHIFT)) {
		result |= KeyModifiers::Shift;
	}

	if (GetAsyncKeyState(VK_MENU)) {
		result |= KeyModifiers::Alt;
	}

	return result;
}

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam) {
	PROFILE_FUNCTION();
	Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

	switch (message)
	{
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_NCPAINT:
		return 0;
	case WM_NCCALCSIZE:
		return WVR_ALIGNTOP | WVR_ALIGNLEFT;
	case WM_NCACTIVATE:
		return TRUE;
	case WM_SHOWWINDOW:
		// The window is being shown
		if (wParam) {
			window_focus(window);
		}
		break;
	case WM_GETMINMAXINFO:
	{
		// NOTE: This prevents the flickering window style changes when the window foucs changes
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		int32_t x = GET_X_LPARAM(lParam);
		int32_t y = GET_Y_LPARAM(lParam);

		if (window->event_count < EVENT_BUFFER_SIZE) {
			WindowEvent& event = window->events[window->event_count];
			window->event_count++;

			event.kind = WindowEventKind::MouseMoved;
			event.data.mouse_moved.position = UVec2 { static_cast<uint32_t>(x), static_cast<uint32_t>(y) };
		}

		return 0;
	}
	case WM_LBUTTONDOWN:
	{
		if (window->event_count < EVENT_BUFFER_SIZE) {
			WindowEvent& event = window->events[window->event_count];
			window->event_count++;

			event.kind = WindowEventKind::MousePressed;
			event.data.mouse_pressed.button = MouseButton::Left;
		}

		return 0;
	}
	case WM_LBUTTONUP:
	{
		if (window->event_count < EVENT_BUFFER_SIZE) {
			WindowEvent& event = window->events[window->event_count];
			window->event_count++;

			event.kind = WindowEventKind::MouseReleased;
			event.data.mouse_released.button = MouseButton::Left;
		}

		return 0;
	}
	case WM_KEYDOWN:
	{
		KeyCode key_code{};
		if (translate_key_code(wParam, key_code)) {
			if (window->event_count < EVENT_BUFFER_SIZE) {
				WindowEvent& event = window->events[window->event_count];
				window->event_count++;

				event.kind = WindowEventKind::Key;
				event.data.key.action = InputAction::Pressed;
				event.data.key.code = key_code;
				event.data.key.modifiers = get_key_modifiers();
			}
		}

		break;
	}
	case WM_CHAR:
		if (window->event_count < EVENT_BUFFER_SIZE) {
			WindowEvent& event = window->events[window->event_count];
			window->event_count++;

			event.kind = WindowEventKind::CharTyped;
			event.data.char_typed.c = (wchar_t)wParam;
		}

		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU) {
			return 0;
		}
		break;
	case WM_KILLFOCUS:
		if (window->event_count < EVENT_BUFFER_SIZE) {
			WindowEvent& event = window->events[window->event_count];
			window->event_count++;

			event.kind = WindowEventKind::FocusLost;
		}
		break;
	case WM_CLOSE:
		window->should_close = true;
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(window_handle, message, wParam, lParam);
}

bool create_opengl_context(Window* window) {
	PROFILE_FUNCTION();
	HDC hdc = GetDC(window->handle);

	PIXELFORMATDESCRIPTOR format_descriptor = { sizeof(format_descriptor), 1 };
	format_descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_SUPPORT_COMPOSITION | PFD_DOUBLEBUFFER;
	format_descriptor.iPixelType = PFD_TYPE_RGBA;
	format_descriptor.cColorBits = 32;
	format_descriptor.cAlphaBits = 8;
	format_descriptor.iLayerType = PFD_MAIN_PLANE;
	auto format_index = ChoosePixelFormat(hdc, &format_descriptor);
	if (!format_index)
		return false;

	if (!SetPixelFormat(hdc, format_index, &format_descriptor))
		return false;

	auto active_format_index = GetPixelFormat(hdc);
	if (!active_format_index)
		return false;

	if (!DescribePixelFormat(hdc, active_format_index, sizeof(format_descriptor), &format_descriptor))
		return false;

	if ((format_descriptor.dwFlags & PFD_SUPPORT_OPENGL) != PFD_SUPPORT_OPENGL)
		return false;

	auto context = wglCreateContext(hdc);
	if (!wglMakeCurrent(hdc, context))
	{
		return false;
	}

	wglCreateContextAttribsARB = (wglCreateContextAttribsARBFunction*)wglGetProcAddress("wglCreateContextAttribsARB");
	wglChoosePixelFormatARB = (wglChoosePixelFormatARBFunction*)wglGetProcAddress("wglChoosePixelFormatARB");
	wglSwapIntervalEXT = (wglSwapIntervalEXTFunction*)wglGetProcAddress("wglSwapIntervalEXT");

	int32_t attributes[] =
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 5,
		0,
	};

	auto openGL45Context = wglCreateContextAttribsARB(hdc, context, attributes);

	wglSwapIntervalEXT(1);

	return true;
}

static void* load_gl_proc(const char* name) {
	PROFILE_FUNCTION();
	void* proc = (void*)wglGetProcAddress(name);

	if (!proc) {
		return (void*)GetProcAddress(s_opengl_module, name);
	}

	return proc;
}

bool init_opengl(Window* window) {
	PROFILE_FUNCTION();

	if (!s_opengl_module) {
		s_opengl_module = GetModuleHandleA("opengl32.dll");
	}

	if (!s_opengl_module) {
		return false;
	}

	if (!gladLoadGLLoader((GLADloadproc)load_gl_proc))
	{
		return false;
	}

	return true;
}

bool translate_key_code(WPARAM virtual_key_code, KeyCode& output) {
	PROFILE_FUNCTION();

	switch (virtual_key_code) {
	case 'A':
		output = KeyCode::A;
		return true;
	case 'C':
		output = KeyCode::C;
		return true;
	case 'V':
		output = KeyCode::V;
		return true;
	case 'X':
		output = KeyCode::X;
		return true;
	case VK_ESCAPE:
		output = KeyCode::Escape;
		return true;
	case VK_RETURN:
		output = KeyCode::Enter;
		return true;
	case VK_BACK:
		output = KeyCode::Backspace;
		return true;
	case VK_UP:
		output = KeyCode::ArrowUp;
		return true;
	case VK_DOWN:
		output = KeyCode::ArrowDown;
		return true;
	case VK_LEFT:
		output = KeyCode::ArrowLeft;
		return true;
	case VK_RIGHT:
		output = KeyCode::ArrowRight;
		return true;
	case VK_F3:
		output = KeyCode::F3;
		return true;
	case VK_HOME:
		output = KeyCode::Home;
		return true;
	case VK_END:
		output = KeyCode::End;
		return true;
	case VK_DELETE:
		output = KeyCode::Delete;
		return true;
	}

	return false;
}

//
// File system
//

static std::filesystem::path get_known_system_path(KNOWNFOLDERID id) {
	PROFILE_FUNCTION();
	PWSTR path = nullptr;

	std::filesystem::path result;

	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_IMPERSONATE, &token)) {
		printf("Failed to open process token");
	}

	if (SHGetKnownFolderPath(id, 0, token, &path) == S_OK) {
		result = path;
	} else {
		printf("SHGetKnownFolderPath: failed");
	}

	CloseHandle(token);

	CoTaskMemFree(path);
	return result;
}

std::vector<std::filesystem::path> get_user_folders(UserFolderKind kind) {
	PROFILE_FUNCTION();
	std::vector<std::filesystem::path> results;

	if (HAS_FLAG(kind, UserFolderKind::StartMenu)) {
		results.push_back(get_known_system_path(FOLDERID_CommonStartMenu));
	}

	if (HAS_FLAG(kind, UserFolderKind::Programs)) {
		results.push_back(get_known_system_path(FOLDERID_Programs));
	}

	if (HAS_FLAG(kind, UserFolderKind::Desktop)) {
		results.push_back(get_known_system_path(FOLDERID_Desktop));
	}

	return results;
}

struct AppManifestQueryState {
	IStream* manifest_input_stream;
	IAppxManifestReader* manifest_reader;
	IAppxManifestApplicationsEnumerator* apps_enumerator;
};

static void app_manifest_query_state_release(AppManifestQueryState& state) {
	PROFILE_FUNCTION();

	if (state.manifest_input_stream) {
		state.manifest_input_stream->Release();
		state.manifest_input_stream = nullptr;
	}

	if (state.manifest_reader) {
		state.manifest_reader->Release();
		state.manifest_reader = nullptr;
	}

	if (state.apps_enumerator) {
		state.apps_enumerator->Release();
		state.apps_enumerator = nullptr;
	}
}

static void log_installed_apps_query_error(std::string_view message, const std::filesystem::path& manifest_path) {
	PROFILE_FUNCTION();

	Arena& allocator = log_get_fmt_arena();

	ArenaSavePoint format_temp = arena_begin_temp(allocator);
	StringBuilder<wchar_t> builder = { &allocator };
	str_builder_append<wchar_t>(builder, L" for a manifest file: ");
	str_builder_append<wchar_t>(builder, manifest_path.wstring());

	log_error(str_builder_to_str(builder));

	arena_end_temp(format_temp);
}

static bool query_user_sid_string(Arena& allocator, winrt::hstring* out_result) {
	PROFILE_FUNCTION();

	HANDLE token{};
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		platform_log_error_message();
		return false;
	}

	DWORD return_length = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &return_length);

	ArenaSavePoint temp = arena_begin_temp(allocator);
	void* token_user_buffer = arena_alloc_aligned(allocator, return_length, 16);

	if (!GetTokenInformation(token, TokenUser, token_user_buffer, return_length, &return_length)) {
		platform_log_error_message();

		// NOTE: don't forget to end temp region
		arena_end_temp(temp);
		return false;
	}

	TOKEN_USER* token_user = reinterpret_cast<TOKEN_USER*>(token_user_buffer);

	PSID sid = token_user->User.Sid;
	LPWSTR sid_string = nullptr;
	if (!ConvertSidToStringSidW(sid, &sid_string)) {
		platform_log_error_message();

		// NOTE: don't forget to end temp region
		arena_end_temp(temp);
		return false;
	}

	*out_result = winrt::hstring(sid_string);

	LocalFree(sid_string);

	// NOTE: don't forget to end temp region
	arena_end_temp(temp);
	return true;
}

struct alignas(64) PackageProcessingTaskContext {
	Span<IAppxFactory*> factories;
	Span<winrt::Windows::ApplicationModel::Package> packages;
	Span<InstalledAppDesc> app_descs;
	uint32_t max_app_descs;
};

static IAppxFactory* create_appx_factory() {
	PROFILE_FUNCTION();
	IAppxFactory* factory = {};

	HRESULT result = CoCreateInstance(__uuidof(AppxFactory),
			nullptr,
			CLSCTX_INPROC_SERVER,
			__uuidof(IAppxFactory),
			(LPVOID*)(&factory));

	if (FAILED(result)) {
		log_error(L"failed to create 'AppxFactory'");
		return nullptr;
	}

	return factory;
}

static void task_process_package_batch(const JobContext& job_context, void* user_data) {
	PROFILE_FUNCTION();

	using namespace winrt;
	using namespace Windows::ApplicationModel;
	using namespace Windows::Management::Deployment;
	using namespace Windows::Storage;
	using namespace Windows::Foundation::Collections;
	using namespace Windows::Foundation;

	PackageProcessingTaskContext* context = reinterpret_cast<PackageProcessingTaskContext*>(user_data);
	IAppxFactory* factory = context->factories[job_context.worker_index];

	Arena& allocator = job_context.arena;

	for (const auto& package : context->packages) {
		std::filesystem::path install_path = package.InstalledPath().c_str();
		std::filesystem::path manifest_path = install_path / "AppxManifest.xml";

		if (!std::filesystem::exists(manifest_path)) {
			continue;
		}

		std::wstring_view logo_uri;
		std::wstring_view display_name;

		{
			PROFILE_SCOPE("get_display_name_and_logo_uri");
			winrt::hstring logo_uri_string = Uri::UnescapeComponent(package.Logo().Path());

			// HACK: After all of the convertions of the URI, it is left with forward slash at the start.
			//       So get rid of it.
			logo_uri = wstr_duplicate(logo_uri_string.c_str() + 1, allocator);
			display_name = wstr_duplicate(package.DisplayName().c_str(), allocator);
		}

		AppManifestQueryState manifest_state{};

		// Based on example from
		// https://learn.microsoft.com/en-us/windows/win32/appxpkg/how-to-query-package-identity-information
		HRESULT result = {};

		{
			PROFILE_SCOPE("create_file_stream");
			result = SHCreateStreamOnFileEx(manifest_path.c_str(),
					STGM_READ | STGM_SHARE_EXCLUSIVE,
					0, FALSE, nullptr,
					&manifest_state.manifest_input_stream);
		}

		if (FAILED(result)) {
			log_installed_apps_query_error("failed to create stream", manifest_path);
			continue;
		}

		{
			PROFILE_SCOPE("create_manifest_reader");
			result = factory->CreateManifestReader(
					manifest_state.manifest_input_stream,
					&manifest_state.manifest_reader);
		}

		if (FAILED(result)) {
			log_installed_apps_query_error("failed to create 'IAppxManifestReader'", manifest_path);
			app_manifest_query_state_release(manifest_state);
			continue;
		}

		{
			PROFILE_SCOPE("get_applications_from_manifest");
			result = manifest_state.manifest_reader->GetApplications(&manifest_state.apps_enumerator);
		}

		if (FAILED(result)) {
			log_installed_apps_query_error("failed to get 'IAppxManifestApplicationsEnumerator'", manifest_path);
			app_manifest_query_state_release(manifest_state);
			continue;
		}

		BOOL has_current = FALSE;
		result = manifest_state.apps_enumerator->GetHasCurrent(&has_current);
		if (FAILED(result)) {
			log_installed_apps_query_error("'IAppxManifestApplicationsEnumerator::GetHasCurrent' failed", manifest_path);
			app_manifest_query_state_release(manifest_state);
			continue;
		}

		while (has_current) {
			IAppxManifestApplication* application = nullptr;
			result = manifest_state.apps_enumerator->GetCurrent(&application);

			if (FAILED(result)) {
				log_installed_apps_query_error("'IAppxManifestApplicationsEnumerator::GetCurrent' failed", manifest_path);
				app_manifest_query_state_release(manifest_state);
				application->Release();
				break;
			}

			LPWSTR app_user_model_id = nullptr;
			result = application->GetAppUserModelId(&app_user_model_id);
			if (FAILED(result)) {
				log_installed_apps_query_error("'AppxManifestApplication::GetAppUserModelId' failed", manifest_path);
				app_manifest_query_state_release(manifest_state);
				application->Release();
				break;
			} else {
				if (context->app_descs.count >= context->max_app_descs) {
					log_error(L"failed to retrieve all of the application entries from the manifest file, because the reserved buffer is full");
					application->Release();
					break;
				}

				PROFILE_SCOPE("append_app_desc");
				
				context->app_descs.count += 1;
				InstalledAppDesc& desc = context->app_descs[context->app_descs.count - 1];
				desc.id = wstr_duplicate(app_user_model_id, allocator).data();
				desc.display_name = display_name;
				desc.logo_uri = logo_uri;

				CoTaskMemFree(app_user_model_id);
			}

			result = manifest_state.apps_enumerator->MoveNext(&has_current);
			if (FAILED(result)) {
				log_installed_apps_query_error("'IAppxManifestApplicationsEnumerator::MoveNext' failed", manifest_path);
				app_manifest_query_state_release(manifest_state);
				application->Release();
				break;
			}

			application->Release();
		}

		app_manifest_query_state_release(manifest_state);
	}
}

struct InstalledAppsQueryState {
	uint32_t worker_count;
	IAppxFactory** factories_per_worker;
	std::vector<PackageProcessingTaskContext> batches;
};

// Thanks to https://github.com/christophpurrer/cppwinrt-clang/blob/master/build.bat
InstalledAppsQueryState* platform_begin_installed_apps_query(Arena& temp_arena) {
	PROFILE_FUNCTION();

	using namespace winrt;
	using namespace Windows::ApplicationModel;
	using namespace Windows::Management::Deployment;
	using namespace Windows::Storage;
	using namespace Windows::Foundation::Collections;
	using namespace Windows::Foundation;

	winrt::hstring sid_hstring;
	if (!query_user_sid_string(temp_arena, &sid_hstring)) {
		log_error(L"failed to get SID of the current user");
		return nullptr;
	}

	InstalledAppsQueryState* query_state = arena_alloc<InstalledAppsQueryState>(temp_arena);

	// HACK: Allocated in the arena, thus need to manually default construct
	new (query_state) InstalledAppsQueryState();

	// TODO: Move this to the `platform_initialize`
	{
		PROFILE_SCOPE("init_winrt");
		winrt::init_apartment();
	}

	// All of these are temp allocations, however it is the users resposibility to clear temp arena.
	query_state->worker_count = job_system_get_worker_count() + 1; // +1 for the main thread
	query_state->factories_per_worker = arena_alloc_array<IAppxFactory*>(temp_arena, query_state->worker_count);

	{
		PROFILE_SCOPE("create factories");
		for (uint32_t i = 0; i < query_state->worker_count; i++) {
			query_state->factories_per_worker[i] = create_appx_factory();
		}
	}

	try {
		PROFILE_SCOPE("query_packages");

		size_t batch_size = 2;
		size_t max_app_descs_per_batch = 8;

		{
			PROFILE_SCOPE("generate_batches");
			PackageManager package_manager;
			auto packages_iterator = package_manager.FindPackagesForUser(sid_hstring);

			Span<IAppxFactory*> factories_per_worker = Span<IAppxFactory*>(
					query_state->factories_per_worker,
					query_state->worker_count);

			PackageProcessingTaskContext* current_batch = nullptr;
			for (auto package : packages_iterator) {
				if (current_batch == nullptr || current_batch->packages.count == batch_size) {
					Package* package_buffer = arena_alloc_array<Package>(temp_arena, batch_size);
					InstalledAppDesc* descs = arena_alloc_array<InstalledAppDesc>(temp_arena, max_app_descs_per_batch);

					current_batch = &query_state->batches.emplace_back();
					current_batch->factories = factories_per_worker;
					current_batch->packages = Span<Package>(package_buffer, 0);
					current_batch->app_descs = Span<InstalledAppDesc>(descs, 0);
					current_batch->max_app_descs = max_app_descs_per_batch;
				}

				// increment the count first, to avoid an out of bounds panic in the `Span<T>`
				current_batch->packages.count += 1;

				Package* package_target_location = &current_batch->packages[current_batch->packages.count - 1];
				// HACK: `Package` class contains a single pointer to the implementation,
				//       so if we set the whole object to zero, the pointer will be null,
				//       and won't pass the validity check during it's release
				//
				// NOTE: Placement new isn't an option here, becase the `Package` class isn't default constractible.
				memset(package_target_location, 0, sizeof(*package_target_location));
				*package_target_location = package;
			}
		}

		// submit jobs
		for (auto& batch : query_state->batches) {
			job_system_submit(task_process_package_batch, &batch);
		}
	} catch (const winrt::hresult_error& e) {
		winrt::hstring message = e.message();
		log_error(std::wstring_view(message.c_str(), message.size()));
	}

	return query_state;
}

std::vector<InstalledAppDesc> platform_finish_installed_apps_query(InstalledAppsQueryState* query_state,
		Arena& job_execution_arena) {
	PROFILE_FUNCTION();

	std::vector<InstalledAppDesc> apps;

	// wait for all the jobs to complete
	job_system_wait_for_all(job_execution_arena);

	for (const auto& batch : query_state->batches) {
		for (const auto& installed_app : batch.app_descs) {
			apps.push_back(installed_app);
		}
	} 

	// delete factories
	{
		PROFILE_SCOPE("delete_factories");
		for (uint32_t i = 0; i < query_state->worker_count; i++) {
			query_state->factories_per_worker[i]->Release();
		}
	}

	// HACK: manually call the desctructor, because the `query_state` was allocated in the arena
	query_state->~InstalledAppsQueryState();

	return apps;
}

bool platform_launch_installed_app(const wchar_t* app_id) {
	PROFILE_FUNCTION();

	IApplicationActivationManager* activation_manager = nullptr;

	{
		PROFILE_SCOPE("create activation manager");
		HRESULT result = CoCreateInstance(__uuidof(ApplicationActivationManager),
				nullptr,
				CLSCTX_INPROC_SERVER,
				__uuidof(IApplicationActivationManager),
				(LPVOID*)(&activation_manager));

		if (FAILED(result)) {
			log_error(L"failed to create 'IApplicationActivationManager'");
			return false;
		}
	}

	DWORD process_id = 0;
	HRESULT result = activation_manager->ActivateApplication(app_id, nullptr, AO_NONE, &process_id);
	if (FAILED(result)) {
		log_error(L"failed to launch installed app");
		return false;
	}

	{
		PROFILE_SCOPE("release activation manager");
		activation_manager->Release();
	}

	return true;
}

static Bitmap extract_icon_bitmap(HICON icon, Arena& arena) {
	PROFILE_FUNCTION();

	ICONINFO icon_info{};
	if (!GetIconInfo(icon, &icon_info)) {
		platform_log_error_message();
		return {};
	}

	HDC screen_dc = GetDC(nullptr);
		
	HBITMAP color_bitmap = icon_info.hbmColor;

	SIZE bitmap_size{};

	BITMAP bitmap;
	GetObject(color_bitmap, sizeof(bitmap), &bitmap);

	bitmap_size.cx = bitmap.bmWidth;
	bitmap_size.cy = bitmap.bmHeight;

	size_t pixel_count = bitmap_size.cx * bitmap_size.cy;
	uint32_t* pixels = arena_alloc_array<uint32_t>(arena, pixel_count);

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = bitmap.bmWidth;
	bmi.bmiHeader.biHeight = -bitmap.bmHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	GetDIBits(screen_dc, color_bitmap, 0, bitmap.bmHeight, pixels, &bmi, DIB_RGB_COLORS);

	for (size_t i = 0; i < pixel_count; i++) {
		uint32_t v = pixels[i];

		uint8_t r = (v >> 16) & 0xff;
		uint8_t g = (v >> 8) & 0xff;
		uint8_t b = (v >> 0) & 0xff;
		uint8_t a = (v >> 24) & 0xff;

		pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
	}

	Bitmap b{};
	b.width = bitmap_size.cx;
	b.height = bitmap_size.cy;
	b.pixels = pixels;

	return b;
}

static Bitmap extract_file_icon(const wchar_t* path, Arena& arena) {
	PROFILE_FUNCTION();

	SHFILEINFO file_info{};
	DWORD_PTR result = SHGetFileInfoW(path, 0, &file_info, sizeof(file_info), SHGFI_ICON);
	if (result == 0) {
		platform_log_error_message();
		return Bitmap{};
	}

	Bitmap bitmap = extract_icon_bitmap(file_info.hIcon, arena);

	DestroyIcon(file_info.hIcon);

	return bitmap;
}

SystemIconHandle fs_query_file_icon(const std::filesystem::path& path) {
	PROFILE_FUNCTION();

	if (!std::filesystem::exists(path)) {
		return nullptr;
	}

	SHFILEINFO file_info{};
	DWORD_PTR result = SHGetFileInfoW(path.c_str(), 0, &file_info, sizeof(file_info), SHGFI_ICON);
	if (result == 0) {
		return nullptr;
	}

	return file_info.hIcon;
}

uint32_t fs_query_file_icon_id(const std::filesystem::path& path) {
	PROFILE_FUNCTION();

	if (!std::filesystem::exists(path)) {
		return INVALID_ICON_ID;
	}

	SHFILEINFO file_info{};
	DWORD_PTR result = SHGetFileInfoW(path.c_str(), 0, &file_info, sizeof(file_info), SHGFI_SYSICONINDEX);
	if (result == 0) {
		return INVALID_ICON_ID;
	}

	return (uint32_t)file_info.iIcon;
}

void fs_release_file_icon(SystemIconHandle icon) {
	PROFILE_FUNCTION();
	DestroyIcon((HICON)icon);
}

Bitmap fs_extract_icon_bitmap(SystemIconHandle icon, Arena& bitmap_allocator) {
	PROFILE_FUNCTION();
	return extract_icon_bitmap((HICON)icon, bitmap_allocator);
}

Bitmap get_file_icon(const std::filesystem::path& path, Arena& arena) {
	PROFILE_FUNCTION();
	if (!std::filesystem::exists(path)) {
		return {};
	}

	return extract_file_icon(path.c_str(), arena);
}

std::filesystem::path fs_resolve_shortcut(const std::filesystem::path& path) {
	PROFILE_FUNCTION();
	
	if (t_shortcut_resolver.state == ShortcutResolverState::Invalid) {
		log_error(L"shortcut resolver is invalid");
		return {};
	} else if (t_shortcut_resolver.state == ShortcutResolverState::NotCreated) {
		shortcut_resolver_create_for_thread();
	}

	// From: https://learn.microsoft.com/en-us/windows/win32/shell/links?redirectedfrom=MSDN

    WCHAR szGotPath[MAX_PATH]; 
    // WCHAR szDescription[MAX_PATH]; 
    WIN32_FIND_DATA wfd; 

	WCHAR wsz[MAX_PATH]; 

	// Load the shortcut. 
	HRESULT hres = t_shortcut_resolver.persistent_file_interface->Load(path.wstring().c_str(), STGM_READ); 
	
	if (SUCCEEDED(hres)) 
	{ 
		// Resolve the link. 
		// NOTE: Not sure whether it is ok to pass a nullptr as hwnd
		hres = t_shortcut_resolver.shell_link_interface->Resolve(nullptr, SLR_NO_UI); 

		if (SUCCEEDED(hres)) 
		{ 
			// Get the path to the link target. 
			hres = t_shortcut_resolver.shell_link_interface->GetPath(szGotPath,
					MAX_PATH,
					(WIN32_FIND_DATA*)&wfd,
					SLGP_SHORTPATH); 

			if (SUCCEEDED(hres)) 
			{ 
				// Get the description of the target. 
				// hres = s_shortcut_resolver.shell_link_interface->GetDescription(szDescription, MAX_PATH); 

				if (SUCCEEDED(hres))
				{
					// Handle success
					return std::filesystem::path(szGotPath);
				}
				else
				{
					// TODO: Handle the error
					return {};
				}
			}
		} 
	} 

	return {};
}

static RunFileResult run_executable_file(const std::filesystem::path& path) {
	PROFILE_FUNCTION();

	STARTUPINFO start_up_info;
	PROCESS_INFORMATION process_info;

	ZeroMemory(&start_up_info, sizeof(start_up_info));
	ZeroMemory(&process_info, sizeof(process_info));

	start_up_info.cb = sizeof(start_up_info);

	std::filesystem::path working_directory = path.parent_path();

	// HACK: Don't why, but the command line arguments must be a mutable string
	wchar_t command_line_arguments[1];
	command_line_arguments[0] = 0;

	bool result = CreateProcessW(path.c_str(), 
			command_line_arguments,
			nullptr, nullptr, 
			false, 0, nullptr,
			working_directory.c_str(),
			&start_up_info,
			&process_info);

	CloseHandle(process_info.hProcess);
	CloseHandle(process_info.hThread);

	return result ? RunFileResult::Ok : RunFileResult::OtherError;
}

RunFileResult platform_run_file(const std::filesystem::path& path, bool run_as_admin) {
	PROFILE_FUNCTION();

	if (!std::filesystem::exists(path)) {
		return RunFileResult::PathNotFound;
	}

	if (!run_as_admin && path.extension() == ".exe") {
		return run_executable_file(path);
	}

	// NOTE: open - is can be used to lauch an exe
	//       runas - is for lauching as admin
	const wchar_t* operation = run_as_admin ? L"runas" : nullptr; // nullptr means a default operation

	INT_PTR result = (INT_PTR)ShellExecute(nullptr, // window is null
			operation,
			path.wstring().c_str(),
			L"",
			nullptr,
			SW_SHOW);

	if (result >= 32) {
		return RunFileResult::Ok;
	}

	switch (result) {
	case 0:
	case SE_ERR_OOM:
		return RunFileResult::OutOfMemory;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	// case SE_ERR_PNF: <- duplicate case
	case SE_ERR_DLLNOTFOUND:
	// case SE_ERR_FNF: <- duplicate case
		return RunFileResult::PathNotFound;
	case ERROR_BAD_FORMAT:
		return RunFileResult::BadFormat;
	case SE_ERR_ACCESSDENIED:
		return RunFileResult::AccessDenied;
	case SE_ERR_ASSOCINCOMPLETE:
	case SE_ERR_DDEBUSY:
	case SE_ERR_DDEFAIL:
	case SE_ERR_DDETIMEOUT:
	case SE_ERR_NOASSOC:
	case SE_ERR_SHARE:
		return RunFileResult::OtherError;
	}

	return RunFileResult::OtherError;
}
