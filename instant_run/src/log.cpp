#include "log.h"

#include "core.h"

#include <stdio.h>
#include <iostream>
#include <fstream>

struct LoggerState {
	std::wofstream file;
	bool output_to_stdout;
};

struct LoggerThreadState {
	Arena* arena;
	std::wstring_view name;
};

static LoggerState s_logger;
thread_local LoggerThreadState t_logger_thread;

bool log_init(const char* log_file_path, bool output_to_stdout) {
	PROFILE_FUNCTION();

	s_logger.output_to_stdout = output_to_stdout;

	s_logger.file = std::wofstream(log_file_path);
	if (!s_logger.file.is_open()) {
		return false;
	}

	return true;
}

bool log_init_thread(Arena& arena, std::string_view thread_name) {
	t_logger_thread.arena = &arena;
	t_logger_thread.name = string_to_wide(thread_name, *t_logger_thread.arena);
	return true;
}

Arena& log_get_fmt_arena() {
	return *t_logger_thread.arena;
}

void log_shutdown() {
	if (s_logger.file.is_open()) {
		s_logger.file.close();
	}
}

void log_shutdown_thread() {
	t_logger_thread = {};
}

void log_message(std::wstring_view message, MessageType message_type) {
	PROFILE_FUNCTION();
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

	switch (message_type) {
	case MessageType::Info:
		str_builder_append<wchar_t>(builder, L"[info] ");
		break;
	case MessageType::Error:
		str_builder_append<wchar_t>(builder, L"[error] ");
		break;
	case MessageType::Warn:
		str_builder_append<wchar_t>(builder, L"[warn] ");
		break;
	}

	str_builder_append<wchar_t>(builder, message);
	str_builder_append<wchar_t>(builder, L'\n');

	std::wstring_view logged_message = std::wstring_view(builder.string, builder.length);

	s_logger.file << logged_message;

	if (s_logger.output_to_stdout) {
		std::wcout << logged_message;
	}

	arena_end_temp(temp);
}

void log_message(std::string_view message, MessageType message_type) {
	if (!s_logger.file.is_open() && !s_logger.output_to_stdout) {
		return;
	}

	ArenaSavePoint temp = arena_begin_temp(*t_logger_thread.arena);

	std::wstring_view wide_message = string_to_wide(message, *t_logger_thread.arena);
	log_message(wide_message, message_type);

	arena_end_temp(temp);
}

