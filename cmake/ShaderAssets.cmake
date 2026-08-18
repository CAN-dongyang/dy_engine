set(DY_STOCK_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Private/Shaders")
set(DY_STOCK_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/stock_shaders")
set(DY_STOCK_SHADER_EMBED_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake")

set(DY_STOCK_SHADER_LAYOUT "${DY_STOCK_SHADER_SOURCE_DIR}/StockShaderLayout.inc")
set(DY_STOCK_SHADER_HEADERS)

function(dy_embed_stock_shader source stage name symbol)
    set(binary "${DY_STOCK_SHADER_GENERATED_DIR}/${name}.bin")
    set(shader_defines ${ARGN})

    if(DY_BACKEND_NORMALIZED STREQUAL "D3D12")
        if(NOT DY_DXC)
            find_program(DY_DXC NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
            if(NOT DY_DXC)
                message(FATAL_ERROR "dxc is required to build the D3D12 stock shaders")
            endif()
        endif()
        if(stage STREQUAL "vert")
            set(profile vs_6_0)
        else()
            set(profile ps_6_0)
        endif()
        add_custom_command(
            OUTPUT "${binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
            COMMAND "${DY_DXC}" -T "${profile}" -E main ${shader_defines} -I "${DY_STOCK_SHADER_SOURCE_DIR}" -Fo "${binary}" "${source}"
            DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
            VERBATIM)
    elseif(DY_BACKEND_NORMALIZED STREQUAL "VULKAN")
        if(NOT DY_GLSLC)
            find_program(DY_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
            if(NOT DY_GLSLC)
                message(FATAL_ERROR "glslc is required to build the Vulkan stock shaders")
            endif()
        endif()
        add_custom_command(
            OUTPUT "${binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
            COMMAND "${DY_GLSLC}" "-fshader-stage=${stage}" ${shader_defines} -I "${DY_STOCK_SHADER_SOURCE_DIR}" "${source}" -o "${binary}"
            DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
            VERBATIM)
    else()
        add_custom_command(
            OUTPUT "${binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${binary}"
            DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
            VERBATIM)
    endif()

    set(header "${DY_STOCK_SHADER_GENERATED_DIR}/${name}.h")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${binary}" "-DOUTPUT=${header}" "-DSYMBOL=${symbol}" -P "${DY_STOCK_SHADER_EMBED_SCRIPT}"
        DEPENDS "${binary}" "${DY_STOCK_SHADER_EMBED_SCRIPT}"
        VERBATIM)
    set(DY_STOCK_SHADER_HEADERS "${DY_STOCK_SHADER_HEADERS};${header}" PARENT_SCOPE)
endfunction()

if(DY_BACKEND_NORMALIZED STREQUAL "METAL")
    find_program(DY_XCRUN NAMES xcrun)
    if(NOT DY_XCRUN)
        message(FATAL_ERROR "xcrun is required to build the Metal stock shaders")
    endif()

    set(metal_air_files)
    set(source "${DY_STOCK_SHADER_SOURCE_DIR}/mesh_shadow_vs.metal")
    set(air "${DY_STOCK_SHADER_GENERATED_DIR}/mesh_shadow_vs.air")
    add_custom_command(
        OUTPUT "${air}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
        COMMAND "${DY_XCRUN}" -sdk macosx metal -I "${DY_STOCK_SHADER_SOURCE_DIR}" -c "${source}" -o "${air}"
        DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
        VERBATIM)
    list(APPEND metal_air_files "${air}")

    foreach(enable_shadows IN ITEMS 1 0)
        if(enable_shadows)
            set(shader mesh_vs)
            set(vertex_entry vertexShader)
        else()
            set(shader mesh_vs_no_shadows)
            set(vertex_entry vertexShaderNoShadows)
        endif()
        set(source "${DY_STOCK_SHADER_SOURCE_DIR}/mesh_vs.metal")
        set(air "${DY_STOCK_SHADER_GENERATED_DIR}/${shader}.air")
        add_custom_command(
            OUTPUT "${air}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
            COMMAND "${DY_XCRUN}" -sdk macosx metal -I "${DY_STOCK_SHADER_SOURCE_DIR}"
                "-DRENDERER_ENABLE_SHADOWS=${enable_shadows}"
                "-DRENDERER_VERTEX_ENTRY=${vertex_entry}"
                -c "${source}" -o "${air}"
            DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
            VERBATIM)
        list(APPEND metal_air_files "${air}")
    endforeach()

    foreach(enable_shadows IN ITEMS 1 0)
        if(enable_shadows)
            set(shader mesh_ps)
            set(fragment_entry fragmentShader)
        else()
            set(shader mesh_ps_no_shadows)
            set(fragment_entry fragmentShaderNoShadows)
        endif()
        set(source "${DY_STOCK_SHADER_SOURCE_DIR}/mesh_ps.metal")
        set(air "${DY_STOCK_SHADER_GENERATED_DIR}/${shader}.air")
        add_custom_command(
            OUTPUT "${air}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${DY_STOCK_SHADER_GENERATED_DIR}"
            COMMAND "${DY_XCRUN}" -sdk macosx metal -I "${DY_STOCK_SHADER_SOURCE_DIR}"
                "-DRENDERER_ENABLE_SHADOWS=${enable_shadows}"
                "-DRENDERER_FRAGMENT_ENTRY=${fragment_entry}"
                -c "${source}" -o "${air}"
            DEPENDS "${source}" "${DY_STOCK_SHADER_LAYOUT}"
            VERBATIM)
        list(APPEND metal_air_files "${air}")
    endforeach()

    set(metal_library "${DY_STOCK_SHADER_GENERATED_DIR}/StockShaders.metallib")
    add_custom_command(
        OUTPUT "${metal_library}"
        COMMAND "${DY_XCRUN}" -sdk macosx metallib ${metal_air_files} -o "${metal_library}"
        DEPENDS ${metal_air_files}
        VERBATIM)
    set(metal_header "${DY_STOCK_SHADER_GENERATED_DIR}/StockMetalLibrary.h")
    add_custom_command(
        OUTPUT "${metal_header}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${metal_library}" "-DOUTPUT=${metal_header}" -DSYMBOL=kStockMetalLibrary -P "${DY_STOCK_SHADER_EMBED_SCRIPT}"
        DEPENDS "${metal_library}" "${DY_STOCK_SHADER_EMBED_SCRIPT}"
        VERBATIM)
    list(APPEND DY_STOCK_SHADER_HEADERS "${metal_header}")
else()
    if(DY_BACKEND_NORMALIZED STREQUAL "D3D12")
        set(shader_extension hlsl)
    else()
        set(shader_extension glsl)
    endif()
    dy_embed_stock_shader("${DY_STOCK_SHADER_SOURCE_DIR}/mesh_vs.${shader_extension}" vert StockVertexShader kStockVertexShader -DRENDERER_ENABLE_SHADOWS=1)
    dy_embed_stock_shader("${DY_STOCK_SHADER_SOURCE_DIR}/mesh_vs.${shader_extension}" vert StockVertexShaderNoShadows kStockVertexShaderNoShadows -DRENDERER_ENABLE_SHADOWS=0)
    dy_embed_stock_shader("${DY_STOCK_SHADER_SOURCE_DIR}/mesh_ps.${shader_extension}" frag StockFragmentShader kStockFragmentShader -DRENDERER_ENABLE_SHADOWS=1)
    dy_embed_stock_shader("${DY_STOCK_SHADER_SOURCE_DIR}/mesh_ps.${shader_extension}" frag StockFragmentShaderNoShadows kStockFragmentShaderNoShadows -DRENDERER_ENABLE_SHADOWS=0)
    dy_embed_stock_shader("${DY_STOCK_SHADER_SOURCE_DIR}/mesh_shadow_vs.${shader_extension}" vert StockShadowVertexShader kStockShadowVertexShader)
endif()

add_custom_target(dy_stock_shaders DEPENDS ${DY_STOCK_SHADER_HEADERS})
add_dependencies(${PROJECT_NAME} dy_stock_shaders)
target_include_directories(${PROJECT_NAME} PRIVATE "${DY_STOCK_SHADER_GENERATED_DIR}")
