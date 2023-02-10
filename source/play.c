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

#define NSTREAMS (16U)
#define NCHANNELS (2U)
#define BUFSIZE (8192U)

typedef enum {
	CODEC_MP3, /* use lame */
	CODEC_OPUS /* use opus */
} codec_type_t;

typedef struct {
	void *decoder_p;
	codec_type_t codec_type;
} decoder_desc_t;

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
init_decoder(int rate, codec_type_t codec)
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
		opus = opus_decoder_create(rate, 2, &err);
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

int
main(int argc, char *argv[])
{
	uint16_t port = 0U;
	int rate = 48000;
	uint32_t prebuf = 2U;
	codec_type_t codec = CODEC_OPUS;

	int c;
	while ((c = getopt(argc, argv, "p:r:B:")) != -1) {
		char *end;
		switch (c) {
		case 'p':
			port = (uint16_t)strtoul(optarg, &end, 10);
			break;

		case 'r':
			rate = (int)strtol(optarg, &end, 10);
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

	pa_simple *play = init_playback(rate, PA_SAMPLE_S16LE, prebuf);
	int err = 0;
	size_t frame_size = pa_sample_size_of_format(PA_SAMPLE_S16LE);
	size_t data_size = frame_size * 8192U;

	const size_t input_maxsize = 1024U;
	unsigned char *input_buffer;

	input_buffer = malloc(input_maxsize);

	decoder_desc_t *decoder = init_decoder(rate, codec);

	short int *pcm_buffer;

	pcm_buffer = malloc(data_size);

	/* UDP init */
	struct sockaddr_in sockaddr;
	int sock;
	socklen_t slen = sizeof(sockaddr);
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

	int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);

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

		ssize_t data_len =
		    recvfrom(sock, input_buffer, input_maxsize, 0,
			     (struct sockaddr *)&sockaddr, &slen);
		if (data_len > 0) {
			int decoded;

			decoded = decode_buffer(decoder, input_buffer,
						(size_t)data_len, pcm_buffer,
						BUFSIZE);
			if (decoded > 0) {
				decoded = pa_simple_write(
				    play, pcm_buffer,
				    ((size_t)decoded * frame_size * NCHANNELS),
				    &err);
				if (decoded < 0) {
					fprintf(stderr, "pa_error\n");
				}
			}
		}
	}

	pa_simple_free(play);

	free(input_buffer);
	free(pcm_buffer);

	free_decoder(decoder);

	return 0;
}
