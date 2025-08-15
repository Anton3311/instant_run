set CMD_ARGS=-m64
set PLATFORM=win
set ARCH=x86_64

if [%1] == [release] (
	set CMD_ARGS=%CMD_ARGS% -O3
	set BIN_DIR=bin\release_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\release_%PLATFORM%_%ARCH%

	set FILES=%FILES% vendor\Tracy\TracyClient.cpp
) else (
	set BIN_DIR=bin\debug_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\debug_%PLATFORM%_%ARCH%
)

clang -Ivendor\GLAD\include -c vendor\GLAD\glad.c -o %BIN_INT_DIR%\glad.o -DUNICODE -D_UNICODE %CMD_ARGS%
clang -c vendor\stb_truetype\stb_truetype.c -o %BIN_INT_DIR%\stb_truetype.o -g -DUNICODE -D_UNICODE %CMD_ARGS%
clang -c vendor\stb_image\stb_image.c -o %BIN_INT_DIR%\stb_image.o -g -DUNICODE -D_UNICODE %CMD_ARGS%

