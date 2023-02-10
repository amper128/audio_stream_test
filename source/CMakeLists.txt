#
# sources
#

file(GLOB_RECURSE app_headers "include/*.h")
file(GLOB_RECURSE global_headers "${PROJECT_SOURCE_DIR}/include/*.h")

# capture and stream
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

#recevie and play
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

#latency measure
add_executable(latency_measure
	latency_measure.c
	${app_headers}
	${global_headers}
	)

target_include_directories(latency_measure
	PRIVATE
		include
	)

target_link_libraries(latency_measure
		-lpulse -lpulse-simple -lmp3lame
	)
