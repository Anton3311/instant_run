#include "platform.h"

#include <string>
#include <iostream>

#include <Windows.h>
#include <Shlobj.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <glad/glad.h>

static const wchar_t* WINDOW_CLASS_NAME = L"InstantRun";

constexpr int32_t WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int32_t WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int32_t WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;

//
// OpenGL
//

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

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam);
bool create_opengl_context(Window* window);
bool init_opengl(Window* window);
bool translate_key_code(WPARAM virtual_key_code, KeyCode& output);

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title) {
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
		WS_OVERLAPPEDWINDOW,
		window_x,
		window_y,
		static_cast<int>(window->width),
		static_cast<int>(window->height),
		nullptr,
		nullptr,
		GetModuleHandleW(nullptr),
		nullptr);

	if (window->handle == nullptr)
	{
		return nullptr;
	}

	LONG_PTR style = GetWindowLongPtr(window->handle, GWL_STYLE);
	style |= WS_THICKFRAME;
	style &= ~WS_CAPTION;
	SetWindowLongPtr(window->handle, GWL_STYLE, style);

	MARGINS margins{};
	DwmExtendFrameIntoClientArea(window->handle, &margins);

	SetWindowPos(window->handle, NULL, 0, 0, width, height, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOCOPYBITS);
	ShowWindow(window->handle, SW_SHOW);

	SetWindowLongPtrW(window->handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	create_opengl_context(window);
	init_opengl(window);

	return window;
}

void swap_window_buffers(Window* window) {
	SwapBuffers(GetDC(window->handle));
}

bool window_should_close(const Window* window) {
	return window->should_close;
}

void poll_window_events(Window* window) {
	window->event_count = 0;

	MSG message{};
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

Span<const WindowEvent> get_window_events(const Window* window) {
	return Span(window->events, window->event_count);
}

UVec2 get_window_framebuffer_size(const Window* window) {
	RECT rect;
	GetWindowRect(window->handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	return { window->width, window->height };
	return UVec2 { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

void close_window(Window& window) {
	window.should_close = true;
}

void destroy_window(Window* window) {
	// TODO: Delete all the resources
	delete window;
}

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam) {
	Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

	switch (message)
	{
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_NCPAINT:
		return 0;
	case WM_NCCALCSIZE:
		return WVR_ALIGNTOP | WVR_ALIGNLEFT;
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
	case WM_CLOSE:
		window->should_close = true;
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(window_handle, message, wParam, lParam);
}

bool create_opengl_context(Window* window) {
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

static void* LoadGLProc(const char* name)
{
	void* proc = wglGetProcAddress(name);

	if (!proc)
	{
		HMODULE openGlModule = GetModuleHandleA("opengl32.dll");
		if (!openGlModule)
			return 0;

		return GetProcAddress(openGlModule, name);
	}
	return proc;
}

bool init_opengl(Window* window) {
	if (!gladLoadGLLoader((GLADloadproc)LoadGLProc))
	{
		return false;
	}

	return true;
}

bool translate_key_code(WPARAM virtual_key_code, KeyCode& output) {
	switch (virtual_key_code) {
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
	}

	return false;
}

//
// File system
//

static std::filesystem::path get_known_system_path(KNOWNFOLDERID id) {
	PWSTR path = nullptr;

	std::filesystem::path result;

	if (SHGetKnownFolderPath(id, 0, nullptr, &path) == S_OK) {
		result = path;
	}

	CoTaskMemFree(path);
	return result;
}

std::vector<std::filesystem::path> get_start_menu_folder_path() {
	std::vector<std::filesystem::path> results;

 	results.push_back(get_known_system_path(FOLDERID_CommonStartMenu));
 	results.push_back(get_known_system_path(FOLDERID_Programs));

	return results;
}

Bitmap get_file_icon(const std::filesystem::path& path) {
	if (!std::filesystem::exists(path)) {
		return {};
	}

	HICON small_icon{};

	UINT icon_count = ExtractIconExW(path.wstring().c_str(), 0, &small_icon, nullptr, 1);
	if (icon_count != 1) {
		return {};
	}

	ICONINFO icon_info{};
	if (!GetIconInfo(small_icon, &icon_info)) {
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
	uint32_t* pixels = new uint32_t[pixel_count * 4];

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
