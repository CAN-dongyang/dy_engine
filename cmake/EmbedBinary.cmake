if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT and SYMBOL are required")
endif()

file(READ "${INPUT}" binary HEX)
if(binary STREQUAL "")
    message(FATAL_ERROR "Cannot embed empty file: ${INPUT}")
endif()

string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," bytes "${binary}")
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT}"
    "#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace dy::Graphics::Private\n{\ninline constexpr uint8_t ${SYMBOL}[] = {${bytes}};\ninline constexpr std::size_t ${SYMBOL}Size = sizeof(${SYMBOL});\n}\n")
