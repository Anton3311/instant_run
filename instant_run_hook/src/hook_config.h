#pragma once

struct HookConfig;

using ApplicationEnableFunction = void(*)();
using InitKeyboardHookFunction = void(*)(const HookConfig& config);

struct HookConfig {
	ApplicationEnableFunction app_enable_fn;
};
