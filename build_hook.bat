set CMD_ARGS=-m64 -shared
set PLATFORM=win
set ARCH=x86_64
set APP_NAME=instant_run
set SRC=instant_run_hook\src

set FILES=%SRC%\hook_main.cpp

if [%1] == [release] (
	set CMD_ARGS=%CMD_ARGS% -DTRACY_ENABLE -DENABLE_PROFILING -O3
	set BIN_DIR=bin\release_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\release_%PLATFORM%_%ARCH%
	
	:: Tracy is disabled
	:: set FILES=%FILES% vendor\Tracy\TracyClient.cpp
) else (
	set BIN_DIR=bin\debug_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\debug_%PLATFORM%_%ARCH%
)

if not exist %BIN_DIR% mkdir %BIN_DIR%
if not exist %BIN_INT_DIR% mkdir %BIN_INT_DIR%

clang++ ^
	%FILES% ^
	-std=c++20 -g -DUNICODE -D_UNICODE %CMD_ARGS% ^
	-lUser32.lib ^
	-o %BIN_DIR%\%APP_NAME%.dll
