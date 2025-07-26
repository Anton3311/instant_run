#pragma once

#include "core.h"

#include <string_view>
#include <filesystem>
#include <stb_trutype.h>

struct Window;

enum class TextureFormat {
	R8_G8_B8_A8,
};

struct Texture {
	uint32_t internal_id;

	TextureFormat format;

	uint32_t width;
	uint32_t height;
};

struct Font {
	float size;
	size_t glyph_count;
	stbtt_bakedchar* glyphs;

	uint32_t char_range_start;

	Texture atlas;
};

void initialize_renderer(Window* window);
void shutdown_renderer();

Texture create_texture(TextureFormat format, uint32_t width, uint32_t height, const void* data);
void delete_texture(const Texture& texture);

Font create_font(const uint8_t* data, size_t data_size, float font_size);
Font load_font_from_file(const std::filesystem::path& path, float font_size);
void delete_font(const Font& font);

void begin_frame();
void end_frame();

void draw_line(Vec2 a, Vec2 b, Color color);
void draw_rect(const Rect& rect, Color color);
void draw_rounded_rect(const Rect& rect, Color color, float corner_radius);
void draw_rect_lines(const Rect& rect, Color color);
void draw_text(std::wstring_view text, Vec2 position, const Font& font, Color color);
