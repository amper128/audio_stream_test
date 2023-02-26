/**
 * @file pulse_api.h
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Заголовки функций работы с PulseAudio
 */

#pragma once

#include <stdint.h>

#include <pulse/pulseaudio.h>

typedef struct {
	const char *server;
	const char *name;
	const char *stream_name;
	uint32_t sample_rate;
	uint8_t channels;
	uint8_t __pad[3];

	int frame_size;
	int fragment_size;

	pa_threaded_mainloop *mainloop;
	pa_context *context;
	pa_stream *stream;
} pulse_t;

int pulse_open(pulse_t *pd, uint32_t rate, pa_sample_format_t format,
	       uint8_t channels, const char *name);

int pulse_close(pulse_t *pd);

int pulse_read_packet(pulse_t *pd, const void **data, size_t *size);
