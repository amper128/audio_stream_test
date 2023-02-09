#
# sources
#

file(GLOB_RECURSE app_headers "include/*.h")
file(GLOB_RECURSE global_headers "${PROJECT_SOURCE_DIR}/include/*.h")

add_executable(audio_src
	capture.c
	${app_headers}
	${global_headers}
	)

target_include_directories(audio_src
	PRIVATE
		include
	)

target_link_libraries(audio_src
		-lpulse -lpulse-simple -lmp3lame
	)

add_executable(audio_play
	play.c
	${app_headers}
	${global_headers}
	)

target_include_directories(audio_play
	PRIVATE
		include
	)

target_link_libraries(audio_play
		-lpulse -lpulse-simple -lmp3lame
	)
