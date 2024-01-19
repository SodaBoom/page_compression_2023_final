#!/bin/bash
# 指定文件路径
file_path="page_engine/dummy_engine.h"

# 读取文件内容
file_content=$(cat "$file_path")

# 使用grep命令提取THREAD_NUM的定义行
thread_num_line=$(echo "$file_content" | grep "#define THREAD_NUM")

# 使用正则表达式提取THREAD_NUM的值
thread_num=$(echo "$thread_num_line" | grep -oP '\d+')

# 输出THREAD_NUM的值
echo "$thread_num" > page_engine/THREADS

# 打包压缩包
rm -rf page_engine.zip && zip page_engine.zip -r page_engine