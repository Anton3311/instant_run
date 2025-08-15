struct CommandLineArgs {
	wchar_t** arguments;
	size_t count;
};

int run_app(CommandLineArgs cmd_args);
