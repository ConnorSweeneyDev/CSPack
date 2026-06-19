#!/bin/bash

current_dir=$(pwd)
cat > compile_commands.json << EOF
[
  {
    "directory": "$current_dir",
    "file": "$current_dir/csp.hpp",
    "command": "clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow-all -Wundef -Wdeprecated -Wtype-limits -Wcast-qual -Wcast-align -Wfloat-equal -Wunreachable-code-aggressive -Wformat=2 -D__linux__"
  }
]
EOF
