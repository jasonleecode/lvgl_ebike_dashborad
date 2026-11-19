cmake_minimum_required(VERSION 3.10)
project(ebike_map_tools C)
set(garmin "${CMAKE_CURRENT_SOURCE_DIR}/../../app/libgarmin/src")
file(GLOB sources "${garmin}/*.c")
add_executable(ebike_map_export export.c features.c pack.c ${sources})
target_include_directories(ebike_map_export PRIVATE "${garmin}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ebike_map_export PRIVATE DEBUG)
target_link_libraries(ebike_map_export PRIVATE m)
set(map_file "${CMAKE_CURRENT_SOURCE_DIR}/../../data/Beijing_gmapsupp.img")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/beijing.map"
    COMMAND ebike_map_export "${map_file}" "${CMAKE_CURRENT_BINARY_DIR}/beijing.tmp"
    COMMAND ${CMAKE_COMMAND} -E rename "${CMAKE_CURRENT_BINARY_DIR}/beijing.tmp" "${CMAKE_CURRENT_BINARY_DIR}/beijing.map"
    DEPENDS ebike_map_export "${map_file}" VERBATIM)
add_custom_target(ebike_map_data DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/beijing.map")
