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

typedef struct {
	uint32_t magic;
	uint32_t uid;
	uint16_t packet_len;
	uint8_t codec_type : 4;
	uint8_t channels : 4;
	uint8_t format;
	uint32_t rate;
} packet_header_t;
