#pragma once

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static constexpr Color BLACK = Color { 0, 0, 0, 255 };
static constexpr Color WHITE = Color { 255, 255, 255, 255 };
static constexpr Color TRANSPARENT = Color { 0, 0, 0, 0 };

inline uint32_t color_to_uint32(Color color) {
	return (static_cast<uint32_t>(color.r) << 24)
		| (static_cast<uint32_t>(color.g) << 16)
		| (static_cast<uint32_t>(color.b) << 8)
		| (static_cast<uint32_t>(color.a));
}

inline Color color_from_hex(uint32_t hex) {
	uint8_t r = static_cast<uint8_t>(hex >> 24);
	uint8_t g = static_cast<uint8_t>(hex >> 16);
	uint8_t b = static_cast<uint8_t>(hex >> 8);
	uint8_t a = static_cast<uint8_t>(hex);

	return Color { r, g, b, a };
}

