cmake_minimum_required(VERSION 3.20)
project(dy_engine CXX)

set(DY_BACKEND "Null" CACHE STRING "Graphics backend: Null, D3D12, Vulkan, or Metal")
set_property(CACHE DY_BACKEND PROPERTY STRINGS Null D3D12 Vulkan Metal)
string(TOUPPER "${DY_BACKEND}" DY_BACKEND_NORMALIZED)
if(NOT DY_BACKEND_NORMALIZED MATCHES "^(NULL|D3D12|VULKAN|METAL)$")
	message(FATAL_ERROR "DY_BACKEND must be Null, D3D12, Vulkan, or Metal")
endif()

# add module path
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

if(DY_BACKEND_NORMALIZED STREQUAL "METAL" AND APPLE)
	enable_language(OBJCXX)
endif()

# main target
add_library(${PROJECT_NAME} STATIC
	"src/RHI/IDevice.cpp"
	"src/Graphics/Renderer.cpp"
	"src/Graphics/Private/DrawData.cpp"
	"src/Graphics/Private/StockShaderAssets.cpp"
	"src/Graphics/Private/TextureCache.cpp"
	"src/Graphics/Mesh.cpp"
	"src/Graphics/Model.cpp"
	"src/Platform/Window.cpp"
)

target_include_directories(${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/src")

# global options
include(Engine_Options)
target_link_libraries(${PROJECT_NAME} PUBLIC dy_engine_options)

include(Dependencies)
include(BuildOptions)
include(ShaderAssets)

# Only add the executable if this is the main project being built (not when included as a subdirectory in another project)
if(CMAKE_PROJECT_NAME  STREQUAL PROJECT_NAME)
	add_subdirectory(examples)
endif()

# ===== Configuration Messages =====
if(WIN32)
    message(STATUS "Windows 환경 감지됨!")
elseif(APPLE)
    message(STATUS "macOS 환경 감지됨!")
elseif(UNIX)
    message(STATUS "Linux 환경 감지됨!")
endif()
