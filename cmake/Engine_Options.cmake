# abstract target for engine options
add_library(dy_engine_options INTERFACE)

# ===== ===== Language ===== =====
target_compile_features(dy_engine_options INTERFACE cxx_std_17)

# dy_engine 소비자와 독립 object Backend가 공유하는 유일한 공개 include root.
target_include_directories(dy_engine_options INTERFACE
	${PROJECT_SOURCE_DIR}/src/Public
)

# ===== ===== SIMD ===== =====
option(DY_ENABLE_SIMD "Enable dy::Math SIMD code paths when supported by the target CPU." ON)

if(DY_ENABLE_SIMD)
	target_compile_definitions(dy_engine_options INTERFACE DY_SIMD_ENABLED=1)

	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86|i[3-6]86)$")
		if(MSVC)
			target_compile_options(dy_engine_options INTERFACE /arch:SSE2)
		elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
			target_compile_options(dy_engine_options INTERFACE -msse2)
		endif()
	endif()
endif()

# ===== ===== Compile options ===== =====
if(MSVC)
	# Public headers are UTF-8 without BOM, so their input encoding is part of the consumer contract.
	target_compile_options(dy_engine_options INTERFACE /utf-8)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
