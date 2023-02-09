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
#include <pulse/simple.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#define NSTREAMS (64U)

int
main(int argc, char *argv[])
{
	uint16_t port = 0U;
	int rate = 44100;
	uint32_t prebuf = 2U;

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
			prebuf = strtoul(optarg, &end, 10);
			break;

		default:
			return -1;
		}
	}

	if (port == 0) {
		return -1;
	}

	pa_simple *play;
	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = (uint32_t)rate;
	int *err = 0;
	size_t frame_size = pa_frame_size(&ss);
	size_t data_size = frame_size * 8192U;

	const size_t mp3_size = 8192U;
	unsigned char *mp3_buffer;

	static pa_buffer_attr buffer_attr;

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

	mp3_buffer = malloc(mp3_size);

	hip_t dec = hip_decode_init();

	short int *pcm_buffer1;
	short int *pcm_buffer2;
	short int *pcm_buffer_i;

	pcm_buffer1 = malloc(data_size * 4);
	pcm_buffer2 = malloc(data_size * 4);
	pcm_buffer_i = malloc(data_size * 4);

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
		    recvfrom(sock, mp3_buffer, mp3_size, 0,
			     (struct sockaddr *)&sockaddr, &slen);
		if (data_len > 0) {
			int d;

			mp3data_struct mp3data;

			d = hip_decode_headers(dec, mp3_buffer,
					       (size_t)data_len, pcm_buffer1,
					       pcm_buffer2, &mp3data);
			if (d > 0) {
				/* convert l+r buffers to interleaved */
				int i;
				for (i = 0U; i < d; i++) {
					pcm_buffer_i[i * 2] = pcm_buffer1[i];
					pcm_buffer_i[i * 2 + 1] =
					    pcm_buffer2[i];
				}

				i = pa_simple_write(play, pcm_buffer_i,
						    ((size_t)d * frame_size),
						    err);
				if (i < 0) {
					fprintf(stderr, "pa_error\n");
				}
			}
		}
	}

	pa_simple_free(play);

	free(mp3_buffer);
	free(pcm_buffer1);
	free(pcm_buffer2);
	free(pcm_buffer_i);

	return 0;
}
