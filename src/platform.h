#pragma once

#include <stdint.h>
#include <string_view>

#include "core.h"

struct Window;

Window* create_window(uint32_t width, uint32_t height, std::wstring_view title);

void swap_window_buffers(Window* window);

bool window_should_close(const Window* window);
void poll_window_events(Window* window);

UVec2 get_window_framebuffer_size(const Window* window);

void destroy_window(Window* window);
