#include "renderer.h"

#include <glad/glad.h>

#include <stdlib.h>
#include <vector>
#include <fstream>

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
}

void shutdown_renderer() {
	delete_texture(s_state.white_texture);

	s_state = {};
}

Texture create_texture(TextureFormat format, uint32_t width, uint32_t height, const void* data) {
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
	glTextureSubImage2D(texture.internal_id, 0, 0, 0, width, height, texture_format, GL_UNSIGNED_BYTE, data);

	glTextureParameteri(texture.internal_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(texture.internal_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	return texture;
}

void delete_texture(const Texture& texture) {
	glDeleteTextures(1, &texture.internal_id);
}

Font create_font(const uint8_t* data, size_t data_size, float font_size) {
	Font font{};
	font.char_range_start = 32;
	font.glyph_count = 96;
	font.glyphs = new stbtt_bakedchar[font.glyph_count];
	font.size = font_size;

	int32_t texture_size = 512;
	size_t pixel_count = static_cast<size_t>(texture_size) * static_cast<size_t>(texture_size);

	uint8_t* bitmap = new uint8_t[pixel_count];

	stbtt_BakeFontBitmap(data,
			0,
			font.size,
			bitmap,
			texture_size,
			texture_size,
			static_cast<int32_t>(font.char_range_start),
			static_cast<int32_t>(font.glyph_count),
			font.glyphs);

	uint32_t* rgba_bitmap = new uint32_t[pixel_count];
	
	for (size_t i = 0; i < pixel_count; i++) {
		uint8_t r = bitmap[i];

		rgba_bitmap[i] = 0xffffff | (r << 24);
	}

	font.atlas = create_texture(TextureFormat::R8_G8_B8_A8, texture_size, texture_size, rgba_bitmap);

	delete[] rgba_bitmap;
	delete[] bitmap;

	return font;
}

Font load_font_from_file(const std::filesystem::path& path, float font_size) {
	std::ifstream stream(path, std::ios::binary);

	if (!stream.is_open()) {
		return Font{};
	}
	
	// FIXME: get the actual file size
	size_t file_size = 1 << 20;

	uint8_t* font_data = new uint8_t[file_size];
	stream.read(reinterpret_cast<char*>(font_data), file_size);

	Font font = create_font(font_data, file_size, font_size);

	delete[] font_data;

	return font;
}

void delete_font(const Font& font) {
	delete_texture(font.atlas);

	delete[] font.glyphs;
}

void begin_frame() {
}

void end_frame() {
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	UVec2 window_frame_buffer_size = get_window_framebuffer_size(s_state.window);
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

	glNamedBufferData(s_state.vertex_buffer_id,
			sizeof(QuadVertex) * s_state.vertices.size(),
			s_state.vertices.data(),
			GL_DYNAMIC_DRAW);

	glNamedBufferData(s_state.index_buffer_id,
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

void draw_rect_lines(const Rect& rect, Color color) {
	Vec2 top_right = Vec2 { rect.max.x, rect.min.y };
	Vec2 bottom_left = Vec2 { rect.min.x, rect.max.y };

	draw_line(rect.min, top_right, color);
	draw_line(top_right, rect.max, color);
	draw_line(bottom_left, rect.max, color);
	draw_line(rect.min, bottom_left, color);
}

void draw_text(std::wstring_view text, Vec2 position, const Font& font, Color color) {
	push_texture(font.atlas);

	uint32_t color_value = color_to_uint32(color);
	Vec2 char_position = position;
	char_position.y += font.size;

	for (size_t i = 0; i < text.size(); i++) {
		uint32_t c = text[i];

		if (c < font.char_range_start || c >= font.char_range_start + font.glyph_count) {
			continue;
		}

		stbtt_aligned_quad quad{};
		stbtt_GetBakedQuad(font.glyphs,
				font.atlas.width,
				font.atlas.height,
				c - font.char_range_start,
				&char_position.x,
				&char_position.y,
				&quad,
				1);

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

		DrawCommand& command = s_state.commands.back();
		command.index_count += 6;
	}
}
