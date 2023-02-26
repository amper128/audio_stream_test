/**
 * @file capture.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Захват аудио
 */

#include <arpa/inet.h>
#include <getopt.h>
#include <lame/lame.h>
#include <opus/opus.h>
#include <pulse/simple.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <streaming_shared.h>

#include <pulse_api.h>

#define FRAMES_COUNT (480U)
#define NSTREAMS (64U)
#define NCHANNELS (2U)

#define MAX_PACKET_SIZE (1400U)
#define MAX_DATA_SIZE (MAX_PACKET_SIZE - sizeof(packet_header_t))

typedef struct {
	void *encoder_p;
	codec_type_t codec_type;
} encoder_desc_t;

static int do_capture = 1;
static uint32_t packet_id = 0U;

static void
sigfunc(int sig)
{
	if (sig == SIGINT) {
		do_capture = 0;
	}
}

static encoder_desc_t *
init_encoder(int rate, int kbitrate, codec_type_t codec)
{
	switch (codec) {
	case CODEC_MP3: {
		lame_t lame = lame_init();

		lame_set_in_samplerate(lame, rate);
		lame_set_VBR(lame, vbr_off);
		lame_set_brate(lame, kbitrate);
		lame_set_force_short_blocks(lame, 1);
		lame_init_params(lame);

		encoder_desc_t *result = malloc(sizeof(encoder_desc_t));
		result->codec_type = codec;
		result->encoder_p = lame;

		return result;
	}
	case CODEC_OPUS: {
		OpusEncoder *opus_encoder;
		int err = 0;
		opus_encoder = opus_encoder_create(
		    rate, NCHANNELS, OPUS_APPLICATION_AUDIO, &err);
		if (err < 0) {
			fprintf(stderr, "failed to create an encoder: %s\n",
				opus_strerror(err));
			exit(1);
		}

		err = opus_encoder_ctl(opus_encoder,
				       OPUS_SET_BITRATE(kbitrate * 1000));
		if (err < 0) {
			fprintf(stderr, "failed to set bitrate: %s\n",
				opus_strerror(err));
			exit(1);
		}

		encoder_desc_t *result = malloc(sizeof(encoder_desc_t));
		result->codec_type = codec;
		result->encoder_p = opus_encoder;

		return result;
	}

	default:
		fprintf(stderr, "Cannot create unknown codec\n");
		exit(1);
	}
}

static void
free_encoder(encoder_desc_t *encoder)
{
	switch (encoder->codec_type) {
	default:
	case CODEC_MP3: {
		lame_t lame = (lame_t)encoder->encoder_p;
		lame_close(lame);
	} break;

	case CODEC_OPUS: {
		OpusEncoder *opus = (OpusEncoder *)encoder->encoder_p;
		opus_encoder_destroy(opus);
	} break;
	}
	free(encoder);
}

static int
encode_frames(encoder_desc_t *encoder, void *buffer, size_t frames,
	      uint8_t *out, size_t out_max)
{
	int encoded;
	int16_t *buffer_i = buffer;

	switch (encoder->codec_type) {
	default:
	case CODEC_MP3: {
		lame_t lame = (lame_t)encoder->encoder_p;

		encoded = lame_encode_buffer_interleaved(
		    lame, buffer_i, (int)frames, out, (int)out_max);

	} break;

	case CODEC_OPUS: {
		OpusEncoder *opus = encoder->encoder_p;

		encoded = opus_encode(opus, buffer_i, (int)frames, out,
				      (opus_int32)out_max);
	} break;
	}

	return encoded;
}

int
main(int argc, char *argv[])
{
	char *addr = NULL;
	uint16_t port = 0U;
	int rate = 48000;
	int kbitrate = 320;
	codec_type_t codec = CODEC_OPUS;

	union {
		uint8_t u8[MAX_PACKET_SIZE];
		struct {
			packet_header_t hdr;
			uint8_t data[];
		} data;
	} packet;

	int c;
	while ((c = getopt(argc, argv, "s:p:r:b:")) != -1) {
		char *end;
		switch (c) {
		case 's':
			addr = optarg;
			break;

		case 'p':
			port = (uint16_t)strtoul(optarg, &end, 10);
			break;

		case 'r':
			rate = (int)strtol(optarg, &end, 10);
			break;

		case 'b':
			kbitrate = (int)strtol(optarg, &end, 10);
			break;

		default:
			return -1;
		}
	}

	if (port == 0) {
		return -1;
	}

	signal(SIGINT, sigfunc);

	pulse_t pulse;
	if (pulse_open(&pulse, (uint32_t)rate, PA_SAMPLE_S16LE, NCHANNELS,
		       "Test capture")) {
		return 1;
	}

	// pa_simple *rec = init_capture(rate, PA_SAMPLE_S16LE);
	// int err = 0;
	uint8_t *input_buffer;
	size_t frame_size = pa_sample_size_of_format(PA_SAMPLE_S16LE);
	size_t data_size = frame_size * FRAMES_COUNT * NCHANNELS;

	const size_t enc_size = 8192U;
	unsigned char *enc_buffer;

	input_buffer = malloc(data_size * 8);

	uint8_t *ring_buffer;
	size_t ring_head = 0U;
	size_t ring_tail = 0U;
	size_t ring_size = data_size * 8U;
	ring_buffer = malloc(data_size * 8);

	enc_buffer = malloc(enc_size * 8);

	encoder_desc_t *encoder = init_encoder(rate, kbitrate, codec);

	/* инициализируем UDP сокет */
	struct sockaddr_in si_other;
	int s, slen = sizeof(si_other);

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		fprintf(stderr, "cannot create socket\n");
		exit(1);
	}

	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(port);

	if (inet_aton(addr, &si_other.sin_addr) == 0) {
		fprintf(stderr, "inet_aton() failed\n");
		exit(1);
	}

	while (do_capture) {
		size_t r;

		const void *pulse_buffer = NULL;
		if (pulse_read_packet(&pulse, &pulse_buffer, &r)) {
			exit(1);
		}

		if (pulse_buffer != NULL) {
			const uint8_t *pb = pulse_buffer;
			size_t copy1 = ring_size - ring_head;
			if (copy1 > r) {
				copy1 = r;
			}
			memcpy(&ring_buffer[ring_head], pb, copy1);
			ring_head += copy1;
			ring_head %= ring_size;
			r -= copy1;

			if (r > 0) {
				memcpy(ring_buffer, &pb[copy1], r);
				ring_head += r;
			}
		}

		while ((ring_head - ring_tail) % ring_size >=
		       (FRAMES_COUNT * frame_size * NCHANNELS)) {
			int encoded;
			size_t offset = 0U;

			encoded =
			    encode_frames(encoder, &ring_buffer[ring_tail],
					  FRAMES_COUNT, enc_buffer, enc_size);

			ring_tail += (FRAMES_COUNT * frame_size * NCHANNELS);
			ring_tail %= ring_size;

			while (encoded > 0) {
				uint16_t len = (uint16_t)encoded;
				if (len > MAX_DATA_SIZE) {
					len = MAX_DATA_SIZE;
				}

				packet.data.hdr.magic = PACKET_MAGIC;
				packet.data.hdr.uid = packet_id++;
				packet.data.hdr.packet_len =
				    len + sizeof(packet_header_t);
				packet.data.hdr.codec_type = (uint8_t)codec;
				packet.data.hdr.channels = NCHANNELS;
				packet.data.hdr.format =
				    (uint8_t)PA_SAMPLE_S16LE;
				packet.data.hdr.rate = (uint32_t)rate;

				memcpy(packet.data.data, &enc_buffer[offset],
				       len);
				offset += len;
				encoded -= len;

				/* UDP send */
				if (sendto(s, packet.u8,
					   packet.data.hdr.packet_len, 0,
					   (struct sockaddr *)&si_other,
					   (socklen_t)slen) == -1) {
					fprintf(stderr,
						"cannot send to socket\n");
					break;
				}
			}
		}
	}

	pulse_close(&pulse);

	free(input_buffer);
	free(ring_buffer);
	free(enc_buffer);

	free_encoder(encoder);

	return 0;
}
