#include "hook_config.h"

#include <windows.h>
#include <iostream>

struct State {
	bool space_pressed;
	bool alt_pressed;
};

static HookConfig s_config;
static State s_state;

extern "C" __declspec(dllexport) void init_keyboard_hook(const HookConfig& config) {
	std::cout << "init hook\n";
	s_config = config;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK keyboard_hook(int code, WPARAM wParam, LPARAM lParam) {
	if (code == HC_ACTION) {
		KBDLLHOOKSTRUCT* hook = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
		DWORD virtual_key = hook->vkCode;
		DWORD scan_code = hook->scanCode;

		switch (wParam) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			switch (virtual_key) {
			case VK_SPACE:
				s_state.space_pressed = true;
				break;
			case VK_LMENU:
			case VK_RMENU:
				s_state.alt_pressed = true;
				break;
			}

			break;
		case WM_SYSKEYUP:
		case WM_KEYUP:
			if (s_state.space_pressed && s_state.alt_pressed) {
				s_config.app_enable_fn();
			}

			switch (virtual_key) {
			case VK_SPACE:
				s_state.space_pressed = false;
				break;
			case VK_LMENU:
			case VK_RMENU:
				s_state.alt_pressed = false;
				break;
			}

			break;
		}
	}

	return CallNextHookEx(NULL, code, wParam, lParam);
}
