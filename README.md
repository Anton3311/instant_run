![Instant Run](docs/screenshots/1.png)

A simple tool for launching installed programs and opening files. Currently **Windows only**.
Simply press `Alt + Space` from any other application or desktop, to launch the search window.

Doesn't search the whole system, rather only programs and files from `Program Files`, `Desktop` and `Start Menu`.

## Running

Running the `.exe` directly without any arguments, will launch the application with a global keyboard hook.

Command line options:
- `--no-hook` to launch without a keyboard hook.

## Building

`clang++` is needed for building.

Running the `scripts/build.bat` without any arguments builds the application in debug configuration. For release or profiling builds run with `release` or `profiling` as the first argument.

The keyboard hook is built separately by running `scripts/build_hook.bat`. Run `scripts/build_hook.bat release` to build the release configuration.

Also the vendor libraries are built separately by running `scripts/build_vendor.bat` or `scripts/build_vendor.bat release`.

## Profiling

The project relies on Tracy for profiling. Tracy is not required for running the `profiling` build, however it is needed to view the profiling data.
