cmake_minimum_required(VERSION 3.20)
project(dy_engine CXX)

# add module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(USE_METAL AND APPLE)
	enable_language(OBJCXX)
endif()

# main target
add_library(${PROJECT_NAME} STATIC
	"src/RHI/IDevice.cpp"
	"src/Graphics/Renderer.cpp"
	"src/Graphics/RenderPath.cpp"
	"src/Graphics/ProfilerHud.cpp"
	"src/Graphics/GpuScene.cpp"
	"src/Graphics/ImageFile.cpp"
	"src/Graphics/Mesh.cpp"
	"src/Graphics/ShadowMath.cpp"
	"src/Platform/Window.cpp"
)

# global options
include(Engine_Options)
target_link_libraries(${PROJECT_NAME} PUBLIC Engine_Options)

include(Dependencies)
include(BuildOptions)

# Only add the executable if this is the main project being built (not when included as a subdirectory in another project)
if(CMAKE_PROJECT_NAME  STREQUAL PROJECT_NAME)
	add_subdirectory(examples)
endif()

# Null backend contract test: validates live/created/destroyed counts and that
# null or repeated destroy requests cannot underflow or double-delete objects.
include(CTest)
if(BUILD_TESTING AND NOT USE_D3D12 AND NOT USE_METAL AND NOT USE_VULKAN)
	add_executable(ResourceAllocationCountersTest
		"tests/ResourceAllocationCountersTest.cpp"
	)
	target_link_libraries(ResourceAllocationCountersTest PRIVATE ${PROJECT_NAME} Backend_Null)
	target_include_directories(ResourceAllocationCountersTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
	add_test(NAME ResourceAllocationCounters COMMAND ResourceAllocationCountersTest)
endif()

# ===== Configuration Messages =====
if(WIN32)
    message(STATUS "Windows 환경 감지됨!")
elseif(APPLE)
    message(STATUS "macOS 환경 감지됨!")
elseif(UNIX)
    message(STATUS "Linux 환경 감지됨!")
endif()
