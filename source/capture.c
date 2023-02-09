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

int
main(int argc, char *argv[])
{
	char *addr = NULL;
	uint16_t port = 0U;
	int rate = 44100;
	int bitrate = 192;

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
			bitrate = (int)strtol(optarg, &end, 10);
			break;

		default:
			return -1;
		}
	}

	if (port == 0) {
		return -1;
	}

	pa_simple *rec;
	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = (uint32_t)rate;
	int *err = 0;
	short *data;
	size_t frame_size = pa_frame_size(&ss);
	size_t data_size = frame_size * FRAMES_COUNT;

	const size_t mp3_size = 8192U;
	unsigned char *mp3_buffer;

	static pa_buffer_attr buffer_attr;

	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	/* Setting prebuf to 0 guarantees us the streams will run synchronously,
	 * no matter what */
	buffer_attr.prebuf = 0;
	buffer_attr.minreq = (uint32_t)-1;
	buffer_attr.fragsize = 0;

	data = malloc(data_size);

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

	mp3_buffer = malloc(mp3_size);

	lame_t lame = lame_init();
	lame_set_in_samplerate(lame, rate);
	lame_set_VBR(lame, vbr_off);
	lame_set_brate(lame, bitrate);
	lame_set_force_short_blocks(lame, 1);
	lame_init_params(lame);

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
		int wr;

		pa_simple_read(rec, data, data_size, err);

		wr = lame_encode_buffer_interleaved(lame, data, FRAMES_COUNT,
						    mp3_buffer, mp3_size);
		if (wr > 0) {
			/* UDP send */
			if (sendto(s, mp3_buffer, (size_t)wr, 0,
				   (struct sockaddr *)&si_other,
				   (socklen_t)slen) == -1) {
				fprintf(stderr, "cannot send to socket\n");
				break;
			}
		}
	}

	pa_simple_free(rec);

	free(data);
	free(mp3_buffer);

	return 0;
}
