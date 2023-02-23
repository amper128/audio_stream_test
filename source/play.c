/**
 * @file play.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Проигрывание аудио
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <lame/lame.h>
#include <opus/opus.h>
#include <pulse/simple.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#include <streaming_shared.h>

#define NSTREAMS (16U)
#define BUFSIZE (8192U)

#define MAX_PACKET_SIZE (1400U)

typedef struct {
	void *decoder_p;
	pa_simple *pulse_p;
	codec_type_t codec_type;
	uint8_t frame_size;
	uint8_t channels;
	uint8_t _reserved[1];
	uint8_t skip_next;
} decoder_desc_t;

static int stream_started = 0;

static pa_simple *
init_playback(int rate, pa_sample_format_t format, uint32_t prebuf)
{
	static pa_buffer_attr buffer_attr;
	pa_simple *play;

	pa_sample_spec ss;

	ss.format = format;
	ss.channels = 2;
	ss.rate = (uint32_t)rate;

	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	buffer_attr.prebuf = prebuf;
	buffer_attr.minreq = (uint32_t)-1;
	buffer_attr.fragsize = 0;

	play = pa_simple_new(NULL,	  // Use the default server.
			     "Test play", // Our application's name.
			     PA_STREAM_PLAYBACK,
			     NULL,	   // Use the default device.
			     "Music",	   // Description of our stream.
			     &ss,	   // Our sample format.
			     NULL,	   // Use default channel map
			     &buffer_attr, // Use default buffering attributes.
			     NULL	   // Ignore error code.
	);

	if (play == NULL) {
		fprintf(stderr, "cannot create pulseaudio stream\n");
		exit(1);
	}

	return play;
}

static decoder_desc_t *
init_decoder(int rate, codec_type_t codec, int channels)
{
	switch (codec) {
	default:
	case CODEC_MP3: {
		hip_t dec = hip_decode_init();
		decoder_desc_t *result = malloc(sizeof(decoder_desc_t));
		result->codec_type = codec;
		result->decoder_p = dec;
		return result;
	}
	case CODEC_OPUS: {
		OpusDecoder *opus;
		int err = 0;
		/* Create a new decoder state. */
		opus = opus_decoder_create(rate, channels, &err);
		if (err < 0) {
			fprintf(stderr, "failed to create decoder: %s\n",
				opus_strerror(err));
			exit(1);
		}
		decoder_desc_t *result = malloc(sizeof(decoder_desc_t));
		result->codec_type = codec;
		result->decoder_p = opus;
		return result;
	}
	}
}

static void
free_decoder(decoder_desc_t *decoder)
{
	free(decoder->decoder_p);
	free(decoder);
}

static int
decode_buffer(decoder_desc_t *decoder, uint8_t *buffer, size_t in_size,
	      short *out, size_t out_max)
{
	int decoded;

	switch (decoder->codec_type) {
	default:
	case CODEC_MP3: {
		hip_t hip = (hip_t)decoder->decoder_p;
		short pcm_l[BUFSIZE];
		short pcm_r[BUFSIZE];

		decoded = hip_decode(hip, buffer, in_size, pcm_l, pcm_r);
		if (decoded > 0) {
			/* convert to interleaved format */
			size_t i;
			for (i = 0U; (i < (size_t)decoded) && (i < BUFSIZE);
			     i++) {
				out[i * 2] = pcm_l[i];
				out[i * 2 + 1] = pcm_r[i];
			}
		}
	} break;

	case CODEC_OPUS: {
		OpusDecoder *opus = decoder->decoder_p;

		decoded = opus_decode(opus, buffer, (opus_int32)in_size, out,
				      (int)out_max, 0);
		if (decoded < 0) {
			fprintf(stderr, "decoder failed: %s\n",
				opus_strerror(decoded));
			return EXIT_FAILURE;
		}
	} break;
	}

	return decoded;
}

static decoder_desc_t *
stream_start(codec_type_t codec, int rate, pa_sample_format_t format,
	     int channels, uint32_t prebuf)
{
	decoder_desc_t *decoder = init_decoder(rate, codec, channels);

	if (decoder != NULL) {
		decoder->pulse_p = init_playback(rate, format, prebuf);
		if (decoder->pulse_p == NULL) {
			free_decoder(decoder);
			return NULL;
		}

		decoder->frame_size = (uint8_t)pa_sample_size_of_format(format);
		decoder->channels = (uint8_t)channels;

		int err = 0;
		pa_simple_flush(decoder->pulse_p, &err);
	}

	return decoder;
}

int
main(int argc, char *argv[])
{
	uint16_t port = 0U;
	// pa_sample_format_t format = PA_SAMPLE_S16LE;
	uint32_t prebuf = 2U;

	int c;
	while ((c = getopt(argc, argv, "p:B:")) != -1) {
		char *end;
		switch (c) {
		case 'p':
			port = (uint16_t)strtoul(optarg, &end, 10);
			break;

		case 'B':
			prebuf = (uint32_t)strtoul(optarg, &end, 10);
			break;

		default:
			return -1;
		}
	}

	if (port == 0) {
		return -1;
	}

	// pa_simple *play = init_playback(rate, format, prebuf);
	int err = 0;
	// size_t frame_size = pa_sample_size_of_format(format);
	// size_t data_size = frame_size * 8192U;

	decoder_desc_t *decoder = NULL;

	short int *pcm_buffer;

	pcm_buffer = malloc(8192U * sizeof(float) * 2U);

	/* UDP init */
	struct sockaddr_in sockaddr;
	int sock;
	// socklen_t slen = sizeof(sockaddr);
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset(sockaddr.sin_zero, '\0', sizeof(sockaddr.sin_zero));

	if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "Could not create UDP socket!\n");
		exit(1);
	}

	if (bind(sock, (struct sockaddr *)&sockaddr, sizeof(struct sockaddr)) ==
	    -1) {
		fprintf(stderr, "bind()\n");
		exit(1);
	}

	/*int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);*/

	while (1) {
		struct pollfd fds[2];
		memset(fds, 0, sizeof(fds));
		int timeout;

		fds[0].fd = sock;
		fds[0].events = POLL_IN;

		timeout = 1000; /* 1 sec */

		int rc;

		rc = poll(fds, 1, timeout);
		if (rc < 0) {
			fprintf(stderr, "poll() error\n");
			break;
		}

		if (rc == 0) {
			/* no data */
			continue;
		}

		union {
			uint8_t u8[MAX_PACKET_SIZE];
			struct {
				packet_header_t hdr;
				stream_start_t meta;
			} start;
			struct {
				packet_header_t hdr;
			} end;
			struct {
				packet_header_t hdr;
				uint8_t data[];
			} data;
		} packet;

		ssize_t data_len =
		    recv(sock, packet.u8, sizeof(packet_header_t), MSG_PEEK);
		if (data_len > 0) {
			if (packet.data.hdr.magic != PACKET_MAGIC) {
				continue;
			}

			size_t offset = 0U;
			size_t len = packet.data.hdr.packet_len;

			do {
				data_len =
				    recv(sock, &packet.u8[offset], len, 0);

				if (data_len < 0) {
					fprintf(stderr, "cannot read socket\n");
					break;
				}

				len -= (size_t)data_len;
				offset += (size_t)data_len;
			} while (len > 0U);

			switch (packet.data.hdr.packet_type) {
			case (uint8_t)PACKET_TYPE_START_STREAM:
				if (stream_started > 0) {
					pa_simple_free(decoder->pulse_p);
					free_decoder(decoder);
				}

				decoder = stream_start(
				    packet.start.meta.codec_type,
				    (int)packet.start.meta.rate,
				    (pa_sample_format_t)
					packet.start.meta.format,
				    packet.start.meta.channels, prebuf);

				if (decoder != NULL) {
					stream_started = 1;
				}

				break;

			case (uint8_t)PACKET_TYPE_STOP_STREAM:
				if (stream_started > 0) {
					pa_simple_free(decoder->pulse_p);
					free_decoder(decoder);
					stream_started = 0;
				}

				break;

			case (uint8_t)PACKED_TYPE_STREAM_DATA:
				if (stream_started > 0) {
					int decoded;

					decoded = decode_buffer(
					    decoder, packet.data.data,
					    packet.data.hdr.packet_len -
						sizeof(packet_header_t),
					    pcm_buffer, BUFSIZE);

					if (decoded <= 0) {
						break;
					}

					if (decoder->skip_next > 0) {
						decoder->skip_next = 0;
						break;
					}

					size_t i;
					for (i = 0U; i < (size_t)decoded;
					     i += 4U) {
						pcm_buffer[(i + 3) * 2] =
						    pcm_buffer[(i + 1) * 2];
						pcm_buffer[(i + 2) * 2] =
						    pcm_buffer[(i)*2];
					}

					decoded = pa_simple_write(
					    decoder->pulse_p, pcm_buffer,
					    ((size_t)decoded *
					     (size_t)decoder->frame_size *
					     (size_t)decoder->channels),
					    &err);
					if (decoded < 0) {
						fprintf(stderr, "pa_error\n");
					}
				}

				break;

			default:
				break;
			}
		}
	}

	if (stream_started > 0) {
		pa_simple_free(decoder->pulse_p);
		free_decoder(decoder);
	}

	free(pcm_buffer);

	return 0;
}
