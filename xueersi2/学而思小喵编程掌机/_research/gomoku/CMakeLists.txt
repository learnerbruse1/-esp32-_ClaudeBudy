# XRS Gomoku (五子棋) — main component
# C/C++ 混合编译，所有依赖必须在 REQUIRES 中显式声明
# 测试文件在 test/ 子目录，通过 TEST_MODE 宏控制

idf_component_register(
    SRCS "alfabeta.cpp" "ai_bridge.cpp" "ipc.cpp" "ui.cpp" "input.cpp" "audio.cpp" "main.c" "board.cpp" "evaluate.cpp" "game.cpp" "test/test_board.cpp" "test/test_evaluate.cpp" "test/test_alfabeta.cpp" "test/test_game.cpp" "test/test_ai_bridge.cpp" "test/test_ipc.cpp" "test/test_input_audio.cpp"
    INCLUDE_DIRS "." "test"
    REQUIRES
        esp_lvgl_port
        esp_timer
        esp_lcd
        driver
        heap
)
