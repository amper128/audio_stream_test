#
# sources
#

find_package(PkgConfig)
pkg_search_module(ALSA REQUIRED alsa)
pkg_search_module(LAME REQUIRED lame)

file(GLOB_RECURSE app_headers "include/*.h")
file(GLOB_RECURSE global_headers "${PROJECT_SOURCE_DIR}/include/*.h")

add_executable(audio_src
	alsa_src.c
	${app_headers}
	${global_headers}
	)

target_include_directories(audio_src
	PRIVATE
		include
		${ALSA_INCLUDE_DIRS}
		${LAME_INCLUDE_DIRS}
	)

target_link_libraries(audio_src
		${ALSA_LIBRARIES}
		${LAME_LIBRARIES}
		-lpulse -lpulse-simple
	)
