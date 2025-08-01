#include "platform.h"

#include <string>
#include <iostream>

#include <Windows.h>
#include <Shlobj.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objbase.h>
#include <glad/glad.h>

struct ShortcutResolverState {
	IPersistFile* persistent_file_interface; 
	IShellLink* shell_link_interface;
	bool is_valid;
};

static ShortcutResolverState s_shortcut_resolver;

void initialize_platform() {
	PROFILE_FUNCTION();
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	s_shortcut_resolver = {};

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called. 
    HRESULT hres = CoCreateInstance(CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_IShellLink,
			(LPVOID*)&s_shortcut_resolver.shell_link_interface); 

    if (SUCCEEDED(hres)) 
    { 
        // Get a pointer to the IPersistFile interface. 
        hres = s_shortcut_resolver.shell_link_interface->QueryInterface(IID_IPersistFile,
				(void**)&s_shortcut_resolver.persistent_file_interface); 
        
        if (SUCCEEDED(hres)) 
		{
			s_shortcut_resolver.is_valid = true;
		}
	}
}

void shutdown_platform() {
	PROFILE_FUNCTION();
	
	if (s_shortcut_resolver.persistent_file_interface) {
		s_shortcut_resolver.persistent_file_interface->Release();
	}

	if (s_shortcut_resolver.shell_link_interface) {
		s_shortcut_resolver.shell_link_interface->Release();
	}

	s_shortcut_resolver = {};

	CoUninitialize();
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

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title) {
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

	MARGINS margins{ 1, 1, 1, 1 };
	DwmExtendFrameIntoClientArea(window->handle, &margins);

	SetWindowPos(window->handle, NULL, 0, 0, width, height, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOCOPYBITS);
	ShowWindow(window->handle, SW_SHOW);

	SetWindowLongPtrW(window->handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	create_opengl_context(window);
	init_opengl(window);

	return window;
}

void swap_window_buffers(Window* window) {
	PROFILE_FUNCTION();
	SwapBuffers(GetDC(window->handle));
}

bool window_should_close(const Window* window) {
	return window->should_close;
}

void poll_window_events(Window* window) {
	PROFILE_FUNCTION();
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
	PROFILE_FUNCTION();
	PWSTR path = nullptr;

	std::filesystem::path result;

	if (SHGetKnownFolderPath(id, 0, nullptr, &path) == S_OK) {
		result = path;
	}

	CoTaskMemFree(path);
	return result;
}

std::vector<std::filesystem::path> get_start_menu_folder_path() {
	PROFILE_FUNCTION();
	std::vector<std::filesystem::path> results;

 	results.push_back(get_known_system_path(FOLDERID_CommonStartMenu));
 	results.push_back(get_known_system_path(FOLDERID_Programs));

	return results;
}

Bitmap get_file_icon(const std::filesystem::path& path) {
	PROFILE_FUNCTION();
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

std::filesystem::path read_symlink_path(const std::filesystem::path& path) {
	PROFILE_FUNCTION();
	if (!std::filesystem::exists(path)) {
		return {};
	}

	HANDLE file_handle = CreateFileW(path.wstring().c_str(),
			0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

	if (file_handle == INVALID_HANDLE_VALUE) {
		CloseHandle(file_handle);
		return {};
	}

	wchar_t buffer[MAX_PATH];

	// NOTE: Can return the required buffer size, if a null buffer is passed
	size_t path_length = GetFinalPathNameByHandleW(file_handle, buffer, sizeof(buffer) / sizeof(TCHAR), FILE_NAME_NORMALIZED);

	if (path_length) {
		auto result = std::filesystem::path(std::wstring(buffer, path_length));

		CloseHandle(file_handle);

		return result;
	}

	CloseHandle(file_handle);

	return {};
}

// Господи помилуй
std::filesystem::path read_shortcut_path(const std::filesystem::path& path) {
	PROFILE_FUNCTION();
	
	if (!s_shortcut_resolver.is_valid) {
		return {};
	}

	// From: https://learn.microsoft.com/en-us/windows/win32/shell/links?redirectedfrom=MSDN

    WCHAR szGotPath[MAX_PATH]; 
    // WCHAR szDescription[MAX_PATH]; 
    WIN32_FIND_DATA wfd; 

	WCHAR wsz[MAX_PATH]; 

	// Load the shortcut. 
	HRESULT hres = s_shortcut_resolver.persistent_file_interface->Load(path.wstring().c_str(), STGM_READ); 
	
	if (SUCCEEDED(hres)) 
	{ 
		// Resolve the link. 
		// NOTE: Not sure whether it is ok to pass a nullptr as hwnd
		hres = s_shortcut_resolver.shell_link_interface->Resolve(nullptr, SLR_NO_UI); 

		if (SUCCEEDED(hres)) 
		{ 
			// Get the path to the link target. 
			hres = s_shortcut_resolver.shell_link_interface->GetPath(szGotPath,
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

	bool result = CreateProcessW(path.c_str(), 
			L"",
			nullptr, nullptr, 
			false, 0, nullptr,
			working_directory.c_str(),
			&start_up_info,
			&process_info);

	CloseHandle(process_info.hProcess);
	CloseHandle(process_info.hThread);

	return result ? RunFileResult::Ok : RunFileResult::OtherError;
}

RunFileResult run_file(const std::filesystem::path& path, bool run_as_admin) {
	PROFILE_FUNCTION();

	if (!std::filesystem::exists(path)) {
		return RunFileResult::PathNotFound;
	}

	if (!run_as_admin && path.extension() == ".exe") {
		return run_executable_file(path);
	}

	// NOTE: open - is can be used to lauch an exe
	//       runas - is for lauching as admin
	const wchar_t* operation = run_as_admin ? L"runas" : L"open";

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
