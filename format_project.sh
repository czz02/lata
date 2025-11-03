#!/bin/bash

echo "开始格式化项目代码..."

# 查找所有 C/C++ 文件
FILES=$(find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | grep -v -E "(build|third_party|vendor)")

# 统计文件数量
FILE_COUNT=$(echo "$FILES" | wc -l)
echo "找到 $FILE_COUNT 个文件需要格式化"

# 执行格式化
if [ -n "$FILES" ]; then
    echo "$FILES" | xargs clang-format -i
    echo "格式化完成"
else
    echo "没有找到需要格式化的文件"
fi
