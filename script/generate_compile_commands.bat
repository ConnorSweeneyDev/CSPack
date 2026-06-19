@echo off

set "CURRENT_DIR=%CD%"
(
echo [
echo   {
echo     "directory": "%CURRENT_DIR:\=\\%",
echo     "file": "%CURRENT_DIR:\=\\%\\csp.hpp",
echo     "command": "clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow-all -Wundef -Wdeprecated -Wtype-limits -Wcast-qual -Wcast-align -Wfloat-equal -Wunreachable-code-aggressive -Wformat=2 -D_WIN32"
echo   }
echo ]
) > compile_commands.json
