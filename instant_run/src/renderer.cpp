#include "renderer.h"

#include <glad/glad.h>
#include <stb_image.h>

#include <stdlib.h>
#include <vector>
#include <fstream>
#include <assert.h>

#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>

#include "platform.h"

struct QuadVertex {
	Vec2 position;
	Vec2 uv;
	uint32_t color;
};

struct DrawCommand {
	uint32_t first_index;
	uint32_t index_count;
	uint32_t texture_id;
};

static constexpr uint32_t ROUNDED_CORNER_VERTEX_COUNT = 3;

struct RendererState {
	Window* window;

	GLuint shader_id;
	GLuint vertex_array_id;
	GLuint vertex_buffer_id;
	GLuint index_buffer_id;

	std::vector<QuadVertex> vertices;
	std::vector<uint32_t> indices;

	std::vector<DrawCommand> commands;

	Texture white_texture;

	Vec2 rounded_corner_vertices[ROUNDED_CORNER_VERTEX_COUNT];
};

static RendererState s_state;

//
// Renderer Functions
//

static void debug_message_callback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* user_param)
{
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_LOW:
		printf("%s", message);
		return;
	case GL_DEBUG_SEVERITY_MEDIUM:
		printf("%s", message);
		return;
	case GL_DEBUG_SEVERITY_HIGH:
		printf("%s", message);
		return;
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		printf("%s", message);
		return;
	}
}

static const char* s_vertex_shader_source = R"(
#version 450

layout(location = 0) in vec2 i_Position;
layout(location = 1) in vec2 i_UV;
layout(location = 2) in uint i_Color;

layout(location = 0) uniform vec2 u_ProjectionParams;

out vec4 a_VertexColor;
out vec2 a_UV;

void main()
{
	vec2 position = i_Position * u_ProjectionParams;
	position.y = 1.0f - position.y;
	gl_Position = vec4(position * 2.0f - vec2(1.0f), 0.0f, 1.0f);

	uint r = (i_Color >> 24) & 0xff;
	uint g = (i_Color >> 16) & 0xff;
	uint b = (i_Color >> 8) & 0xff;
	uint a = (i_Color >> 0) & 0xff;

	a_VertexColor = vec4(float(r), float(g), float(b), float(a)) * (1.0f / 255.0f);
	a_UV = i_UV;
}
)";

static const char* s_fragment_shader_source = R"(
#version 450

in vec4 a_VertexColor;
in vec2 a_UV;

layout(location = 0) out vec4 o_Color;

uniform sampler2D u_Texture;

void main()
{
	o_Color = a_VertexColor * texture(u_Texture, a_UV);
}
)";

static GLuint create_shader_from_source(const char* source, GLenum type) {
	PROFILE_FUNCTION();

	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, 0);
	glCompileShader(shader);

	GLint compiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

	if (compiled)
		return shader;

	GLint maxLength = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

	char* message = new char[maxLength + 1];

	message[maxLength] = 0;

	glGetShaderInfoLog(shader, maxLength, &maxLength, message);
	glDeleteShader(shader);

	printf("%s", message);

	delete[] message;

	return 0;
}

static bool create_shaders() {
	PROFILE_FUNCTION();

	GLuint vertex_shader = create_shader_from_source(s_vertex_shader_source, GL_VERTEX_SHADER);
	if (vertex_shader == 0)
		return false;

	GLuint fragment_shader = create_shader_from_source(s_fragment_shader_source, GL_FRAGMENT_SHADER);
	if (fragment_shader == 0)
	{
		// Vertex shader was created, so now it must be deleted
		glDeleteShader(vertex_shader);
		return false;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	glLinkProgram(program);

	GLint link_status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);

	if (link_status)
	{
		glDetachShader(program, vertex_shader);
		glDetachShader(program, fragment_shader);

		glDeleteShader(vertex_shader);
		glDeleteShader(fragment_shader);

		s_state.shader_id = program;

		return true;
	}

	GLint max_length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);

	char* message = new char[max_length + 1];
	message[max_length] = 0;

	glGetProgramInfoLog(program, max_length, &max_length, message);

	printf("%s", message);

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	glDeleteProgram(program);

	delete[] message;

	return false;
}

static void create_buffers() {
	PROFILE_FUNCTION();

	glCreateBuffers(1, &s_state.vertex_buffer_id);
	glCreateBuffers(1, &s_state.index_buffer_id);

	glCreateVertexArrays(1, &s_state.vertex_array_id);

	glBindVertexArray(s_state.vertex_array_id);

	glBindBuffer(GL_ARRAY_BUFFER, s_state.vertex_buffer_id);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_state.index_buffer_id);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, position));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, uv));
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, color));

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

}

inline static void push_texture(const Texture& texture) {
	if (s_state.commands.size() == 0) {
		DrawCommand& command = s_state.commands.emplace_back();
		command.texture_id = texture.internal_id;
		return;
	}

	DrawCommand last_command = s_state.commands.back();

	if (last_command.texture_id == texture.internal_id) {
		return;
	}

	DrawCommand& command = s_state.commands.emplace_back();
	command.first_index = last_command.first_index + last_command.index_count;
	command.texture_id = texture.internal_id;
}

void initialize_renderer(Window* window) {
	PROFILE_FUNCTION();

	s_state.window = window;

	create_shaders();
	create_buffers();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	{
		uint32_t white_pixel = 0xffffffff;

		uint32_t pixels[] = { 0xffffffff, 0xff00ff00, 0xffff00ff, 0xffffff00 };

		s_state.white_texture = create_texture(TextureFormat::R8_G8_B8_A8, 1, 1, pixels);
	}

	{
		float angle_step = M_PI / (2.0f * ((float)ROUNDED_CORNER_VERTEX_COUNT - 1));

		for (size_t i = 0; i < ROUNDED_CORNER_VERTEX_COUNT; i++) {
			float angle = angle_step * (float)i;
			s_state.rounded_corner_vertices[i] = Vec2 { std::cos(angle), std::sin(angle) };
		}
	}
}

void shutdown_renderer() {
	PROFILE_FUNCTION();
	delete_texture(s_state.white_texture);

	s_state = {};
}

Texture create_texture(TextureFormat format, uint32_t width, uint32_t height, const void* data) {
	PROFILE_FUNCTION();
	Texture texture{};
	texture.width = width;
	texture.height = height;
	texture.format = format;

	glGenTextures(1, &texture.internal_id);

	GLenum texture_format{};
	GLenum internal_format{};

	switch (format) {
	case TextureFormat::R8_G8_B8_A8:
		texture_format = GL_RGBA;
		internal_format = GL_RGBA8;
		break;
	}

	glBindTexture(GL_TEXTURE_2D, texture.internal_id);

	glTextureStorage2D(texture.internal_id, 1, internal_format, width, height);

	if (data) {
		glTextureSubImage2D(texture.internal_id, 0, 0, 0, width, height, texture_format, GL_UNSIGNED_BYTE, data);
	}

	glTextureParameteri(texture.internal_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(texture.internal_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	return texture;
}

void upload_texture_region(const Texture& texture, UVec2 offset, UVec2 size, const void* data) {
	PROFILE_FUNCTION();
	GLenum texture_format{};

	switch (texture.format) {
	case TextureFormat::R8_G8_B8_A8:
		texture_format = GL_RGBA;
		break;
	}

	glBindTexture(GL_TEXTURE_2D, texture.internal_id);

	if (data) {
		glTextureSubImage2D(texture.internal_id,
				0,
				offset.x,
				offset.y,
				size.x,
				size.y,
				texture_format,
				GL_UNSIGNED_BYTE,
				data);
	}
}

bool load_texture(const std::filesystem::path& path, Texture& out_texture) {
	PROFILE_FUNCTION();
	if (!std::filesystem::exists(path)) {
		return false;
	}

	stbi_set_flip_vertically_on_load(true);

	int width, height, channels;
	stbi_uc* pixel_data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);

	if (!pixel_data) {
		return false;
	}

	if (channels == 4) {
		out_texture = create_texture(TextureFormat::R8_G8_B8_A8,
				static_cast<uint32_t>(width),
				static_cast<uint32_t>(height),
				pixel_data);

		free(pixel_data);
		return true;
	} else {
		free(pixel_data);
		return false;
	}

	return true;
}

void delete_texture(const Texture& texture) {
	PROFILE_FUNCTION();
	glDeleteTextures(1, &texture.internal_id);
}

TexturePixelData texture_load_pixel_data(const std::filesystem::path& path) {
	PROFILE_FUNCTION();

	if (!std::filesystem::exists(path)) {
		return {};
	}

	stbi_set_flip_vertically_on_load(true);

	int width, height, channels;
	stbi_uc* pixel_data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);

	channels = 4;

	if (!pixel_data) {
		return {};
	}

	TexturePixelData data{};
	data.pixels = pixel_data;
	data.width = width;
	data.height = height;
	switch (channels) {
	case 4:
		data.format = TextureFormat::R8_G8_B8_A8;
		break;
	default:
		// Loaded the texture, but the format is not supported
		free(pixel_data);
		return {};
	}

	return data;
}

void texture_release_pixel_data(const TexturePixelData& pixel_data) {
	PROFILE_FUNCTION();

	if (pixel_data.pixels == nullptr) {
		return;
	}

	free(pixel_data.pixels);
}

struct FloatColor {
	float r;
	float g;
	float b;
	float a;
};

inline FloatColor float_color_from_bytes(const uint8_t* bytes) {
	constexpr float SCALE_FACTOR = 1.0f / 255.0f;

	uint8_t r = bytes[3];
	uint8_t g = bytes[2];
	uint8_t b = bytes[1];
	uint8_t a = bytes[0];

	return FloatColor { (float)r * SCALE_FACTOR, (float)g * SCALE_FACTOR, (float)b * SCALE_FACTOR, (float)a * SCALE_FACTOR };
}

inline FloatColor operator+(const FloatColor& a, const FloatColor& b) {
	return FloatColor { a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a };
}

inline FloatColor operator*(const FloatColor& a, float scalar) {
	return FloatColor { a.r * scalar, a.g * scalar, a.b * scalar, a.a * scalar };
}

TexturePixelData texture_downscale(const TexturePixelData& source, uint32_t target_size, Arena& allocator) {
	PROFILE_FUNCTION();

	size_t new_pixel_count = (size_t)target_size * (size_t)target_size;

	TexturePixelData downsampled{};
	downsampled.width = target_size;
	downsampled.height = target_size;
	downsampled.format = source.format;

	switch (source.format) {
	case TextureFormat::R8_G8_B8_A8: {
		size_t bytes_per_pixel = 4;
		uint32_t* new_pixels = arena_alloc_array<uint32_t>(allocator, new_pixel_count);
		downsampled.pixels = new_pixels;

		const uint8_t* source_pixels = reinterpret_cast<const uint8_t*>(source.pixels);

		float width_convertion_factor = (float)source.width / target_size;
		float height_convertion_factor = (float)source.height / target_size;

		for (uint32_t y = 0; y < target_size; y++) {
			for (uint32_t x = 0; x < target_size; x++) {
				float source_x = (float)x * width_convertion_factor;
				float source_y = (float)y * height_convertion_factor;

				uint32_t source_x_floor = (uint32_t)std::floor(source_x);
				uint32_t source_y_floor = (uint32_t)std::floor(source_y);

				UVec2 top_left_pos = UVec2 { source_x_floor, source_y_floor };
				UVec2 top_right_pos = UVec2 { min(source_x_floor + 1, source.width - 1), source_y_floor };
				UVec2 bottom_left_pos = UVec2 { source_x_floor, min(source_y_floor + 1, source.height - 1) };
				UVec2 bottom_right_pos = UVec2 {
					min(source_x_floor + 1, source.width - 1),
					min(source_y_floor + 1, source.height - 1)
				};

				uint32_t top_left_pixel_offset = top_left_pos.y * source.width + top_left_pos.x;
				uint32_t top_right_pixel_offset = top_right_pos.y * source.width + top_right_pos.x;
				uint32_t bottom_left_pixel_offset = bottom_left_pos.y * source.width + bottom_left_pos.x;
				uint32_t bottom_right_pixel_offset = bottom_right_pos.y * source.width + bottom_right_pos.x;

				FloatColor top_left = float_color_from_bytes(source_pixels + top_left_pixel_offset * bytes_per_pixel);
				FloatColor top_right = float_color_from_bytes(source_pixels + top_right_pixel_offset * bytes_per_pixel);
				FloatColor bottom_left = float_color_from_bytes(source_pixels + bottom_left_pixel_offset * bytes_per_pixel);
				FloatColor bottom_right = float_color_from_bytes(source_pixels + bottom_right_pixel_offset * bytes_per_pixel);

				float x_blend = source_x - std::floor(source_x);
				float y_blend = source_y - std::floor(source_y);

				float top_left_blend_factor = (1.0f - x_blend) * (1.0f - y_blend);
				float top_right_blend_factor = x_blend * (1.0f - y_blend);
				float bottom_left_blend_factor = (1.0f - x_blend) * y_blend;
				float bottom_right_blend_factor = x_blend * y_blend;

				FloatColor blended_color = top_left * top_left_blend_factor
					+ top_right * top_right_blend_factor
					+ bottom_left * bottom_left_blend_factor
					+ bottom_right * bottom_right_blend_factor;

				uint8_t blended_r = (uint8_t)(clamp(blended_color.r, 0.0f, 1.0f) * 255.0f);
				uint8_t blended_g = (uint8_t)(clamp(blended_color.g, 0.0f, 1.0f) * 255.0f);
				uint8_t blended_b = (uint8_t)(clamp(blended_color.b, 0.0f, 1.0f) * 255.0f);
				uint8_t blended_a = (uint8_t)(clamp(blended_color.a, 0.0f, 1.0f) * 255.0f);

				new_pixels[(target_size - 1 - y) * target_size + x] = ((uint32_t)blended_r << 24)
					| ((uint32_t)blended_g << 16)
					| ((uint32_t)blended_b << 8)
					| ((uint32_t)blended_a);
			}
		}

		break;
	}
	default:
		return {};
	}

	return downsampled;
}

static RangeU32 s_supported_char_ranges[] = {
	RangeU32 { 0x0020, 94 },
	RangeU32 { 0x0400, 256 }
};

static uint32_t s_supported_char_range_count = sizeof(s_supported_char_ranges) / sizeof(RangeU32);

void rasterize_glyphs(Font& font,
		float pixel_height,
		uint8_t* pixels,
		int pw,
		int ph,
		stbtt_bakedchar* chardata) {
	PROFILE_FUNCTION();

	float scale;
	int x, y, bottom_y;

	std::memset(pixels, 0, pw * ph); // background of 0 around pixels
	x = y = 1;
	bottom_y = 1;

	scale = stbtt_ScaleForPixelHeight(&font.info, pixel_height);

	size_t chardata_index = 0;
	for (RangeU32 char_range : font.char_ranges) {
		for (uint32_t i = 0; i < char_range.count; ++i) {
			int advance, lsb, x0,y0,x1,y1,gw,gh;

			uint32_t codepoint = char_range.start + i;

			int g = stbtt_FindGlyphIndex(&font.info, codepoint);
			stbtt_GetGlyphHMetrics(&font.info, g, &advance, &lsb);
			stbtt_GetGlyphBitmapBox(&font.info, g, scale,scale, &x0,&y0,&x1,&y1);
			gw = x1-x0;
			gh = y1-y0;
			if (x + gw + 1 >= pw)
				y = bottom_y, x = 1; // advance to next row
			if (y + gh + 1 >= ph) // check if it fits vertically AFTER potentially moving to next row
				return;

			assert(x+gw < pw);
			assert(y+gh < ph);

			stbtt_MakeGlyphBitmap(&font.info, pixels+x+y*pw, gw,gh,pw, scale,scale, g);
			chardata[chardata_index].x0 = (int16_t) x;
			chardata[chardata_index].y0 = (int16_t) y;
			chardata[chardata_index].x1 = (int16_t) (x + gw);
			chardata[chardata_index].y1 = (int16_t) (y + gh);
			chardata[chardata_index].xadvance = scale * advance;
			chardata[chardata_index].xoff     = (float) x0;
			chardata[chardata_index].yoff     = (float) y0;

			chardata_index += 1;

			x = x + gw + 1;
			if (y+gh+1 > bottom_y)
				bottom_y = y+gh+1;
		}
	}
}

Font create_font(const uint8_t* data, size_t data_size, float font_size, Arena& arena) {
	PROFILE_FUNCTION();

	Font font{};
	font.char_ranges = Span<const RangeU32>(s_supported_char_ranges, s_supported_char_range_count);

	for (RangeU32 range : font.char_ranges) {
		font.glyph_count += range.count;
	}

	font.glyphs = arena_alloc_array<stbtt_bakedchar>(arena, font.glyph_count);
	font.size = font_size;

	if (!stbtt_InitFont(&font.info, data, 0)) {
		return Font{};
	}

	stbtt_GetFontVMetrics(&font.info, &font.ascent, &font.descent, &font.line_gap);

	int32_t texture_size = 512;
	size_t pixel_count = static_cast<size_t>(texture_size) * static_cast<size_t>(texture_size);

	{
		ArenaSavePoint temp = arena_begin_temp(arena);
		uint8_t* bitmap = arena_alloc_array<uint8_t>(arena, pixel_count);

		rasterize_glyphs(font,
				font.size,
				bitmap,
				texture_size,
				texture_size,
				font.glyphs);

		uint32_t* rgba_bitmap = arena_alloc_array<uint32_t>(arena, pixel_count);
		
		for (size_t i = 0; i < pixel_count; i++) {
			uint8_t r = bitmap[i];

			rgba_bitmap[i] = 0xffffff | (r << 24);
		}

		font.atlas = create_texture(TextureFormat::R8_G8_B8_A8, texture_size, texture_size, rgba_bitmap);

		arena_end_temp(temp);
	}

	return font;
}

Font load_font_from_file(const std::filesystem::path& path, float font_size, Arena& arena) {
	PROFILE_FUNCTION();
	std::ifstream stream(path, std::ios::binary);

	if (!stream.is_open()) {
		return Font{};
	}
	
	// FIXME: get the actual file size
	size_t file_size = 1 << 20;

	uint8_t* font_data = reinterpret_cast<uint8_t*>(arena_alloc_aligned(arena, file_size, 16));
	stream.read(reinterpret_cast<char*>(font_data), file_size);

	return create_font(font_data, file_size, font_size, arena);
}

void delete_font(const Font& font) {
	PROFILE_FUNCTION();
	delete_texture(font.atlas);
}

uint32_t font_get_glyph_index(const Font& font, uint32_t codepoint) {
	uint32_t offset = 0;
	for (RangeU32 range : font.char_ranges) {
		if (codepoint >= range.start && codepoint < range.start + range.count) {
			return offset + codepoint - range.start;
		} else {
			offset += range.count;
		}
	}

	return UINT32_MAX;
}

float font_get_height(const Font& font) {
	float scale = stbtt_ScaleForPixelHeight(&font.info, font.size);
	return (float)(font.ascent - font.descent) * scale;
}

void begin_frame() {
}

void end_frame() {
	PROFILE_FUNCTION();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	UVec2 window_frame_buffer_size = window_get_framebuffer_size(s_state.window);
	Vec2 viewport_size = static_cast<Vec2>(window_frame_buffer_size);

	glViewport(0.0f, 0.0f, viewport_size.x, viewport_size.y);

	{
		int32_t location = glGetUniformLocation(s_state.shader_id, "u_ProjectionParams");
		glUniform2f(location, 1.0f / viewport_size.x, 1.0f / viewport_size.y);
	}

	{
		int32_t location = glGetUniformLocation(s_state.shader_id, "u_Texture");
		glUniform1i(location, 0);
	}

	glBindBuffer(GL_ARRAY_BUFFER, s_state.vertex_buffer_id);
	glBufferData(GL_ARRAY_BUFFER,
			sizeof(QuadVertex) * s_state.vertices.size(),
			s_state.vertices.data(),
			GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, s_state.index_buffer_id);
	glBufferData(GL_ARRAY_BUFFER,
			sizeof(uint32_t) * s_state.indices.size(),
			s_state.indices.data(),
			GL_DYNAMIC_DRAW);

	glUseProgram(s_state.shader_id);
	glBindVertexArray(s_state.vertex_array_id);
	glActiveTexture(GL_TEXTURE0);

	for (const DrawCommand& command : s_state.commands) {
		if (command.index_count == 0) {
			continue;
		}

		glBindTexture(GL_TEXTURE_2D, command.texture_id);
		glDrawElements(GL_TRIANGLES,
				command.index_count,
				GL_UNSIGNED_INT,
				reinterpret_cast<const void*>(command.first_index * sizeof(uint32_t)));
	}

	s_state.vertices.clear();
	s_state.indices.clear();
	s_state.commands.clear();
}

void draw_line(Vec2 a, Vec2 b, Color color) {
	draw_rect(Rect { .min = a - Vec2 { 0.5f, 0.5f }, .max = b + Vec2 { 0.5f, 0.5f} }, color);
}

void draw_rect(const Rect& rect, Color color) {
	push_texture(s_state.white_texture);

	uint32_t vertex_offset = static_cast<uint32_t>(s_state.vertices.size());
	uint32_t color32 = color_to_uint32(color);

	s_state.vertices.push_back(QuadVertex {
		.position = rect.min,
		.uv = Vec2 { 0.0f, 0.0f },
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = Vec2 { rect.max.x, rect.min.y },
		.uv = Vec2 { 0.0f, 0.0f },
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = rect.max,
		.uv = Vec2 { 0.0f, 0.0f },
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = Vec2 { rect.min.x, rect.max.y },
		.uv = Vec2 { 0.0f, 0.0f },
		.color = color32
	});
	
	s_state.indices.push_back(vertex_offset + 0);
	s_state.indices.push_back(vertex_offset + 1);
	s_state.indices.push_back(vertex_offset + 2);
	s_state.indices.push_back(vertex_offset + 0);
	s_state.indices.push_back(vertex_offset + 2);
	s_state.indices.push_back(vertex_offset + 3);

	DrawCommand& command = s_state.commands.back();
	command.index_count += 6;
}

void draw_rect(const Rect& rect, Color color, const Texture& texture, Rect uv_rect) {
	push_texture(texture);

	uint32_t vertex_offset = static_cast<uint32_t>(s_state.vertices.size());
	uint32_t color32 = color_to_uint32(color);

	uv_rect.min.y = 1.0f - uv_rect.min.y;
	uv_rect.max.y = 1.0f - uv_rect.max.y;

	s_state.vertices.push_back(QuadVertex {
		.position = rect.min,
		.uv = uv_rect.min,
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = Vec2 { rect.max.x, rect.min.y },
		.uv = Vec2 { uv_rect.max.x, uv_rect.min.y },
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = rect.max,
		.uv = uv_rect.max,
		.color = color32
	});

	s_state.vertices.push_back(QuadVertex {
		.position = Vec2 { rect.min.x, rect.max.y },
		.uv = Vec2 { uv_rect.min.x, uv_rect.max.y },
		.color = color32
	});
	
	s_state.indices.push_back(vertex_offset + 0);
	s_state.indices.push_back(vertex_offset + 1);
	s_state.indices.push_back(vertex_offset + 2);
	s_state.indices.push_back(vertex_offset + 0);
	s_state.indices.push_back(vertex_offset + 2);
	s_state.indices.push_back(vertex_offset + 3);

	DrawCommand& command = s_state.commands.back();
	command.index_count += 6;
}

void draw_rounded_rect(const Rect& rect, Color color, float corner_radius) {
	if (color.a == 0) {
		return;
	}

	if (corner_radius == 0.0f) {
		draw_rect(rect, color);
		return;
	}

	push_texture(s_state.white_texture);

	uint32_t vertex_offset = static_cast<uint32_t>(s_state.vertices.size());
	uint32_t color32 = color_to_uint32(color);

	Vec2 radius_vector = Vec2 { corner_radius, corner_radius };

	Vec2 top_left_origin = rect.min + radius_vector;
	Vec2 top_right_origin = Vec2 { rect.max.x, rect.min.y } + Vec2 { -corner_radius, corner_radius };
	Vec2 bottom_left_origin = Vec2 { rect.min.x, rect.max.y } + Vec2 { corner_radius, -corner_radius };
	Vec2 bottom_right_origin = rect.max - radius_vector;

	size_t vertex_count = ROUNDED_CORNER_VERTEX_COUNT * 4;

	// top left
	for (size_t i = 0; i < ROUNDED_CORNER_VERTEX_COUNT; i++) {
		Vec2 offset = s_state.rounded_corner_vertices[i] * corner_radius;
		offset.x = -offset.x;
		offset.y = -offset.y;
		s_state.vertices.push_back(QuadVertex { top_left_origin + offset, Vec2{}, color32 });
	}

	// top right
	for (size_t i = 0; i < ROUNDED_CORNER_VERTEX_COUNT; i++) {
		Vec2 offset = s_state.rounded_corner_vertices[ROUNDED_CORNER_VERTEX_COUNT - i - 1] * corner_radius;
		offset.y = -offset.y;
		s_state.vertices.push_back(QuadVertex { top_right_origin + offset, Vec2{}, color32 });
	}

	// bottom right
	for (size_t i = 0; i < ROUNDED_CORNER_VERTEX_COUNT; i++) {
		Vec2 offset = s_state.rounded_corner_vertices[i] * corner_radius;
		s_state.vertices.push_back(QuadVertex { bottom_right_origin + offset, Vec2{}, color32 });
	}

	// bottom left
	for (size_t i = 0; i < ROUNDED_CORNER_VERTEX_COUNT; i++) {
		Vec2 offset = s_state.rounded_corner_vertices[ROUNDED_CORNER_VERTEX_COUNT - i - 1] * corner_radius;
		offset.x = -offset.x;
		s_state.vertices.push_back(QuadVertex { bottom_left_origin + offset, Vec2{}, color32 });
	}

	for (size_t i = 0; i < vertex_count - 2; i++) {
		s_state.indices.push_back(vertex_offset);
		s_state.indices.push_back(vertex_offset + i + 1);
		s_state.indices.push_back(vertex_offset + i + 2);
	}

	DrawCommand& command = s_state.commands.back();
	command.index_count += (vertex_count - 2) * 3;
}

void draw_rect_lines(const Rect& rect, Color color) {
	if (color.a == 0) {
		return;
	}

	Vec2 top_right = Vec2 { rect.max.x, rect.min.y };
	Vec2 bottom_left = Vec2 { rect.min.x, rect.max.y };

	draw_line(rect.min, top_right, color);
	draw_line(top_right, rect.max, color);
	draw_line(bottom_left, rect.max, color);
	draw_line(rect.min, bottom_left, color);
}

void draw_text(std::wstring_view text, Vec2 position, const Font& font, Color color, float max_width) {
	PROFILE_FUNCTION();

	if (color.a == 0) {
		return;
	}

	push_texture(font.atlas);
	DrawCommand& command = s_state.commands.back();

	float scale = stbtt_ScaleForPixelHeight(&font.info, font.size);

	uint32_t color_value = color_to_uint32(color);
	Vec2 char_position = position;
	char_position.y += (float)font.ascent * scale;

	for (size_t i = 0; i < text.size(); i++) {
		uint32_t c = text[i];
		uint32_t glyph_index = font_get_glyph_index(font, c);
		if (glyph_index == UINT32_MAX) {
			continue;
		}

		stbtt_aligned_quad quad{};
		stbtt_GetBakedQuad(font.glyphs,
				font.atlas.width,
				font.atlas.height,
				glyph_index,
				&char_position.x,
				&char_position.y,
				&quad,
				1);

		float text_width = char_position.x - position.x;
		if (text_width > max_width) {
			break;
		}

		int32_t kerning_advance = 0;
		if (i + 1 < text.size()) {
			kerning_advance = stbtt_GetCodepointKernAdvance(&font.info, text[i], text[i + 1]);
			char_position.x += (float)(kerning_advance) * scale;
		}

		Vec2 uv_min = Vec2(quad.s0, quad.t0);
		Vec2 uv_max = Vec2(quad.s1, quad.t1);

		Vec2 min = Vec2(quad.x0, quad.y0);
		Vec2 max = Vec2(quad.x1, quad.y1);

		uint32_t vertex_offset = static_cast<uint32_t>(s_state.vertices.size());

		s_state.vertices.push_back(QuadVertex{ Vec2(min.x, min.y), uv_min, color_value });
		s_state.vertices.push_back(QuadVertex{ Vec2(max.x, min.y), Vec2(uv_max.x, uv_min.y), color_value });
		s_state.vertices.push_back(QuadVertex{ Vec2(max.x, max.y), uv_max, color_value });
		s_state.vertices.push_back(QuadVertex{ Vec2(min.x, max.y), Vec2(uv_min.x, uv_max.y), color_value });

		s_state.indices.push_back(vertex_offset + 0);
		s_state.indices.push_back(vertex_offset + 1);
		s_state.indices.push_back(vertex_offset + 2);
		s_state.indices.push_back(vertex_offset + 0);
		s_state.indices.push_back(vertex_offset + 2);
		s_state.indices.push_back(vertex_offset + 3);

		command.index_count += 6;
	}
}
