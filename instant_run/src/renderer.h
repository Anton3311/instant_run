#pragma once

#include "core.h"
#include "math.h"
#include "color.h"

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
	stbtt_fontinfo info;
	stbtt_bakedchar* glyphs;

	Span<const RangeU32> char_ranges;

	Texture atlas;

	// Metrics
	int32_t ascent; // coordinate above baseline
	int32_t descent; // coordinate below baseline
	int32_t line_gap;
};

void initialize_renderer(Window* window);
void shutdown_renderer();

Texture create_texture(TextureFormat format, uint32_t width, uint32_t height, const void* data);
void upload_texture_region(const Texture& texture, UVec2 offset, UVec2 size, const void* data);
bool load_texture(const std::filesystem::path& path, Texture& out_texture);
void delete_texture(const Texture& texture);

struct TexturePixelData {
	void* pixels;
	TextureFormat format;
	uint32_t width;
	uint32_t height;
};

TexturePixelData texture_load_pixel_data(const std::filesystem::path& path);
void texture_release_pixel_data(const TexturePixelData& pixel_data);

TexturePixelData texture_downscale(const TexturePixelData& source, uint32_t target_size, Arena& allocator);

Font create_font(const uint8_t* data, size_t data_size, float font_size, Arena& arena);
Font load_font_from_file(const std::filesystem::path& path, float font_size, Arena& arena);
void delete_font(const Font& font);

uint32_t font_get_glyph_index(const Font& font, uint32_t codepoint);
float font_get_height(const Font& font);

void begin_frame();
void end_frame();

void draw_line(Vec2 a, Vec2 b, Color color);
void draw_rect(const Rect& rect, Color color);
void draw_rect(const Rect& rect, Color color, const Texture& texture, Rect uv_rect);
void draw_rounded_rect(const Rect& rect, Color color, float corner_radius);
void draw_rect_lines(const Rect& rect, Color color);
void draw_text(std::wstring_view text, Vec2 position, const Font& font, Color color, float max_width = FLT_MAX);
