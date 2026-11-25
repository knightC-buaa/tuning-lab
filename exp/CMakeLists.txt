# 实验清单：Task 3 起逐行添加 add_experiment(<id>)。
# 每次重新配置时清空索引，避免重复追加。
file(WRITE "${CMAKE_BINARY_DIR}/exp_index.txt" "")

add_experiment(2.1.1_hash_vs_scan)
add_experiment(4.1.1a_async_overlap)
add_experiment(4.1.1b_coroutine_vs_thread)
add_experiment(4.2.1_zero_copy)
