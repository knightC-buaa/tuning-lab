# 实验清单：Task 3 起逐行添加 add_experiment(<id>)。
# 每次重新配置时清空索引，避免重复追加。
file(WRITE "${CMAKE_BINARY_DIR}/exp_index.txt" "")
