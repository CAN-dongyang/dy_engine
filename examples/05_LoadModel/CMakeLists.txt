set(EXAMPLE_NAME "LoadModel")

add_executable(${EXAMPLE_NAME}
    main.cpp
    LoadModelOptions.cpp
    LoadModelOptions.h
    LoadModelPlayback.cpp
    LoadModelPlayback.h
    Shaders/mesh_vs.glsl
    Shaders/mesh_ps.glsl
    Shaders/mesh_shadow_vs.glsl
	Shaders/mesh_skinning_cs.glsl
    Shaders/mesh_vs.hlsl
    Shaders/mesh_ps.hlsl
)

set_source_files_properties(
    Shaders/mesh_vs.glsl
    Shaders/mesh_ps.glsl
    Shaders/mesh_shadow_vs.glsl
	Shaders/mesh_skinning_cs.glsl
    Shaders/mesh_vs.hlsl
    Shaders/mesh_ps.hlsl
    PROPERTIES HEADER_FILE_ONLY TRUE
)

dy_setup_example(${EXAMPLE_NAME})

add_executable(SimpleFox
    SimpleFox.cpp
    LoadModelOptions.cpp
    LoadModelOptions.h
    Shaders/mesh_vs.glsl
    Shaders/mesh_ps.glsl
    Shaders/mesh_shadow_vs.glsl
	Shaders/mesh_skinning_cs.glsl
    Shaders/mesh_vs.hlsl
    Shaders/mesh_ps.hlsl
)

dy_setup_example(SimpleFox)

add_executable(ExistingFox
    ExistingFox.cpp
    LoadModelOptions.cpp
    LoadModelOptions.h
    Shaders/mesh_vs.glsl
    Shaders/mesh_ps.glsl
    Shaders/mesh_shadow_vs.glsl
	Shaders/mesh_skinning_cs.glsl
    Shaders/mesh_vs.hlsl
    Shaders/mesh_ps.hlsl
)

dy_setup_example(ExistingFox)

foreach(target IN ITEMS SimpleFox ExistingFox)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${target}/$<CONFIG>")
	if(MSVC)
		target_compile_options(${target} PRIVATE /EHsc)
	endif()
endforeach()

function(copy_fox_model target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Models/Fox.glb"
            "$<TARGET_FILE_DIR:${target}>/Models/Fox.glb"
        VERBATIM)
endfunction()

copy_fox_model(SimpleFox)
copy_fox_model(ExistingFox)

add_custom_command(TARGET ${EXAMPLE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Models"
        "$<TARGET_FILE_DIR:${EXAMPLE_NAME}>/Models"
    VERBATIM)
