#include "xml.h"

#include "core.h"

#include <cstring>
#include <cctype>
#include <assert.h>

struct ParserState {
	Arena* arena;
	std::string_view string;
	size_t read_position;
};

inline static bool is_eof(ParserState& state) {
	return state.read_position == state.string.length();
}

inline static void skip_whitespace(ParserState& state) {
	while (!is_eof(state)) {
		if (std::isspace((uint8_t)(state.string[state.read_position]))) {
			state.read_position += 1;
		} else {
			break;
		}
	}
}

inline static std::string_view parse_ident(ParserState& state) {
	PROFILE_FUNCTION();

	skip_whitespace(state);

	size_t ident_start = state.read_position;
	while (!is_eof(state)) {
		char current_char = state.string[state.read_position];

		if (std::isalnum(current_char) || current_char == ':') {
			state.read_position += 1;
		} else {
			break;
		}
	}

	return state.string.substr(ident_start, state.read_position - ident_start);
}

inline static bool consume_char(ParserState& state, char c) {
	if (state.string[state.read_position] == c) {
		state.read_position += 1;
		return true;
	}

	return false;
}

inline static bool parse_string(ParserState& state, std::string_view* out_string) {
	assert(state.string[state.read_position] == '"');

	size_t string_start = state.read_position;

	state.read_position += 1;
	while (!is_eof(state)) {
		if (state.string[state.read_position] == '"') {
			state.read_position += 1; // consume the quote

			std::string_view string = state.string.substr(string_start + 1,
					state.read_position - string_start - 2);

			*out_string = string;
			return true;
		} else {
			state.read_position += 1;
		}
	}
	
	return false;
}

inline static XMLTag* parse_tag(ParserState& state) {
	PROFILE_FUNCTION();

	skip_whitespace(state);

	if (!consume_char(state, '<')) {
		return nullptr;
	}

	bool has_quastion_mark = consume_char(state, '?');

	std::string_view tag_name = parse_ident(state);

	XMLTag* tag = arena_alloc<XMLTag>(*state.arena);
	tag->name = tag_name;
	tag->value = std::string_view();
	tag->first_child = nullptr;
	tag->next_sibling = nullptr;
	tag->attributes = {};

	while (true) {
		skip_whitespace(state);

		if (is_eof(state)) {
			return nullptr;
		}

		char current_char = state.string[state.read_position];
		if (current_char == '/') {
			state.read_position += 1;

			if (!consume_char(state, '>')) {
				return nullptr;
			}

			return tag;
		} else if (current_char == '?') {
			if (!has_quastion_mark) {
				return nullptr;
			}

			state.read_position += 1;

			if (!consume_char(state, '>')) {
				return nullptr;
			}

			return tag;
		} else if (current_char == '>') {
			state.read_position += 1;

			XMLTag* last_child_tag = nullptr;

			while (true) {
				skip_whitespace(state);

				if (is_eof(state)) {
					return nullptr;
				}

				char current_char = state.string[state.read_position];
				if (current_char == '<'
						&& state.read_position + 1 < state.string.length()
						&& state.string[state.read_position + 1] == '/') {
					state.read_position += 2;

					std::string_view closing_tag_name = parse_ident(state);

					if (!consume_char(state, '>')) {
						return nullptr;
					}

					if (tag_name != closing_tag_name) {
						return nullptr;
					}

					return tag;
				} else if (current_char == '<') {
					// we have a child tag
					XMLTag* child_tag = parse_tag(state);

					if (last_child_tag == nullptr) {
						tag->first_child = child_tag;
						last_child_tag = child_tag;
					} else {
						last_child_tag->next_sibling = child_tag;
						last_child_tag = child_tag;
					}
				} else {
					// we have a value

					size_t value_start = state.read_position;
					while (!is_eof(state)) {
						if (state.string[state.read_position] != '<') {
							state.read_position += 1;
						} else {
							break;
						}
					}

					tag->value = state.string.substr(value_start, state.read_position - value_start);
				}
			}

			return tag;
		} else {
			// we have attributes

			if (tag->attributes.values == nullptr) {
				tag->attributes.values = arena_alloc_array<XMLAttribute>(*state.arena, 0);
			}

			std::string_view attr_name = parse_ident(state);

			skip_whitespace(state);

			if (!consume_char(state, '=')) {
				return nullptr;
			}

			skip_whitespace(state);

			std::string_view attr_value;
			if (!parse_string(state, &attr_value)) {
				return nullptr;
			}

			XMLAttribute* attrib = arena_alloc<XMLAttribute>(*state.arena);
			attrib->name = attr_name;
			attrib->value = attr_value;

			tag->attributes.count += 1;
		}
	}

	return nullptr;
}

XMLDocument xml_parse(std::string_view xml_string, Arena& arena) {
	PROFILE_FUNCTION();

	ParserState state = {};
	state.arena = &arena;
	state.string = xml_string;
	state.read_position = 0;

	XMLDocument document = {};
	document.metadata = parse_tag(state);
	document.root = parse_tag(state);

	return document;
}
