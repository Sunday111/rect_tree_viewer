cmake_minimum_required(VERSION 3.20)
include(set_compiler_options)
set(module_source_files
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/open_file_dialog.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/open_file_dialog_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/path_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/path_helpers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/read_directory_tree.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/rect_tree_draw_data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/rect_tree_draw_data.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/rect_tree_viewer_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/rect_tree_viewer_app.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/rect_tree_viewer_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/code/private/tree.hpp)
add_executable(rect_tree_viewer ${module_source_files})
set_generic_compiler_options(rect_tree_viewer PRIVATE)
target_link_libraries(rect_tree_viewer PRIVATE klgl)
target_include_directories(rect_tree_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/code/public)
target_include_directories(rect_tree_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/code/private)
