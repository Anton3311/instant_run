#include "log.h"

#include "core.h"

#include <stdio.h>
#include <iostream>
#include <fstream>

struct LoggerState {
	std::wofstream file;
	bool is_initialized;
	bool output_to_stdout;
};

struct LoggerThreadState {
	Arena* arena;
	bool is_initialized;
	std::wstring_view name;
};

static LoggerState s_logger;
thread_local LoggerThreadState t_logger_thread;

bool log_init(const std::filesystem::path& log_file_path, bool output_to_stdout) {
	PROFILE_FUNCTION();

	s_logger.output_to_stdout = output_to_stdout;
	s_logger.is_initialized = true;

	s_logger.file = std::wofstream(log_file_path);
	if (!s_logger.file.is_open()) {
		return false;
	}

	return true;
}

bool log_init_thread(Arena& arena, std::string_view thread_name) {
	assert(s_logger.is_initialized);
	assert(!t_logger_thread.is_initialized);

	t_logger_thread.arena = &arena;
	t_logger_thread.name = string_to_wide(thread_name, *t_logger_thread.arena);
	t_logger_thread.is_initialized = true;
	return true;
}

Arena& log_get_fmt_arena() {
	return *t_logger_thread.arena;
}

void log_shutdown() {
	if (s_logger.file.is_open()) {
		s_logger.file.close();
	}

	s_logger.is_initialized = false;
}

void log_shutdown_thread() {
	assert(s_logger.is_initialized);
	assert(t_logger_thread.is_initialized);

	t_logger_thread = {};
}

void log_message(std::wstring_view message, MessageType message_type) {
	PROFILE_FUNCTION();

	assert(s_logger.is_initialized && t_logger_thread.is_initialized);

	if (message.length() == 0) {
		return;
	}

	if (!s_logger.file.is_open() && !s_logger.output_to_stdout) {
		return;
	}

	if (message.data()[message.length() - 1] == L'\n') {
		message = message.substr(0, message.length() - 1);
	}

	ArenaSavePoint temp = arena_begin_temp(*t_logger_thread.arena);
	StringBuilder<wchar_t> builder  = StringBuilder<wchar_t> { .arena = t_logger_thread.arena };

	str_builder_append<wchar_t>(builder, t_logger_thread.name);
	str_builder_append<wchar_t>(builder, L" ");

	const wchar_t* color_code = nullptr;

	switch (message_type) {
	case MessageType::Info:
		str_builder_append<wchar_t>(builder, L"[info] ");
		break;
	case MessageType::Error:
		color_code = L"\033[1;31m";
		str_builder_append<wchar_t>(builder, L"[error] ");
		break;
	case MessageType::Warn:
		color_code = L"\033[1;35m";
		str_builder_append<wchar_t>(builder, L"[warn] ");
		break;
	}

	str_builder_append<wchar_t>(builder, message);
	str_builder_append<wchar_t>(builder, L'\n');

	std::wstring_view logged_message = std::wstring_view(builder.string, builder.length);

	s_logger.file << logged_message;

	if (s_logger.output_to_stdout) {
		if (color_code) {
			std::wcout << color_code << logged_message << L"\033[0m";
		} else {
			std::wcout << logged_message;
		}
	}

	arena_end_temp(temp);
}

void log_message(std::string_view message, MessageType message_type) {
	PROFILE_FUNCTION();

	assert(s_logger.is_initialized && t_logger_thread.is_initialized);

	if (message.length() == 0) {
		return;
	}

	if (!s_logger.file.is_open() && !s_logger.output_to_stdout) {
		return;
	}

	ArenaSavePoint temp = arena_begin_temp(*t_logger_thread.arena);

	std::wstring_view wide_message = string_to_wide(message, *t_logger_thread.arena);
	log_message(wide_message, message_type);

	arena_end_temp(temp);
}

