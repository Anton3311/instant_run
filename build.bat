clang++ ^
	src\main.cpp ^
	src\platform.cpp ^
	src\renderer.cpp ^
	src\ui.cpp ^
	-Ivendor\GLAD\include ^
	-Ivendor\stb_truetype ^
	-o bin\debug_win_x86_64\instant_run.exe ^
	bin_int\debug_win_x86_64\glad.o ^
	bin_int\debug_win_x86_64\stb_truetype.o ^
	-std=c++20 -m64 -lgdi32 -lopengl32 -luser32 -ldwmapi -g -DUNICODE -D_UNICODE
