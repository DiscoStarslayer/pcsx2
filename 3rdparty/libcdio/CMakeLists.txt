set(LIBCDIO_VERSION "2.4.1dev0")
set(LIBCDIO_VERSION_NUM 20401)
set(VERSION "${LIBCDIO_VERSION}")
set(build "${CMAKE_SYSTEM_NAME}")

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/include/cdio/version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/include/cdio/version.h"
	@ONLY
)

file(GLOB_RECURSE libcdio_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/lib/driver/*.c"
)

add_library(libcdio STATIC ${libcdio_sources})
add_library(libcdio::libcdio ALIAS libcdio)

target_compile_features(libcdio PRIVATE c_std_11)
target_include_directories(libcdio
	PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${CMAKE_CURRENT_BINARY_DIR}/include"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/lib/driver"
		"${CMAKE_CURRENT_SOURCE_DIR}/lib/driver/mmc"
)

target_compile_definitions(libcdio PRIVATE
	HAVE_ERRNO_H=1
	HAVE_FCNTL_H=1
	HAVE_INTTYPES_H=1
	HAVE_LIMITS_H=1
	HAVE_SNPRINTF=1
	HAVE_STDARG_H=1
	HAVE_STDBOOL_H=1
	HAVE_STDINT_H=1
	HAVE_STDIO_H=1
	HAVE_STDLIB_H=1
	HAVE_STRING_H=1
	HAVE_SYS_STAT_H=1
	HAVE_SYS_TYPES_H=1
	HAVE_VSNPRINTF=1
)

if(WIN32)
	target_include_directories(libcdio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.vs")
	target_compile_definitions(libcdio PRIVATE HAVE_WINDOWS_H=1)
else()
	find_package(Iconv REQUIRED)
	target_link_libraries(libcdio PRIVATE Iconv::Iconv)
	target_compile_definitions(libcdio PRIVATE
		_GNU_SOURCE=1
		HAVE_GLOB_H=1
		HAVE_ICONV=1
		HAVE_LSTAT=1
		HAVE_REALPATH=1
		HAVE_S_ISLNK=1
		HAVE_S_ISSOCK=1
		HAVE_STRINGS_H=1
		HAVE_STRNDUP=1
		HAVE_STRTOK_R=1
		HAVE_UNISTD_H=1
		ICONV_CONST=
	)
endif()

set_target_properties(libcdio PROPERTIES POSITION_INDEPENDENT_CODE ON)

