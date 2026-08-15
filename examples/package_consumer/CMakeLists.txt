cmake_minimum_required(VERSION 3.29)

project(vinox_package_consumer LANGUAGES CXX)

find_package(vinox 0.1 CONFIG REQUIRED)

add_executable(vinox_package_consumer main.cpp)
target_compile_features(vinox_package_consumer PRIVATE cxx_std_20)
target_link_libraries(vinox_package_consumer PRIVATE vinox::core vinox::openvino)
