file(GLOB_RECURSE SOURCES "*.cpp" "*.c")
file(GLOB_RECURSE HEADERS "*.h" "*.hpp")

add_subdirectory(api)
add_subdirectory(backtester)
add_subdirectory(core)
add_subdirectory(risk)
add_subdirectory(strategy)
add_subdirectory(utils)

add_executable(Trader ${SOURCES} ${HEADERS})

target_link_libraries(Trader PRIVATE
        api
        backtester
        core
        risk
        strategy
        utils
)
add_dependencies(Trader generate_version)
target_include_directories(Trader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(Trader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)