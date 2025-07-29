#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#include "platform.h"
#include "renderer.h"
#include "ui.h"

static constexpr UVec2 INVALID_ICON_POSITION = UVec2 { UINT32_MAX, UINT32_MAX };

struct ApplicationIconsStorage {
	Texture texture;
	uint32_t icon_size;
	uint32_t write_offset;
	uint32_t grid_size;
};

void initialize_app_icon_storage(ApplicationIconsStorage& storage, uint32_t icon_size, uint32_t grid_size) {
	PROFILE_FUNCTION();
	uint32_t texture_size = icon_size * grid_size;

	storage.texture = create_texture(TextureFormat::R8_G8_B8_A8, texture_size, texture_size, nullptr);
	storage.icon_size = icon_size;
	storage.write_offset = 0;
	storage.grid_size = grid_size;
}

UVec2 store_app_icon(ApplicationIconsStorage& storage, const void* pixels) {
	PROFILE_FUNCTION();
	uint32_t capacity = storage.grid_size * storage.grid_size;
	if (storage.write_offset == capacity) {
		return INVALID_ICON_POSITION;
	}

	UVec2 icon_position = UVec2 {
		storage.write_offset % storage.grid_size,
		storage.write_offset / storage.grid_size
	};

	UVec2 offset = UVec2 { icon_position.x * storage.icon_size, icon_position.y * storage.icon_size };

	upload_texture_region(storage.texture, offset, UVec2 { storage.icon_size, storage.icon_size }, pixels);
	storage.write_offset++;
	
	return icon_position;
}

Rect get_icon_rect(const ApplicationIconsStorage& storage, UVec2 icon_position) {
	float icon_size = (float)storage.icon_size;
	float icon_size_uv = 1.0f / (float)storage.grid_size;

	Vec2 offset = Vec2 { (float)icon_position.x, (float)icon_position.y } * icon_size_uv;

	Rect uv_rect = Rect { offset, offset + Vec2 { icon_size_uv, icon_size_uv } };
	uv_rect.min.y = 1.0f - uv_rect.min.y;
	uv_rect.max.y = 1.0f - uv_rect.max.y;

	return uv_rect;
}

struct Entry {
	std::wstring name;
	std::filesystem::path path;
	UVec2 icon;
};

struct ResultEntry {
	uint32_t entry_index;
	uint32_t score;

	RangeU32 highlights;
};

struct ResultViewState {
	uint32_t selected_index;
	uint32_t scroll_offset;
	uint32_t visible_item_count;
	
	std::vector<ResultEntry> matches;
	std::vector<RangeU32> highlights;
};

bool starts_with(std::wstring_view string, std::wstring_view start_pattern) {
	PROFILE_FUNCTION();
	if (string.length() < start_pattern.length()) {
		return false;
	}

	size_t min_length = std::min(string.length(), start_pattern.length());
	for (size_t i = 0; i < min_length; i++) {
		if (string[i] != start_pattern[i]) {
			return false;
		}
	}

	return true;
}

uint32_t dp[100][100];
uint32_t compute_edit_distance(const std::wstring_view& string, const std::wstring_view& pattern) {
	PROFILE_FUNCTION();
	std::memset(dp, 0, sizeof(dp));

	for (size_t i = 0; i <= string.size(); i++)
		dp[i][0] = i;

	for (size_t i = 0; i <= pattern.size(); i++)
		dp[0][i] = i;

	for (size_t j = 1; j <= pattern.size(); j++) {
		for (size_t i = 1; i <= string.size(); i++) {
			uint32_t substitution_cost;

			if (string[i - 1] == pattern[j - 1]) {
				substitution_cost = 0;
			} else {
				substitution_cost = 1;
			}

			uint32_t deletion = dp[i - 1][j] + 1 + 2;
			uint32_t insertion = dp[i][j - 1] + 1;
			uint32_t substitution = dp[i - 1][j - 1] + substitution_cost;

			dp[i][j] = std::min(substitution, std::min(deletion, insertion));
		}
	}

	return dp[string.size()][pattern.size()];
}

inline wchar_t to_lower_case(wchar_t c) {
	if (c >= L'A' && c <= 'Z') {
		return c - L'A' + 'a';
	}
	return c;
}

uint32_t compute_logest_common_subsequence(
		const std::wstring_view& string,
		const std::wstring_view& pattern,
		std::vector<RangeU32>& sequence_ranges,
		RangeU32& highlight_range) {
	PROFILE_FUNCTION();
	if (string.length() < pattern.length()) {
		return 0;
	}

	uint32_t result = 0;

	highlight_range.start = static_cast<uint32_t>(sequence_ranges.size());

	for (size_t i = 0; i < (string.length() - pattern.length() + 1); i++) {
		uint32_t sequence_length = 0;

		for (size_t j = 0; j < pattern.size(); j++) {
			if (to_lower_case(string[i + j]) == to_lower_case(pattern[j])) {
				sequence_length += 1;
			} else {
				break;
			}
		}

		if (sequence_length > 0) {
			sequence_ranges.push_back(RangeU32 { static_cast<uint32_t>(i), sequence_length });

			highlight_range.count += 1;
		}

		result = std::max(result, sequence_length);
	}

	return result;
}

void update_search_result(std::wstring_view search_pattern,
		const std::vector<Entry>& entries,
		std::vector<ResultEntry>& result,
		std::vector<RangeU32>& sequence_ranges) {
	PROFILE_FUNCTION();

	result.clear();
	sequence_ranges.clear();

	for (size_t i = 0; i < entries.size(); i++) {
		const Entry& entry = entries[i];

		RangeU32 highlight_range{};

		uint32_t score = compute_logest_common_subsequence(
				entry.name,
				search_pattern,
				sequence_ranges,
				highlight_range);

		result.push_back({ (uint32_t)i, score, highlight_range });
	}

	std::sort(result.begin(), result.end(), [&](auto a, auto b) -> bool {
		return a.score > b.score;
	});
}

enum class EntryAction {
	None,
	Launch,
};

EntryAction draw_result_entry(const ResultEntry& match,
		const Entry& entry,
		const ResultViewState& state,
		bool is_selected,
		Color highlight_color,
		const ApplicationIconsStorage& app_icon_storage) {
	PROFILE_FUNCTION();
	const ui::Theme& theme = ui::get_theme();

	Vec2 available_region = ui::get_available_layout_region_size();
	Vec2 widget_size = Vec2 { available_region.x, ui::get_default_widget_height() };

	ui::add_item(widget_size);

	Rect item_bounds = ui::get_item_bounds();

	bool hovered = ui::is_item_hovered();
	bool pressed = hovered && ui::is_mouse_button_pressed(MouseButton::Left);

	{

		Color widget_color = theme.widget_color;
		if (hovered || is_selected) {
			widget_color = theme.widget_hovered_color;
		}

		draw_rounded_rect(item_bounds, widget_color, theme.frame_corner_radius);
	}

	Vec2 saved_cursor = ui::get_cursor();
	ui::set_cursor(item_bounds.min + theme.frame_padding);

	ui::LayoutConfig layout_config{};

	ui::begin_horizontal_layout(&layout_config);

	{
		// Icon
		
		float icon_size = theme.default_font->size;
		
		ui::add_item(Vec2 { icon_size, icon_size });

		if (entry.icon != INVALID_ICON_POSITION) {
			draw_rect(ui::get_item_bounds(), WHITE, app_icon_storage.texture, get_icon_rect(app_icon_storage, entry.icon));
		} else {
			draw_rounded_rect(ui::get_item_bounds(), WHITE, theme.frame_corner_radius);
		}

		ui::append_item_spacing(theme.default_layout_config.item_spacing * 2.0f);
	}

	uint32_t cursor = 0;
	for (uint32_t i = match.highlights.start; i < match.highlights.start + match.highlights.count; i++) {
		RangeU32 highlight_range = state.highlights[i];

		if (cursor != highlight_range.start) {
			std::wstring_view t = std::wstring_view(entry.name)
				.substr(cursor, highlight_range.start - cursor);

			ui::text(t);
		}

		std::wstring_view highlighted_text = std::wstring_view(entry.name)
			.substr(highlight_range.start, highlight_range.count);

		ui::colored_text(highlighted_text, highlight_color);

		cursor = highlight_range.start + highlight_range.count;
	}

	if (cursor < entry.name.length()) {
		std::wstring_view t = std::wstring_view(entry.name)
			.substr(cursor);

		ui::text(t);
	}

	ui::end_horizontal_layout();

	ui::set_cursor(saved_cursor);

	if (pressed) {
		return EntryAction::Launch;
	}

	return EntryAction::None;
}

void append_entry(std::vector<Entry>& entries, const std::filesystem::path& path) {
	PROFILE_FUNCTION();
	Entry& entry = entries.emplace_back();
	entry.name = path.filename().replace_extension("").wstring();
	entry.path = path;
}

void walk_directory(const std::filesystem::path& path, std::vector<Entry>& entries) {
	PROFILE_FUNCTION();
	for (std::filesystem::path child : std::filesystem::directory_iterator(path)) {
		if (std::filesystem::is_directory(child)) {
			walk_directory(child, entries);
		} else {
			append_entry(entries, child);
		}
	}
}

void process_result_view_key_event(ResultViewState& state, KeyCode key) {
	size_t result_count = state.matches.size();

	switch (key) {
	case KeyCode::ArrowUp:
		state.selected_index = (state.selected_index + result_count - 1) % result_count;
		break;
	case KeyCode::ArrowDown:
		state.selected_index = (state.selected_index + 1) % result_count;
		break;
	}
}

void update_result_view_scroll(ResultViewState& state) {
	uint32_t visible_range_end = state.scroll_offset + state.visible_item_count;
	bool is_selection_visible = state.selected_index >= state.scroll_offset && state.selected_index < visible_range_end;

	if (is_selection_visible) {
		return;
	}

	if (state.selected_index >= visible_range_end) {
		state.scroll_offset += state.selected_index - visible_range_end + 1;
	} else if (state.selected_index < state.scroll_offset) {
		state.scroll_offset = state.selected_index;
	}
}

struct Icons {
	Rect search;
	Rect close;
	Rect enter;
	Rect nav;
};

static constexpr float ICON_SIZE = 32.0f;

Rect create_icon(UVec2 position, const Texture& texture) {

	float x = ((float)position.x * ICON_SIZE) / (float)texture.width;
	float y = ((float)position.y * ICON_SIZE) / (float)texture.height;

	float icon_width_uv = ICON_SIZE / (float)texture.width;
	float icon_height_uv = ICON_SIZE / (float)texture.height;

	return Rect { Vec2 { x, y }, Vec2 { x + icon_width_uv, y + icon_height_uv } };
}

void load_application_icons(std::vector<Entry>& entries, ApplicationIconsStorage& app_icon_storage) {
	PROFILE_FUNCTION();

	for (auto& entry : entries) {
		if (entry.path.extension() != ".lnk") {
			continue;
		}

		std::filesystem::path resolved_path = read_shortcut_path(entry.path);

		if (!std::filesystem::exists(resolved_path)) {
			continue;
		}

		Bitmap bitmap = get_file_icon(resolved_path);
		if (bitmap.pixels) {
			entry.icon = store_app_icon(app_icon_storage, bitmap.pixels);
			delete[] bitmap.pixels;
		}
	}
}

int main()
{
	initialize_platform();

	Window* window = create_window(800, 500, L"Instant Run");

	initialize_renderer(window);

	Texture icons_texture{};
	load_texture("./assets/icons.png", icons_texture);

	Icons icons{};
	icons.search = create_icon(UVec2 { 0, 0 }, icons_texture);
	icons.close = create_icon(UVec2 { 1, 0 }, icons_texture);
	icons.enter = create_icon(UVec2 { 2, 0 }, icons_texture);
	icons.nav = create_icon(UVec2 { 3, 0 }, icons_texture);

	Font font = load_font_from_file("./assets/Roboto/Roboto-Regular.ttf", 22.0f);
	ui::Theme theme{};
	theme.default_font = &font;

	ApplicationIconsStorage app_icon_storage{};
	initialize_app_icon_storage(app_icon_storage, 32, 32);

	theme.window_background = color_from_hex(0x242222FF);

	theme.widget_color = color_from_hex(0x242222FF);
	// NOTE: hovered_color & pressed_color are the same
	theme.widget_hovered_color = color_from_hex(0x37373AFF);
	theme.widget_pressed_color = color_from_hex(0x37373AFF);

	// NOTE: hovered_color & pressed_color are the same
	theme.default_button_style.color = color_from_hex(0x242222FF);
	theme.default_button_style.hovered_color = color_from_hex(0x37373AFF);
	theme.default_button_style.pressed_color = color_from_hex(0x37373AFF);

	theme.default_button_style.content_color = color_from_hex(0x9E9E9EFF);
	theme.default_button_style.content_hovered_color = WHITE;
	theme.default_button_style.content_pressed_color = color_from_hex(0x9E9E9EFF);

	theme.separator_color = color_from_hex(0x37373AFF);
	theme.text_color = WHITE;
	theme.prompt_text_color = color_from_hex(0x9E9E9EFF);
	theme.default_layout_config.item_spacing = 4.0f;
	theme.default_layout_config.padding = Vec2 { 12.0f, 12.0f };
	theme.frame_padding = Vec2 { 12.0f, 8.0f };
	theme.frame_corner_radius = 4.0f;

	theme.icon_size = ICON_SIZE;
	theme.icon_color = theme.prompt_text_color;
	theme.icon_hovered_color = WHITE;
	theme.icon_pressed_color = theme.prompt_text_color;

	Color highlight_color = color_from_hex(0xE6A446FF);

	constexpr size_t INPUT_BUFFER_SIZE = 128;
	wchar_t text_buffer[INPUT_BUFFER_SIZE];
	ui::TextInputState input_state{};
	input_state.buffer = Span(text_buffer, INPUT_BUFFER_SIZE);

	ui::initialize(*window);
	ui::set_theme(theme);

	ui::Options& options = ui::get_options();

	std::vector<Entry> entries;

	try {
		std::vector<std::filesystem::path> known_folders = get_start_menu_folder_path();
		for (const auto& known_folder : known_folders) {
			walk_directory(known_folder, entries);
		}
	} catch (std::exception e) {
		std::cout << e.what() << '\n';
	}

	load_application_icons(entries, app_icon_storage);

	ResultViewState result_view_state{};

	update_search_result({}, entries, result_view_state.matches, result_view_state.highlights);

	while (!window_should_close(window)) {
		PROFILE_BEGIN_FRAME("Main");

		poll_window_events(window);

		bool enter_pressed = false;

		Span<const WindowEvent> events = get_window_events(window);
		for (size_t i = 0; i < events.count; i++) {
			switch (events[i].kind) {
			case WindowEventKind::Key: {
				auto& key_event = events[i].data.key;
				if (key_event.action == InputAction::Pressed) {
					switch (key_event.code) {
					case KeyCode::Escape:
						close_window(*window);
						break;
					case KeyCode::Enter:
						enter_pressed = true;
						break;
					case KeyCode::F3:
						options.debug_layout = !options.debug_layout;
						break;
					default:
						process_result_view_key_event(result_view_state, key_event.code);
						break;
					}
				}
				break;
		    }
			}
		}

		begin_frame();
		ui::begin_frame();

		{
			ui::begin_horizontal_layout();

			float icon_width = ui::get_default_widget_height(); // icons are square
			float text_field_width = ui::get_available_layout_region_size().x
				- (icon_width + theme.default_layout_config.item_spacing) * 2.0f;

			ui::icon(icons_texture, icons.search);

			ui::push_next_item_fixed_size(text_field_width);

			if (ui::text_input(input_state, L"Search ...")) {
				std::wstring_view search_pattern(text_buffer, input_state.text_length);
				update_search_result(search_pattern, entries, result_view_state.matches, result_view_state.highlights);

				result_view_state.selected_index = 0;
			}

			ui::WidgetStyle close_icon_style = theme.default_button_style;
			close_icon_style.color = TRANSPARENT;
			close_icon_style.hovered_color = TRANSPARENT;
			close_icon_style.pressed_color = TRANSPARENT;

			if (ui::icon_button(icons_texture, icons.close, &close_icon_style)) {
				input_state.text_length = 0;
			}

			ui::end_horizontal_layout();
		}

		ui::separator();

		float available_height = ui::get_available_layout_space();
		float item_height = ui::get_default_widget_height() + theme.default_layout_config.item_spacing;

		result_view_state.visible_item_count = std::ceil(available_height / item_height);

		update_result_view_scroll(result_view_state);

		uint32_t visible_item_count = std::min(
				result_view_state.visible_item_count,
				(uint32_t)result_view_state.matches.size() - result_view_state.scroll_offset);

		for (uint32_t i = result_view_state.scroll_offset; i < result_view_state.scroll_offset + visible_item_count; i++) {
			bool is_selected = i == result_view_state.selected_index;

			const ResultEntry& match = result_view_state.matches[i];
			const Entry& entry = entries[match.entry_index];

			EntryAction action = draw_result_entry(match,
					entry,
					result_view_state,
					is_selected,
					highlight_color,
					app_icon_storage);

			if (is_selected && enter_pressed) {
				action = EntryAction::Launch;
			}

			if (action == EntryAction::Launch) {
				run_file(entry.path, false);
			}
		}

		ui::end_frame();
		end_frame();

		swap_window_buffers(window);

		PROFILE_END_FRAME("Main");
	}

	delete_texture(app_icon_storage.texture);
	delete_texture(icons_texture);
	delete_font(font);

	shutdown_renderer();

	destroy_window(window);

	shutdown_platform();

	return 0;
}
