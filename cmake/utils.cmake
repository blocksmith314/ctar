
# src/core/version.h is the single source of truth for both the tool version
# and the on-disk archive format version. The numbers are read as plain text,
# so nothing else has to be kept in sync by hand.
function(ctar_extract_version)
    # Constant name prefixes used in version.h. Renaming the constants means
    # changing these two lines and nothing else.
    set(_tool_prefix   "kToolVersion")
    set(_format_prefix "kFileFormatVersion")

    set(_header "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/core/version.h")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR "ctar_extract_version: ${_header} not found")
    endif()

    file(READ "${_header}" _contents)


    string(REGEX REPLACE "//[^\n]*" "" _contents "${_contents}")
    # Prepend a newline so the word boundary used below also works for a
    # definition on the very first line of the file.
    set(_contents "\n${_contents}")

    foreach(_part Major Minor Patch)
        _ctar_version_part("${_header}" "${_contents}" "${_tool_prefix}"   "${_part}" "_tool_${_part}")
        _ctar_version_part("${_header}" "${_contents}" "${_format_prefix}" "${_part}" "_format_${_part}")
    endforeach()

    # PARENT_SCOPE only writes to the caller's scope, so the local values are
    # assembled first and pushed out afterwards.
    set(CTAR_VERSION_MAJOR         "${_tool_Major}"   PARENT_SCOPE)
    set(CTAR_VERSION_MINOR         "${_tool_Minor}"   PARENT_SCOPE)
    set(CTAR_VERSION_PATCH         "${_tool_Patch}"   PARENT_SCOPE)
    set(CTAR_VERSION               "${_tool_Major}.${_tool_Minor}.${_tool_Patch}" PARENT_SCOPE)

    set(CTAR_FORMAT_VERSION_MAJOR  "${_format_Major}" PARENT_SCOPE)
    set(CTAR_FORMAT_VERSION_MINOR  "${_format_Minor}" PARENT_SCOPE)
    set(CTAR_FORMAT_VERSION_PATCH  "${_format_Patch}" PARENT_SCOPE)
    set(CTAR_FORMAT_VERSION        "${_format_Major}.${_format_Minor}.${_format_Patch}" PARENT_SCOPE)
endfunction()

# Internal helper. Extracts one numeric component and returns it through
# <out_var> in the caller's scope.
function(_ctar_version_part _header _contents _prefix _part _out_var)
    # [^A-Za-z0-9_] emulates a word boundary, so that a longer name merely
    # ending in this one cannot match by accident.
    string(REGEX MATCHALL "[^A-Za-z0-9_]${_prefix}${_part}[ \t]*=[ \t]*([0-9]+)"
            _matches "${_contents}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR
                "ctar_extract_version: expected exactly one '${_prefix}${_part} = <number>' "
                "in ${_header}, found ${_count}")
    endif()

    string(REGEX REPLACE ".*=[ \t]*([0-9]+).*" "\\1" _value "${_matches}")

    # The constants are uint8_t, so anything above 255 would be truncated.
    if(_value GREATER 255)
        message(FATAL_ERROR
                "ctar_extract_version: ${_prefix}${_part} = ${_value} exceeds 255")
    endif()

    set(${_out_var} "${_value}" PARENT_SCOPE)
endfunction()