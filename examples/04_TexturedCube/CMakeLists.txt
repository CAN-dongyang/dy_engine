set(EXAMPLE_NAME "TexturedCube")

add_executable(${EXAMPLE_NAME} main.cpp)

dy_setup_example(${EXAMPLE_NAME})

add_custom_command(TARGET ${EXAMPLE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Textures"
        "$<TARGET_FILE_DIR:${EXAMPLE_NAME}>/Textures"
    VERBATIM)
