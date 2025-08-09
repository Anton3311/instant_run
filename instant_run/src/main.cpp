#include "platform.h"
#include "renderer.h"
#include "ui.h"
#include "log.h"

#include "hook_config.h"

#include <filesystem>
#include <string>
#include <vector>
#include <mutex>

#undef TRANSPARENT

static constexpr UVec2 INVALID_ICON_POSITION = UVec2 { UINT32_MAX, UINT32_MAX };

struct ApplicationIconsStorage {
	Texture texture;
	uint32_t icon_size;
	uint32_t write_offset;
	uint32_t grid_size;
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
	Font font;
	Arena arena;
	Window* window;
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

uint32_t compute_longest_common_substring(
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

enum class LCSDirection : uint16_t {
	None,
	Diagonal,
	Horizontal,
	Vertical
};

struct LCSCell {
	uint16_t value;
	LCSDirection direction;
};

uint32_t compute_longest_common_subsequence(
		const std::wstring_view& string,
		const std::wstring_view& pattern,
		std::vector<RangeU32>& sequence_ranges,
		RangeU32& highlight_range,
		Arena& arena) {
	PROFILE_FUNCTION();

	if (string.length() == 0 || pattern.length() == 0) {
		return 0;
	}

	ArenaSavePoint temp_region = arena_begin_temp(arena);

	size_t grid_width = pattern.length() + 1;
	size_t grid_height = string.length() + 1;
	LCSCell* cells = arena_alloc_array<LCSCell>(arena, grid_width * grid_height);

	for (size_t x = 0; x < grid_width; x++) {
		cells[x] = LCSCell {};
	}

	for (size_t y = 0; y < grid_height; y++) {
		cells[y * grid_width] = LCSCell {};
	}

	highlight_range.start = static_cast<uint32_t>(sequence_ranges.size());

	for (size_t y = 1; y <= string.length(); y++) {
		for (size_t x = 1; x <= pattern.length(); x++) {
			LCSCell& current_cell = cells[y * grid_width + x];
	
			wchar_t string_char = to_lower_case(string[y - 1]);
			wchar_t pattern_char = to_lower_case(pattern[x - 1]);
			if (string_char == pattern_char) {
				 current_cell = LCSCell {
					.value = (uint16_t)(cells[(y - 1) * grid_width + x - 1].value + 1),
					.direction = LCSDirection::Diagonal
				};
			} else {
				LCSCell& horizontal = cells[y * grid_width + x - 1];
				LCSCell& vertical = cells[(y - 1) * grid_width + x];

				if (horizontal.value > vertical.value) {
					current_cell = LCSCell {
						.value = horizontal.value,
						.direction = LCSDirection::Horizontal
					};
				} else {
					current_cell = LCSCell {
						.value = vertical.value,
						.direction = LCSDirection::Vertical
					};
				}
			}
		}
	}
	
	uint16_t similarity_score = cells[(grid_height - 1) * grid_width + (grid_width - 1)].value;
	uint16_t longest_substr_score = 0;

	// Generate highlight ranges
	{
		PROFILE_SCOPE("Generate highlight ranges");
		struct BacktrackEntry {
			LCSDirection direction;
			uint32_t y;
		};

		// Preallocate for the worst case
		size_t backtrack_buffer_size = grid_width * grid_height - 1;
		BacktrackEntry* backtrack = arena_alloc_array<BacktrackEntry>(arena, backtrack_buffer_size);
		// NOTE: It is off by 1 in order to avoid an overflow.
		// 	     It points to the first valid entry staring from the left.
		size_t backtrack_insert_index = backtrack_buffer_size;

		UVec2 position = UVec2 { (uint32_t)(grid_width - 1), (uint32_t)(grid_height - 1) };
		while (true) {
			LCSCell& cell = cells[position.y * grid_width + position.x];

			if (cell.direction == LCSDirection::None) {
				break;
			}

			backtrack[backtrack_insert_index - 1] = BacktrackEntry { .direction = cell.direction, .y = position.y - 1 };
			backtrack_insert_index -= 1;

			switch (cell.direction) {
			case LCSDirection::Vertical:
				position.y -= 1;
				break;
			case LCSDirection::Horizontal:
				position.x -= 1;
				break;
			case LCSDirection::Diagonal:
				position.x -= 1;
				position.y -= 1;
				break;
			case LCSDirection::None:
				break;
			}
		}

		RangeU32 range{};
		for (size_t i = backtrack_insert_index; i < backtrack_buffer_size; i++) {
			if (backtrack[i].direction == LCSDirection::Diagonal) {
				if (range.count == 0) {
					range.start = backtrack[i].y;
					range.count = 1;
				} else {
					range.count += 1;
				}
			} else {
				if (range.count != 0) {
					sequence_ranges.push_back(range);
					longest_substr_score = max(longest_substr_score, (uint16_t)range.count);
					highlight_range.count += 1;

					range.count = 0;
				}
			}
		}

		if (range.count > 0) {
			sequence_ranges.push_back(range);
			longest_substr_score = max(longest_substr_score, (uint16_t)range.count);
			highlight_range.count += 1;
		}
	}

	arena_end_temp(temp_region);

	return (uint32_t)similarity_score << 16 | (uint32_t)longest_substr_score;
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

		uint32_t score = compute_longest_common_subsequence(
				entry.name,
				search_pattern,
				sequence_ranges,
				highlight_range,
				arena);

		result.push_back({ (uint32_t)i, score, highlight_range });
	}

	std::sort(result.begin(), result.end(), [&](auto a, auto b) -> bool {
		return a.score > b.score;
	});
}

enum class EntryAction {
	None,
	Launch,
	LaunchAsAdmin,
	CopyPath,
};

float compute_result_entry_height() {
	return ui::get_default_widget_height();
}

void draw_result_entry_text(const Entry& entry,
		const ResultEntry& match,
		const ResultViewState& state,
		Color highlight_color,
		float available_width) {

	ui::LayoutConfig layout_config{};
	ui::begin_fixed_horizontal_layout(Vec2 { available_width, ui::get_default_font_height() }, &layout_config);

	uint32_t cursor = 0;
	for (uint32_t i = match.highlights.start; i < match.highlights.start + match.highlights.count; i++) {
		RangeU32 highlight_range = state.highlights[i];

		if (cursor != highlight_range.start) {
			std::wstring_view t = std::wstring_view(entry.name)
				.substr(cursor, highlight_range.start - cursor);

			ui::text(t);

			float text_width = ui::get_item_bounds().width();
			available_width -= text_width;
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

	const float item_height = compute_result_entry_height();
	ui::begin_horizontal_layout(nullptr, &item_height);

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
		
		float icon_size = theme.default_font->size;
		
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

		if (ui::icon_button(icons.texture, icons.run_as_admin, &close_icon_style, &icon_size)) {
			action = EntryAction::LaunchAsAdmin;
		}

		if (ui::icon_button(icons.texture, icons.copy, &close_icon_style, &icon_size)) {
			action = EntryAction::CopyPath;
		}

		// Reduce the available_width of the text row, so it doesn't overflow or go under the iocn buttons
		available_width -= icon_row_width + theme.default_layout_config.item_spacing;
	}

	ui::set_cursor(text_cursor_position);
	draw_result_entry_text(entry, match, state, highlight_color, available_width);

	ui::end_horizontal_layout();

	return action;
}

void append_entry(std::vector<Entry>& entries, const std::filesystem::path& path) {
	PROFILE_FUNCTION();
	
	Entry& entry = entries.emplace_back();
	entry.name = path.filename().replace_extension("").wstring();

	if (path.extension() == ".lnk") {
		std::filesystem::path resolved_path = read_shortcut_path(path);

		entry.path = resolved_path;
	} else {
		entry.path = path;
	}
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

static constexpr float ICON_SIZE = 32.0f;

Rect create_icon(UVec2 position, const Texture& texture) {

	float x = ((float)position.x * ICON_SIZE) / (float)texture.width;
	float y = ((float)position.y * ICON_SIZE) / (float)texture.height;

	float icon_width_uv = ICON_SIZE / (float)texture.width;
	float icon_height_uv = ICON_SIZE / (float)texture.height;

	return Rect { Vec2 { x, y }, Vec2 { x + icon_width_uv, y + icon_height_uv } };
}

void load_application_icons(std::vector<Entry>& entries, ApplicationIconsStorage& app_icon_storage, Arena& arena) {
	PROFILE_FUNCTION();

	for (auto& entry : entries) {
		bool is_shortcut = false;

		std::filesystem::path resolved_path;

		if (entry.path.extension() == ".lnk") {
			is_shortcut = true;
			resolved_path = read_shortcut_path(entry.path);

			if (!std::filesystem::exists(resolved_path)) {
				continue;
			}
		}

		ArenaSavePoint temp_region = arena_begin_temp(arena);
		Bitmap bitmap = get_file_icon(is_shortcut ? resolved_path : entry.path, arena);
		if (bitmap.pixels) {
			entry.icon = store_app_icon(app_icon_storage, bitmap.pixels);
		}

		arena_end_temp(temp_region);
	}
}

void enable_app() {
	PROFILE_FUNCTION();

	log_info("recieved activation notification from the keyboard hook");

	s_app.enable_var.notify_all();
	s_app.is_active.store(true, std::memory_order::acquire);
}

// Waits for the hook to notify about the activation
void wait_for_activation() {
	// Can happen that the hook notifies before this function is called
	bool is_already_active = s_app.is_active.load(std::memory_order::relaxed);
	if (is_already_active) {
		log_info("already activated");
		return;
	}

	{
		log_info("waiting for activation");
		std::unique_lock lock(s_app.enable_mutex);
		s_app.enable_var.wait(lock);
	}

	// The app was activated
	s_app.state = AppState::Running;
}

void enter_sleep_mode() {
	log_info("entering sleep mode");
	s_app.is_active.store(false, std::memory_order::acquire);
	wait_for_activation();
}

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

	ui::initialize(*s_app.window);
	ui::set_theme(theme);

	ui::Options& options = ui::get_options();
	options.debug_layout_overflow = true;

	s_app.state = AppState::Sleeping;
}

void initialize_search_entries() {
	PROFILE_FUNCTION();

	std::vector<std::filesystem::path> known_folders = get_user_folders(
			UserFolderKind::Desktop | UserFolderKind::StartMenu | UserFolderKind::Programs);

	for (const auto& known_folder : known_folders) {
		try {
			walk_directory(known_folder, s_app.entries);
		} catch (std::exception e) {
		}
	}

	load_application_icons(s_app.entries, s_app.app_icon_storage, s_app.arena);
}

void run_app_frame() {
	PROFILE_FUNCTION();

	bool enter_pressed = false;

	const ui::Theme& theme = ui::get_theme();
	ui::Options& options = ui::get_options();

	Span<const WindowEvent> events = window_get_events(s_app.window);
	for (size_t i = 0; i < events.count; i++) {
		switch (events[i].kind) {
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
				case KeyCode::F3:
					options.debug_layout = !options.debug_layout;
					break;
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
	float item_height = compute_result_entry_height();
	float item_spacing = theme.default_layout_config.item_spacing;

	float item_count = (available_height + item_spacing) / (item_height + item_spacing);
	s_app.result_view_state.fully_visible_item_count = std::floor(item_count);
	uint32_t partially_visible_item_count = (uint32_t)std::ceil(item_count);

	update_result_view_scroll(s_app.result_view_state);

	uint32_t visible_item_count = std::min(
			partially_visible_item_count,
			(uint32_t)s_app.result_view_state.matches.size() - s_app.result_view_state.scroll_offset);

	auto& result_view_state = s_app.result_view_state;

	for (uint32_t i = result_view_state.scroll_offset; i < result_view_state.scroll_offset + visible_item_count; i++) {
		bool is_selected = i == result_view_state.selected_index;

		const ResultEntry& match = s_app.result_view_state.matches[i];
		const Entry& entry = s_app.entries[match.entry_index];

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
			run_file(entry.path, false);
			s_app.state = AppState::Sleeping;
			break;
		case EntryAction::LaunchAsAdmin:
			run_file(entry.path, true);
			s_app.state = AppState::Sleeping;
			break;
		case EntryAction::CopyPath:
			break;
		}
	}

	ui::end_vertical_layout();

	ui::end_frame();
	end_frame();

	window_swap_buffers(s_app.window);
}

int main()
{
	query_system_memory_spec();

	s_app.arena = {};
	s_app.arena.capacity = mb_to_bytes(8);

	log_init("log.txt", true);
	log_init_thread(s_app.arena, "main");

	log_info("logger started");

	initialize_platform();

	init_keyboard_hook(s_app.arena);

	initialize_app();
	initialize_search_entries();

	update_search_result({}, s_app.entries, s_app.result_view_state.matches, s_app.result_view_state.highlights, s_app.arena);

	window_hide(s_app.window);

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
			window_poll_events(s_app.window);
			run_app_frame();
			PROFILE_END_FRAME("Main");
			break;
		case AppState::Sleeping:
			window_hide(s_app.window);
			enter_sleep_mode();

			window_show(s_app.window);
			window_focus(s_app.window);
			break;
		}
	}

	shutdown_keyboard_hook();

	delete_texture(s_app.app_icon_storage.texture);
	delete_texture(s_app.icons.texture);
	delete_font(s_app.font);

	shutdown_renderer();
	window_destroy(s_app.window);
	shutdown_platform();

	log_shutdown_thread();
	log_shutdown();

	arena_release(s_app.arena);

	return 0;
}
