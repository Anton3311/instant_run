#include "platform.h"

#include <string>

#include <Windows.h>
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

struct Window {
	std::wstring title;
	uint32_t width;
	uint32_t height;

	HWND handle;

	bool should_close;
};

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam);
bool create_opengl_context(Window* window);
bool init_opengl(Window* window);

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title) {
	Window* window = new Window();
	window->title = title;
	window->width = width;
	window->height = height;

	WNDCLASSW window_class{};
	window_class.lpfnWndProc = window_procedure;
	window_class.hInstance = GetModuleHandleA(nullptr);
	window_class.lpszClassName = WINDOW_CLASS_NAME;

	if (!RegisterClassW(&window_class))
	{
		return nullptr;
	}

	window->handle = CreateWindowExW(0,
		WINDOW_CLASS_NAME,
		window->title.c_str(),
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
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
	MSG message{};
	while (PeekMessageW(&message, window->handle, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

UVec2 get_window_framebuffer_size(const Window* window) {
	RECT rect;
	GetWindowRect(window->handle, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;
	return UVec2 { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

void destroy_window(Window* window) {
	delete window;
}

LRESULT window_procedure(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam) {
	Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

	switch (message)
	{
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_NCCALCSIZE:
	{
		return WVR_ALIGNTOP | WVR_ALIGNLEFT;
	}
	case WM_CLOSE:
		window->should_close = true;
		PostQuitMessage(0);
		break;
	}

	return DefWindowProc(window_handle, message, wParam, lParam);
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
