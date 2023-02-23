/**
 * @file streaming_shared.h
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Общие заголовки
 */

#pragma once

#include <stdint.h>

#define PACKET_MAGIC (0xAA704253)

typedef enum {
	CODEC_MP3, /* use lame */
	CODEC_OPUS /* use opus */
} codec_type_t;

typedef enum {
	PACKET_TYPE_START_STREAM = 0,
	PACKET_TYPE_STOP_STREAM,
	PACKED_TYPE_STREAM_DATA,
} packet_type_t;

typedef struct {
	uint32_t magic;
	uint8_t packet_type; /* aka packet_type_t */
	uint8_t __reserved;
	uint16_t packet_len;
	uint32_t uid;
} packet_header_t;

typedef struct {
	uint8_t codec_type;
	uint8_t format;
	uint8_t channels;
	uint8_t __reserved;
	uint32_t rate;
} stream_start_t;
