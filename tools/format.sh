#!/usr/bin/env bash

set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format 未安装，请先安装后再执行。"
    exit 1
fi

mapfile -t files < <(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp')

if [ "${#files[@]}" -eq 0 ]; then
    echo "没有找到需要格式化的 C/C++ 源码文件。"
    exit 0
fi

clang-format -i "${files[@]}"
echo "已格式化 ${#files[@]} 个文件。"
