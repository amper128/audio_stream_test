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
#include <stdlib.h>
#include <string.h>

#define FRAMES_COUNT (120U)
#define NSTREAMS (64U)
#define NCHANNELS (2U)

typedef enum {
	CODEC_MP3, /* use lame */
	CODEC_OPUS /* use opus */
} codec_type_t;

typedef struct {
	void *encoder_p;
	codec_type_t codec_type;
} encoder_desc_t;

static pa_simple *
init_capture(int rate, pa_sample_format_t format)
{
	static pa_buffer_attr buffer_attr;
	pa_simple *rec;

	pa_sample_spec ss;

	ss.format = format;
	ss.channels = NCHANNELS;
	ss.rate = (uint32_t)rate;

	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	/* Setting prebuf to 0 guarantees us the streams will run synchronously,
	 * no matter what */
	buffer_attr.prebuf = 0;
	buffer_attr.minreq = (uint32_t)-1;
	buffer_attr.fragsize = 0;

	rec = pa_simple_new(NULL,	    // Use the default server.
			    "Test capture", // Our application's name.
			    PA_STREAM_RECORD,
			    NULL,	  // Use the default device.
			    "Music",	  // Description of our stream.
			    &ss,	  // Our sample format.
			    NULL,	  // Use default channel map
			    &buffer_attr, // Use default buffering attributes.
			    NULL	  // Ignore error code.
	);

	if (rec == NULL) {
		fprintf(stderr, "cannot create pulseaudio capture\n");
		exit(1);
	}

	return rec;
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
encode_frames(encoder_desc_t *encoder, int16_t *buffer, size_t frames,
	      uint8_t *out, size_t out_max)
{
	int encoded;

	switch (encoder->codec_type) {
	default:
	case CODEC_MP3: {
		lame_t lame = (lame_t)encoder->encoder_p;

		encoded = lame_encode_buffer_interleaved(
		    lame, buffer, (int)frames, out, (int)out_max);

	} break;

	case CODEC_OPUS: {
		OpusEncoder *opus = encoder->encoder_p;

		encoded = opus_encode(opus, buffer, (int)frames, out,
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

	pa_simple *rec = init_capture(rate, PA_SAMPLE_S16LE);
	int err = 0;
	int16_t *input_buffer;
	size_t frame_size = pa_sample_size_of_format(PA_SAMPLE_S16LE);
	size_t data_size = frame_size * FRAMES_COUNT * NCHANNELS;

	const size_t enc_size = 8192U;
	unsigned char *enc_buffer;

	input_buffer = malloc(data_size * 8);
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

	while (1) {
		int encoded;

		pa_simple_read(rec, input_buffer, data_size, &err);

		encoded = encode_frames(encoder, input_buffer, FRAMES_COUNT,
					enc_buffer, enc_size);

		if (encoded > 0) {
			/* UDP send */
			if (sendto(s, enc_buffer, (size_t)encoded, 0,
				   (struct sockaddr *)&si_other,
				   (socklen_t)slen) == -1) {
				fprintf(stderr, "cannot send to socket\n");
				break;
			}
		}
	}

	pa_simple_free(rec);

	free(input_buffer);
	free(enc_buffer);

	free_encoder(encoder);

	return 0;
}
