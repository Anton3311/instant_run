#pragma once

#include <stdint.h>

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
	return static_cast<uint32_t>(color.r)
		| (static_cast<uint32_t>(color.g) << 8)
		| (static_cast<uint32_t>(color.b) << 16)
		| (static_cast<uint32_t>(color.a) << 24);
}

struct Vec2;

struct UVec2 {
	inline operator Vec2();

	uint32_t x;
	uint32_t y;
};

struct Vec2 {
	float x;
	float y;
};

//
// UVec2
//

inline UVec2::operator Vec2() {
	return Vec2 { static_cast<float>(x), static_cast<float>(y) };
}

inline Vec2 operator+(Vec2 a, Vec2 b) {
	return Vec2 { a.x + b.x, a.y + b.y };
}

inline Vec2 operator-(Vec2 a, Vec2 b) {
	return Vec2 { a.x - b.x, a.y - b.y };
}

inline Vec2 operator*(Vec2 a, float scalar) {
	return Vec2 { a.x * scalar, a.y * scalar };
}

struct Rect {
	Vec2 min;
	Vec2 max;
};
