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
#include <pulse/simple.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES_COUNT (256U)
#define NSTREAMS (64U)

static pa_simple *
init_capture(int rate, pa_sample_format_t format)
{

	static pa_buffer_attr buffer_attr;
	pa_simple *rec;

	pa_sample_spec ss;

	ss.format = format;
	ss.channels = 2;
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

static void *
init_encoder(int rate, int kbitrate)
{
	lame_t encoder = lame_init();

	lame_set_in_samplerate(encoder, rate);
	lame_set_VBR(encoder, vbr_off);
	lame_set_brate(encoder, kbitrate);
	lame_set_force_short_blocks(encoder, 1);
	lame_init_params(encoder);

	return encoder;
}

static int
encode_buffer(void *encoder, int16_t *buffer, size_t in_size, uint8_t *out,
	      size_t out_max)
{
	lame_t lame = (lame_t)encoder;

	int encoded;
	encoded = lame_encode_buffer_interleaved(lame, buffer, (int)in_size,
						 out, (int)out_max);

	return encoded;
}

int
main(int argc, char *argv[])
{
	char *addr = NULL;
	uint16_t port = 0U;
	int rate = 48000;
	int kbitrate = 192;

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
	size_t data_size = frame_size * FRAMES_COUNT;

	const size_t enc_size = 8192U;
	unsigned char *enc_buffer;

	input_buffer = malloc(data_size);

	enc_buffer = malloc(enc_size);

	void *encoder = init_encoder(rate, kbitrate);

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

		encoded = encode_buffer(encoder, input_buffer, FRAMES_COUNT,
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

	return 0;
}
