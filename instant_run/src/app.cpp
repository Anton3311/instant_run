#include "app.h"
#include "platform.h"
#include "renderer.h"
#include "ui.h"
#include "log.h"
#include "job_system.h"

#include "hook_config.h"

#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <iostream>

static constexpr UVec2 INVALID_ICON_POSITION = UVec2 { UINT32_MAX, UINT32_MAX };

struct ApplicationIconsStorage {
	using IconId = void*;

	Texture texture;
	uint32_t icon_size;
	uint32_t write_offset;
	uint32_t grid_size;

	std::unordered_map<IconId, UVec2> ext_to_icon;
};

struct Icons {
	Texture texture;
	Rect search;
	Rect close;
	Rect enter;
	Rect nav;
	Rect run;
	Rect run_as_admin;
	Rect copy;
};

struct Entry {
	std::wstring name;
	std::filesystem::path path;

	bool icon_is_loaded;
	UVec2 icon;

	const wchar_t* id;
	bool is_microsoft_store_app;

	uint16_t frequency_score;
};

struct ResultEntry {
	uint32_t entry_index;
	uint32_t score;

	RangeU32 highlights;
};

struct ResultViewState {
	uint32_t selected_index;
	uint32_t scroll_offset;
	uint32_t fully_visible_item_count;
	
	std::vector<ResultEntry> matches;
	std::vector<RangeU32> highlights;
};


enum class AppState {
	Running,
	Sleeping,
};

struct App {
	AppState state;
	std::mutex enable_mutex;
	std::condition_variable enable_var;

	// Keyboard hook
	KeyboardHookHandle keyboard_hook;

	alignas(64) std::atomic_bool is_active;

	// App state
	bool use_keyboard_hook;
	Font font;
	Arena arena;
	Window* window;
	bool wait_for_window_events;
	Icons icons;
	ApplicationIconsStorage app_icon_storage;
	ui::TextInputState search_input_state;
	std::vector<Entry> entries;
	ResultViewState result_view_state;
	Color highlight_color;
};

static App s_app;

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

//
// Searching
//

// FIXME: Don't hardcode uppercase ranges
inline wchar_t to_lower_case(wchar_t c) {
	if (c >= L'A' && c <= 'Z') {
		return c - L'A' + 'a';
	}
	
	if (c == L'І') {
		return L'і';
	}

	if (c == L'Ї') {
		return L'ї';
	}

	if (c >= L'А' && c <= L'Я') {
		return c - L'А' + L'а';
	}

	return c;
}

uint32_t compute_search_score(std::wstring_view string,
		std::wstring_view pattern,
		std::vector<RangeU32>& sequence_ranges,
		RangeU32& highlight_range) {
	PROFILE_FUNCTION();

	highlight_range.start = static_cast<uint32_t>(sequence_ranges.size());

	size_t pattern_index = 0;

	size_t matches = 0;
	size_t max_substring_length = 0;
	size_t substring_length = 0;
	size_t substring_start = 0;

	for (size_t i = 0; i < string.length() && pattern_index < pattern.length(); i++) {
		if (substring_length == 0) {
			substring_start = i;
		}

		if (to_lower_case(string[i]) == to_lower_case(pattern[pattern_index])) {
			pattern_index += 1;
			substring_length += 1;
			matches += 1;
		} else {
			if (substring_length != 0) {
				highlight_range.count += 1;
				sequence_ranges.push_back(RangeU32 { (uint32_t)substring_start, (uint32_t)substring_length });
			}

			max_substring_length = max(max_substring_length, substring_length);
			substring_length = 0;
		}
	}

	if (substring_length != 0) {
		highlight_range.count += 1;
		sequence_ranges.push_back(RangeU32 { (uint32_t)substring_start, (uint32_t)substring_length });
	}

	max_substring_length = max(max_substring_length, substring_length);
	return matches + max_substring_length;
}

void update_search_result(std::wstring_view search_pattern,
		const std::vector<Entry>& entries,
		std::vector<ResultEntry>& result,
		std::vector<RangeU32>& sequence_ranges,
		Arena& arena) {
	PROFILE_FUNCTION();

	result.clear();
	sequence_ranges.clear();

	for (size_t i = 0; i < entries.size(); i++) {
		const Entry& entry = entries[i];

		RangeU32 highlight_range{};
		uint32_t score = compute_search_score(entry.name, search_pattern, sequence_ranges, highlight_range);

		// The frequency_score is store in lower half of the int,
		// so that when the string matching scores of both entries are equal
		// the `frequency_score` is used to prioritize the most used entry
		score = ((score & 0xff) << 16) | ((uint32_t)(entry.frequency_score));

		result.push_back({ (uint32_t)i, score, highlight_range });
	}

	std::sort(result.begin(), result.end(), [&](auto a, auto b) -> bool {
		return a.score > b.score;
	});
}

//
// Result View
//

enum class EntryAction {
	None,
	Launch,
	LaunchAsAdmin,
	CopyPath,
};

float compute_result_entry_height(const ApplicationIconsStorage& app_icon_storage) {
	const ui::Theme& theme = ui::get_theme();
	return (float)app_icon_storage.icon_size + theme.frame_padding.y * 2.0f;
}

void draw_result_entry_text(const Entry& entry,
		const ResultEntry& match,
		const ResultViewState& state,
		Color highlight_color,
		float available_width) {
	PROFILE_FUNCTION();

	const ui::Theme& theme = ui::get_theme();
	const Color default_text_color = theme.text_color;

	ui::LayoutConfig layout_config{};
	ui::begin_fixed_horizontal_layout(Vec2 { available_width, ui::get_default_font_height() }, &layout_config);

	ArenaSavePoint temp = arena_begin_temp(s_app.arena);

	// Assume the worst case when the there are non-highlighted text in between highlighted ranges
	// + two non-highlighted ranges at the start and at the end.
	uint32_t max_text_part_count = match.highlights.count * 2 + 1;

	std::wstring_view* text_parts = arena_alloc_array<std::wstring_view>(s_app.arena, max_text_part_count);
	Color* part_colors = arena_alloc_array<Color>(s_app.arena, max_text_part_count);

	uint32_t part_count = 0;

	uint32_t cursor = 0;
	for (uint32_t i = match.highlights.start; i < match.highlights.start + match.highlights.count; i++) {
		RangeU32 highlight_range = state.highlights[i];

		if (cursor != highlight_range.start) {
			text_parts[part_count] = std::wstring_view(entry.name)
				.substr(cursor, highlight_range.start - cursor);

			part_colors[part_count] = default_text_color;
			part_count += 1;
		}


		text_parts[part_count] = std::wstring_view(entry.name)
			.substr(highlight_range.start, highlight_range.count);

		part_colors[part_count] = highlight_color;
		part_count += 1;

		cursor = highlight_range.start + highlight_range.count;
	}

	if (cursor < entry.name.length()) {
		text_parts[part_count] = std::wstring_view(entry.name).substr(cursor);
		part_colors[part_count] = default_text_color;
		part_count += 1;
	}

	// finally renderer the text parts
	ui::colored_text(text_parts, part_colors, part_count);

	arena_end_temp(temp);

	ui::end_horizontal_layout();
}

EntryAction draw_result_entry(const ResultEntry& match,
		const Entry& entry,
		const ResultViewState& state,
		bool is_selected,
		Color highlight_color,
		const ApplicationIconsStorage& app_icon_storage,
		const Icons& icons) {
	PROFILE_FUNCTION();
	const ui::Theme& theme = ui::get_theme();

	const float item_height = compute_result_entry_height(app_icon_storage);
	ui::LayoutConfig entry_layout_config = theme.default_layout_config;
	entry_layout_config.padding = theme.frame_padding;
	entry_layout_config.cross_axis_align = ui::AxisAlignment::Center;
	ui::begin_horizontal_layout(&entry_layout_config, &item_height);

	Rect item_bounds = ui::get_max_layout_bounds();

	EntryAction action = EntryAction::None;

	bool hovered = ui::is_rect_hovered(item_bounds);
	bool pressed = hovered && ui::is_mouse_button_pressed(MouseButton::Left);

	if (pressed) {
		action = EntryAction::Launch;
	}

	{
		Color widget_color = theme.widget_color;
		if (hovered || is_selected) {
			widget_color = theme.widget_hovered_color;
		}

		draw_rounded_rect(item_bounds, widget_color, theme.frame_corner_radius);
	}

	{
		// Icon
		
		float icon_size = (float)app_icon_storage.icon_size;
		
		ui::add_item(Vec2 { icon_size, icon_size });

		if (entry.icon != INVALID_ICON_POSITION) {
			draw_rect(ui::get_item_bounds(), WHITE, app_icon_storage.texture, get_icon_rect(app_icon_storage, entry.icon));
		} else {
			draw_rounded_rect(ui::get_item_bounds(), WHITE, theme.frame_corner_radius);
		}
	}

	float available_width = ui::get_available_layout_space();
	Vec2 text_cursor_position = ui::get_cursor();

	if (hovered || is_selected)
	{
		float icon_size = font_get_height(*theme.default_font);

		uint32_t icon_button_count = 3;
		
		// Remove run as admin for MS store apps
		if (entry.is_microsoft_store_app) {
			icon_button_count -= 2;
		}

		float icon_row_width = (float)icon_button_count * icon_size
			+ (float)(icon_button_count - 1) * theme.default_layout_config.item_spacing;

		Vec2 cursor = ui::get_cursor();
		cursor.x += available_width - icon_row_width;

		ui::set_cursor(cursor);

		ui::WidgetStyle close_icon_style = theme.default_button_style;
		close_icon_style.color = TRANSPARENT;
		close_icon_style.hovered_color = TRANSPARENT;
		close_icon_style.pressed_color = TRANSPARENT;

		if (ui::icon_button(icons.texture, icons.run, &close_icon_style, &icon_size)) {
			action = EntryAction::Launch;
		}

		if (!entry.is_microsoft_store_app) {
			if (ui::icon_button(icons.texture, icons.run_as_admin, &close_icon_style, &icon_size)) {
				action = EntryAction::LaunchAsAdmin;
			}

			if (ui::icon_button(icons.texture, icons.copy, &close_icon_style, &icon_size)) {
				action = EntryAction::CopyPath;
			}
		}

		// Reduce the available_width of the text row, so it doesn't overflow or go under the iocn buttons
		available_width -= icon_row_width + theme.default_layout_config.item_spacing;
	}

	ui::set_cursor(text_cursor_position);
	draw_result_entry_text(entry, match, state, highlight_color, available_width);

	ui::end_horizontal_layout();

	return action;
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
	default:
		break;
	}
}

void update_result_view_scroll(ResultViewState& state) {
	uint32_t visible_range_end = state.scroll_offset + state.fully_visible_item_count;
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

void try_load_app_entry_icon(ApplicationIconsStorage& app_icon_storage, Entry& entry, Arena& arena) {
	PROFILE_FUNCTION();
	if (entry.icon_is_loaded) {
		return;
	}

	SystemIconHandle icon_handle = fs_query_file_icon(entry.path);
	ApplicationIconsStorage::IconId icon_id = icon_handle;

	if (!icon_handle) {
		entry.icon_is_loaded = true; // loaded but invalid
		return;
	}

	auto it = app_icon_storage.ext_to_icon.find(icon_id);
	if (it != app_icon_storage.ext_to_icon.end()) {
		entry.icon = it->second;
		entry.icon_is_loaded = true;
		return;
	}

	ArenaSavePoint temp_region = arena_begin_temp(arena);
	Bitmap bitmap = fs_extract_icon_bitmap(icon_handle, arena);
	if (bitmap.pixels) {
		UVec2 icon = store_app_icon(app_icon_storage, bitmap.pixels);
		app_icon_storage.ext_to_icon.emplace(icon_id, icon);
		entry.icon = icon;
	}

	fs_release_file_icon(icon_handle);

	arena_end_temp(temp_region);
	entry.icon_is_loaded = true;
}

void enable_app() {
	PROFILE_FUNCTION();

	log_info("recieved activation notification from the keyboard hook");

	s_app.enable_var.notify_all();
	s_app.is_active.store(true, std::memory_order::acquire);
}

// Waits for the hook to notify about the activation
void wait_for_activation() {
	if (!s_app.use_keyboard_hook) {
		s_app.state = AppState::Running;
		return;
	}

	// Can happen that the hook notifies before this function is called
	bool is_already_active = s_app.is_active.load(std::memory_order::relaxed);
	if (is_already_active) {
		log_info("already activated");
	} else {
		log_info("waiting for activation");
		std::unique_lock lock(s_app.enable_mutex);
		s_app.enable_var.wait(lock);
	}

	// The app was activated
	s_app.state = AppState::Running;
}

void enter_sleep_mode() {
	if (!s_app.use_keyboard_hook) {
		window_close(*s_app.window);
		return;
	}

	log_info("entering sleep mode");
	s_app.is_active.store(false, std::memory_order::acquire);
	wait_for_activation();
}

//
// Keyboard Hook
//

bool init_keyboard_hook(Arena& allocator) {
	PROFILE_FUNCTION();
	HookConfig config{};
	config.app_enable_fn = enable_app;
	s_app.keyboard_hook = keyboard_hook_init(allocator, config);

	return s_app.keyboard_hook != nullptr;
}

void shutdown_keyboard_hook() {
	keyboard_hook_shutdown(s_app.keyboard_hook);
}

//
// Search Entries
//

void resolve_shortcuts_task(const JobContext& context, void* data) {
	PROFILE_FUNCTION();

	Span<Entry> entries = Span(reinterpret_cast<Entry*>(data), context.batch_size);
	for (Entry& entry : entries) {
		if (entry.path.extension() == ".lnk") {
			entry.path = fs_resolve_shortcut(entry.path);
		}
	}
}

void walk_directory(const std::filesystem::path& path, std::vector<Entry>& entries) {
	PROFILE_FUNCTION();
	for (std::filesystem::path child : std::filesystem::directory_iterator(path)) {
		if (std::filesystem::is_directory(child)) {
			walk_directory(child, entries);
		} else {
			Entry& entry = entries.emplace_back();
			entry.name = child.filename().replace_extension("").wstring();
			entry.path = child;
		}
	}
}

struct SearchEntriesQuery {
	InstalledAppsQueryState* installed_apps_query;
};

void schedule_search_entries_query(Arena& arena, SearchEntriesQuery& query_state) {
	PROFILE_FUNCTION();

	std::vector<std::filesystem::path> known_folders = get_user_folders(
			UserFolderKind::Desktop | UserFolderKind::StartMenu | UserFolderKind::Programs);

	for (const auto& known_folder : known_folders) {
		try {
			walk_directory(known_folder, s_app.entries);
		} catch (std::exception e) {
			log_error(e.what());
		}
	}

	job_system_submit(resolve_shortcuts_task, s_app.entries.data(), s_app.entries.size());

	query_state.installed_apps_query = platform_begin_installed_apps_query(arena);
}

void collect_search_entries_query_result(Arena& arena, SearchEntriesQuery& query_state) {
	PROFILE_FUNCTION();

	job_system_wait_for_all(arena);
	
	std::vector<InstalledAppDesc> installed_apps = platform_finish_installed_apps_query(
			query_state.installed_apps_query,
			arena);

	// TODO: reserve entries
	for (const auto& app_desc : installed_apps) {
		Entry& entry = s_app.entries.emplace_back();
		entry.name = app_desc.display_name;
		entry.is_microsoft_store_app = true;
		entry.icon = INVALID_ICON_POSITION;
		entry.id = app_desc.id;

		TexturePixelData data = texture_load_pixel_data(app_desc.logo_uri);
		if (data.pixels) {
			ArenaSavePoint temp = arena_begin_temp(arena);
			TexturePixelData downsampled = texture_downscale(data, 32, s_app.arena);

			entry.icon = store_app_icon(s_app.app_icon_storage, downsampled.pixels);

			texture_release_pixel_data(data);
			arena_end_temp(temp);
		}
	}

	{
		ArenaSavePoint temp = arena_begin_temp(arena);
		StringBuilder builder = { &arena };
		str_builder_append(builder, "loaded ");
		str_builder_append(builder, std::to_string(s_app.entries.size()));
		str_builder_append(builder, " entries");

		log_info(std::string_view(builder.string, builder.length));
		arena_end_temp(temp);
	}

}

void clear_search_result() {
	PROFILE_FUNCTION();

	ui::text_input_state_clear(s_app.search_input_state);
	update_search_result({}, s_app.entries, s_app.result_view_state.matches, s_app.result_view_state.highlights, s_app.arena);
}

//
// Application Launching
//

struct EntryLaunchParams {
	bool as_admin;
	Entry entry;
};

// Reposible for freing the `EntryLaunchParams`
void launch_app_task(const JobContext& context, void* data) {
	PROFILE_FUNCTION();
	EntryLaunchParams* params = reinterpret_cast<EntryLaunchParams*>(data);

	if (params->as_admin) {
		// NOTE: No support for launching MS store apps with admin rights
		if (!params->entry.is_microsoft_store_app) {
			platform_run_file(params->entry.path, true);
		}
	} else {
		if (params->entry.is_microsoft_store_app) {
			platform_launch_installed_app(params->entry.id);
		} else {
			platform_run_file(params->entry.path, false);
		}
	}

	delete params;
}

//
// Application Logic
//

static constexpr float ICON_SIZE = 32.0f;

Rect create_icon(UVec2 position, const Texture& texture) {

	float x = ((float)position.x * ICON_SIZE) / (float)texture.width;
	float y = ((float)position.y * ICON_SIZE) / (float)texture.height;

	float icon_width_uv = ICON_SIZE / (float)texture.width;
	float icon_height_uv = ICON_SIZE / (float)texture.height;

	return Rect { Vec2 { x, y }, Vec2 { x + icon_width_uv, y + icon_height_uv } };
}

void initialize_app() {
	PROFILE_FUNCTION();
	s_app.window = window_create(800, 500, L"Instant Run");

	initialize_renderer(s_app.window);
	initialize_app_icon_storage(s_app.app_icon_storage, 32, 32);

	Icons& icons = s_app.icons;
	load_texture("./assets/icons.png", icons.texture);
	icons.search = create_icon(UVec2 { 0, 0 }, icons.texture);
	icons.close = create_icon(UVec2 { 1, 0 }, icons.texture);
	icons.enter = create_icon(UVec2 { 2, 0 }, icons.texture);
	icons.nav = create_icon(UVec2 { 3, 0 }, icons.texture);
	icons.run = create_icon(UVec2 { 0, 1 }, icons.texture);
	icons.run_as_admin = create_icon(UVec2 { 1, 1 }, icons.texture);
	icons.copy = create_icon(UVec2 { 2, 1 }, icons.texture);

	s_app.font = load_font_from_file("./assets/Roboto/Roboto-Regular.ttf", 22.0f, s_app.arena);

	ui::Theme theme{};
	theme.default_font = &s_app.font;
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
	theme.default_layout_config.item_spacing = 8.0f;
	theme.default_layout_config.padding = Vec2 { 12.0f, 12.0f };
	theme.frame_padding = Vec2 { 12.0f, 8.0f };
	theme.frame_corner_radius = 4.0f;

	theme.icon_size = ICON_SIZE;
	theme.icon_color = theme.prompt_text_color;
	theme.icon_hovered_color = WHITE;
	theme.icon_pressed_color = theme.prompt_text_color;

	s_app.highlight_color = color_from_hex(0xE6A446FF);

	constexpr size_t INPUT_BUFFER_SIZE = 128;
	ui::TextInputState& input_state = s_app.search_input_state;
	input_state.buffer = Span(arena_alloc_array<wchar_t>(s_app.arena, INPUT_BUFFER_SIZE), INPUT_BUFFER_SIZE);

	ui::initialize(*s_app.window, s_app.arena);
	ui::set_theme(theme);

	ui::Options& options = ui::get_options();
#ifdef BUILD_DEV
	options.debug_layout_overflow = true;
#endif

	s_app.state = AppState::Sleeping;
	s_app.wait_for_window_events = false;
}

void run_app_frame() {
	PROFILE_FUNCTION();

	bool enter_pressed = false;

	const ui::Theme& theme = ui::get_theme();
	ui::Options& options = ui::get_options();

	Span<const WindowEvent> events = window_get_events(s_app.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
		case WindowEventKind::FocusLost:
			if (s_app.use_keyboard_hook) {
				s_app.state = AppState::Sleeping;
			}
			break;
		case WindowEventKind::Key: {
			auto& key_event = events[i].data.key;
			if (key_event.action == InputAction::Pressed) {
				switch (key_event.code) {
				case KeyCode::Escape:
					s_app.state = AppState::Sleeping;
					break;
				case KeyCode::Enter:
					enter_pressed = true;
					break;
#ifdef BUILD_DEV
				case KeyCode::F3:
					options.debug_layout = !options.debug_layout;
					options.debug_item_bounds = !options.debug_item_bounds;
					break;
#endif
				default:
					process_result_view_key_event(s_app.result_view_state, key_event.code);
					break;
				}
			}
			break;
		}
		default:
			break;
		}
	}

	begin_frame();
	ui::begin_frame();

	{
		ui::begin_horizontal_layout();

		Icons& icons = s_app.icons;

		float icon_width = ui::get_default_widget_height(); // icons are square
		float text_field_width = ui::get_available_layout_region_size().x
			- (icon_width + theme.default_layout_config.item_spacing) * 2.0f;

		ui::icon(icons.texture, icons.search);

		ui::push_next_item_fixed_size(text_field_width);

		if (ui::text_input(s_app.search_input_state, L"Search ...")) {
			std::wstring_view search_pattern(s_app.search_input_state.buffer.values, s_app.search_input_state.text_length);
			update_search_result(search_pattern,
					s_app.entries,
					s_app.result_view_state.matches,
					s_app.result_view_state.highlights,
					s_app.arena);

			s_app.result_view_state.selected_index = 0;
		}

		ui::WidgetStyle close_icon_style = theme.default_button_style;
		close_icon_style.color = TRANSPARENT;
		close_icon_style.hovered_color = TRANSPARENT;
		close_icon_style.pressed_color = TRANSPARENT;

		if (ui::icon_button(icons.texture, icons.close, &close_icon_style)) {
			s_app.search_input_state.text_length = 0;
		}

		ui::end_horizontal_layout();
	}

	ui::separator();

	ui::LayoutConfig result_list_layout_config{};
	result_list_layout_config.padding = Vec2{};
	result_list_layout_config.allow_overflow = true;
	result_list_layout_config.item_spacing = theme.default_layout_config.item_spacing;
	ui::begin_vertical_layout(&result_list_layout_config);

	float available_height = ui::get_available_layout_space();
	float item_height = compute_result_entry_height(s_app.app_icon_storage);
	float item_spacing = theme.default_layout_config.item_spacing;

	float item_count = (available_height + item_spacing) / (item_height + item_spacing);
	s_app.result_view_state.fully_visible_item_count = (uint32_t)std::ceil(item_count);
	uint32_t partially_visible_item_count = (uint32_t)std::ceil(item_count);

	update_result_view_scroll(s_app.result_view_state);

	uint32_t visible_item_count = std::min(
			partially_visible_item_count,
			(uint32_t)s_app.result_view_state.matches.size() - s_app.result_view_state.scroll_offset);

	auto& result_view_state = s_app.result_view_state;

	for (uint32_t i = result_view_state.scroll_offset; i < result_view_state.scroll_offset + visible_item_count; i++) {
		bool is_selected = i == result_view_state.selected_index;

		const ResultEntry& match = s_app.result_view_state.matches[i];
		Entry& entry = s_app.entries[match.entry_index];

		if (!entry.icon_is_loaded) {
			try_load_app_entry_icon(s_app.app_icon_storage, entry, s_app.arena);
		}

		EntryAction action = draw_result_entry(match,
				entry,
				s_app.result_view_state,
				is_selected,
				s_app.highlight_color,
				s_app.app_icon_storage,
				s_app.icons);

		if (action == EntryAction::None && is_selected && enter_pressed) {
			action = EntryAction::Launch;
		}

		switch (action) {
		case EntryAction::None:
			break;
		case EntryAction::Launch:
		case EntryAction::LaunchAsAdmin: {
			EntryLaunchParams* params = new EntryLaunchParams();
			params->as_admin = action == EntryAction::LaunchAsAdmin;
			params->entry = entry;

			if (entry.frequency_score != UINT16_MAX) {
				entry.frequency_score += 1;
			}

			job_system_submit(launch_app_task, params);

			s_app.state = AppState::Sleeping;
			clear_search_result();
			break;
		}
		case EntryAction::CopyPath:
			// path is not available for imicrosoft store apps
			if (!entry.is_microsoft_store_app) {
				window_copy_text_to_clipboard(*s_app.window, entry.path.wstring());
			}
			break;
		}
	}

	ui::end_vertical_layout();

	ui::end_frame();
	end_frame();

	window_swap_buffers(s_app.window);

	s_app.wait_for_window_events = true;
}

int run_app(CommandLineArgs cmd_args) {
	s_app.use_keyboard_hook = true;
	if (cmd_args.count == 2) {
		std::wstring_view arg1 = cmd_args.arguments[1];
		if (arg1 == L"--no-hook") {
			s_app.use_keyboard_hook = false;
		}
	}

	query_system_memory_spec();

	s_app.arena = {};
	s_app.arena.capacity = mb_to_bytes(8);

	log_init("log.txt", true);
	log_init_thread(s_app.arena, "main");

	log_info("logger started");

	job_system_init(4);

	platform_initialize();

	SearchEntriesQuery search_entries_query{};
	schedule_search_entries_query(s_app.arena, search_entries_query);

	if (s_app.use_keyboard_hook) {
		init_keyboard_hook(s_app.arena);
	} else {
		log_info("running without the keyboard hook");
	}

	initialize_app();
	window_hide(s_app.window);

	// At this point the search entry must be made available
	collect_search_entries_query_result(s_app.arena, search_entries_query);

	clear_search_result();

	{
		wait_for_activation();
		log_info("initial start");

		window_show(s_app.window);
		window_focus(s_app.window);
	}

	while (!window_should_close(s_app.window)) {
		switch (s_app.state) {
		case AppState::Running:
			PROFILE_BEGIN_FRAME("Main");

			if (s_app.wait_for_window_events) {
				window_wait_for_events(s_app.window);
			} else {
				window_poll_events(s_app.window);
			}

			run_app_frame();
			PROFILE_END_FRAME("Main");
			break;
		case AppState::Sleeping:
			clear_search_result();

			window_hide(s_app.window);
			enter_sleep_mode();

			window_show(s_app.window);
			window_focus(s_app.window);
			break;
		}
	}

	log_info("terminated");

	if (s_app.use_keyboard_hook) {
		shutdown_keyboard_hook();
	}

	delete_texture(s_app.app_icon_storage.texture);
	delete_texture(s_app.icons.texture);
	delete_font(s_app.font);

	shutdown_renderer();
	window_destroy(s_app.window);

	job_system_shutdown();
	platform_shutdown();

	log_shutdown_thread();
	log_shutdown();

	arena_release(s_app.arena);

	return 0;
}
