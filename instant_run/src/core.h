#pragma once

#include <stdint.h>
#include <string_view>

#ifdef ENABLE_PROFILING 
#include <tracy/Tracy.hpp>

#define PROFILE_SCOPE(name) ZoneScopedN(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCSIG__)

#define PROFILE_BEGIN_FRAME(name) FrameMarkStart(name)
#define PROFILE_END_FRAME(name) FrameMarkEnd(name)

#define PROFILE_NAME_THREAD(name) tracy::SetThreadName(name)
#else
#define PROFILE_SCOPE(name)
#define PROFILE_FUNCTION()

#define PROFILE_BEGIN_FRAME(name)
#define PROFILE_END_FRAME(name)

#define PROFILE_NAME_THREAD(name)
#endif

#if defined(BUILD_DEBUG) || defined(BUILD_PROFILING)
#define BUILD_DEV
#endif

#define HAS_FLAG(flag_set, flag) (((flag_set) & (flag)) == (flag))
#define HAS_ANY_FLAG(flag_set, flag) (((flag_set) & (flag)) != 0)

#define ENUM_TYPE(enum_name) std::underlying_type_t<enum_name>
#define IMPL_ENUM_FLAGS(enum_name) \
	constexpr enum_name operator&(enum_name a, enum_name b) \
		{ return (enum_name)((ENUM_TYPE(enum_name))a & (ENUM_TYPE(enum_name))b); } \
	constexpr enum_name operator|(enum_name a, enum_name b) \
		{ return (enum_name)((ENUM_TYPE(enum_name))a | (ENUM_TYPE(enum_name))b); } \
	constexpr enum_name& operator|=(enum_name& a, enum_name b) \
		{ a = (enum_name)((ENUM_TYPE(enum_name))a | (ENUM_TYPE(enum_name))b); return a; } \
	constexpr bool operator==(enum_name a, int b) { return (int)a == b; } \
	constexpr bool operator!=(enum_name a, int b) { return (int)a != b; } \
	constexpr enum_name operator~(enum_name a) \
		{ return (enum_name)(~(ENUM_TYPE(enum_name))a); }

//
// Span
//

template<typename T>
struct Span {
	Span() = default;
	Span(T* values, size_t count)
		: values(values), count(count) {}

	inline T& operator[](size_t index) {
		if (index >= count) {
			__debugbreak(); // TODO: assert
		}

		return values[index];
	}

	inline const T& operator[](size_t index) const {
		if (index >= count) {
			__debugbreak(); // TODO: assert
		}

		return values[index];
	}

	inline T* begin() { return values; }
	inline T* end() { return values + count; }

	inline const T* begin() const { return values; }
	inline const T* end() const { return values + count; }

	T* values;
	size_t count;
};

//
// Range
//

struct RangeU32 {
	uint32_t start;
	uint32_t count;
};

//
// Arena
//

inline size_t align(size_t value, size_t alignment) {
	return (value + alignment - 1) / alignment * alignment;
}

struct Arena {
	size_t capacity;
	size_t commited;
	size_t allocated;
	uint8_t* base;
};
void query_system_memory_spec();
void* arena_alloc_aligned(Arena& arena, size_t size, size_t alignment);

inline void arena_align_to_cache_line(Arena& arena) {
	arena.allocated = align(arena.allocated, 64);
}

inline void arena_reset(Arena& arena) {
	arena.allocated = 0;
}

template<typename T>
inline T* arena_alloc(Arena& arena) {
	return reinterpret_cast<T*>(arena_alloc_aligned(arena, sizeof(T), alignof(T)));
}

template<typename T>
inline T* arena_alloc_array(Arena& arena, size_t count) {
	return reinterpret_cast<T*>(arena_alloc_aligned(arena, sizeof(T) * count, alignof(T)));
}

inline std::wstring_view arena_push_string(Arena& arena, std::wstring_view string) {
	wchar_t* copy = arena_alloc_array<wchar_t>(arena, string.length());
	std::memcpy(copy, string.data(), sizeof(wchar_t) * string.length());

	return std::wstring_view(copy, string.length());
}

void arena_release(Arena& arena);

struct ArenaSavePoint {
	Arena* arena;
	size_t allocated_state;
};

inline ArenaSavePoint arena_begin_temp(Arena& arena) {
	return ArenaSavePoint { .arena = &arena, .allocated_state = arena.allocated };
}

inline void arena_end_temp(ArenaSavePoint save_point) {
	save_point.arena->allocated = save_point.allocated_state;
}

constexpr size_t kb_to_bytes(size_t kb) { return kb * 1024; }
constexpr size_t mb_to_bytes(size_t mb) { return kb_to_bytes(mb * 1024); }

//
// String
//

inline size_t wstr_length(const wchar_t* string) {
	PROFILE_FUNCTION();
	const wchar_t* iterator = string;
	while (*iterator != 0) {
		++iterator;
	}
	
	return iterator - string;
}

// create a copy of a string with null-terminator
inline std::wstring_view wstr_duplicate(const wchar_t* string, Arena& allocator) {
	PROFILE_FUNCTION();

	size_t length = wstr_length(string);
	wchar_t* new_string = arena_alloc_array<wchar_t>(allocator, length + 1);

	memcpy(new_string, string, length * sizeof(*string));

	new_string[length] = 0;
	return std::wstring_view(new_string, length);
}

// create a copy of a string without null-terminator
inline std::wstring_view wstr_duplicate(std::wstring_view string, Arena& allocator) {
	PROFILE_FUNCTION();

	size_t length = string.length();
	wchar_t* new_string = arena_alloc_array<wchar_t>(allocator, length);
	memcpy(new_string, string.data(), length * sizeof(string[0]));

	return std::wstring_view(new_string, length);
}

// create a copy of a string with null-terminator
inline std::string_view str_duplicate(const char* string, Arena& allocator) {
	PROFILE_FUNCTION();

	size_t length = strlen(string);
	char* new_string = arena_alloc_array<char>(allocator, length + 1);

	memcpy(new_string, string, length * sizeof(string[0]));

	new_string[length] = 0;
	return std::string_view(new_string, length);
}

// create a copy of a string without null-terminator
inline std::string_view str_duplicate(std::string_view string, Arena& allocator) {
	PROFILE_FUNCTION();

	size_t length = string.length();
	char* new_string = arena_alloc_array<char>(allocator, length);

	memcpy(new_string, string.data(), length * sizeof(string[0]));
	return std::string_view(new_string, length);
}

//
// String Builder
//

template<typename T>
struct StringBuilder {
	Arena* arena;
	const T* string;
	size_t length;
};

template<typename T>
inline void str_builder_append(StringBuilder<T>& builder, std::basic_string_view<T> string) {
	T* buffer = arena_alloc_array<T>(*builder.arena, string.length());
	memcpy(buffer, string.data(), sizeof(T) * string.length());

	builder.length += string.length();

	if (!builder.string) {
		builder.string = buffer;
	}
}

template<typename T>
inline void str_builder_append(StringBuilder<T>& builder, T c) {
	T* buffer = arena_alloc_array<T>(*builder.arena, 1);
	*buffer = c;

	builder.length += 1;

	if (!builder.string) {
		builder.string = buffer;
	}
}

template<typename T>
inline std::basic_string_view<T> str_builder_to_str(const StringBuilder<T>& builder) {
	return std::basic_string_view<T>(builder.string, builder.length);
}

template<typename T>
inline const T* str_builder_to_cstr(StringBuilder<T>& builder) {
	str_builder_append<T>(builder, (T)0);
	return builder.string;
}

