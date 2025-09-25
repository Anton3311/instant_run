#pragma once

#include "core.h"

#include <string_view>

struct Arena;

struct XMLAttribute {
	std::string_view name;
	std::string_view value;
};

struct XMLTag {
	std::string_view name;
	std::string_view value;

	XMLTag* first_child;
	XMLTag* next_sibling;

	Span<XMLAttribute> attributes;
};

struct XMLDocument {
	XMLTag* metadata;
	XMLTag* root;
};

inline XMLTag* xml_tag_find_child(XMLTag* tag, std::string_view child_tag) {
	XMLTag* child = tag->first_child;
	while (child) {
		if (child->name == child_tag) {
			return child;
		}

		child = child->next_sibling;
	}

	return nullptr;
}

inline XMLAttribute* xml_tag_find_attrib(XMLTag* tag, std::string_view attrib_name) {
	for (size_t i = 0; i < tag->attributes.count; i++) {
		if (tag->attributes[i].name == attrib_name) {
			return &tag->attributes[i];
		}
	}

	return nullptr;
}

XMLDocument xml_parse(std::string_view xml_string, Arena& arena);
