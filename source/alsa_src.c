/**
 * @file alsa_src.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Захват аудио
 */

#include <alsa/asoundlib.h>
#include <lame.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
	int i;
	int err;
	char *buffer;
	const int buffer_frames = 256;
	unsigned int rate = 44100;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	FILE *pcm_file = fopen("file.pcm", "wb");

	const int mp3_size = 1024;
	unsigned char *mp3_buffer;
	FILE *mp3_file = fopen("file.mp3", "wb");

	(void)argc;

	if ((err = snd_pcm_open(&capture_handle, argv[1],
				SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf(stderr, "cannot open audio device %s (%s)\n", argv[1],
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "audio interface opened\n");

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr,
			"cannot allocate hardware parameter structure (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params allocated\n");

	if ((err = snd_pcm_hw_params_any(capture_handle, hw_params)) < 0) {
		fprintf(stderr,
			"cannot initialize hardware parameter structure (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params initialized\n");

	if ((err = snd_pcm_hw_params_set_access(
		 capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) <
	    0) {
		fprintf(stderr, "cannot set access type (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params access setted\n");

	if ((err = snd_pcm_hw_params_set_format(capture_handle, hw_params,
						format)) < 0) {
		fprintf(stderr, "cannot set sample format (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params format setted\n");

	if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, hw_params,
						   &rate, 0)) < 0) {
		fprintf(stderr, "cannot set sample rate (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params rate setted\n");

	if ((err = snd_pcm_hw_params_set_channels(capture_handle, hw_params,
						  2)) < 0) {
		fprintf(stderr, "cannot set channel count (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params channels setted\n");

	if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0) {
		fprintf(stderr, "cannot set parameters (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "hw_params setted\n");

	snd_pcm_hw_params_free(hw_params);

	fprintf(stdout, "hw_params freed\n");

	if ((err = snd_pcm_prepare(capture_handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use (%s)\n",
			snd_strerror(err));
		exit(1);
	}

	fprintf(stdout, "audio interface prepared\n");

	buffer = malloc(
	    (size_t)(buffer_frames * snd_pcm_format_width(format) / 8 * 2));
	mp3_buffer = malloc(mp3_size);

	fprintf(stdout, "buffers allocated\n");

	lame_t lame = lame_init();
	lame_set_in_samplerate(lame, (int)rate);
	lame_set_VBR(lame, vbr_default);
	lame_init_params(lame);

	fprintf(stdout, "LAME configured\n");
	int wr;

	for (i = 0; i < 1024; ++i) {
		if ((err = (int)snd_pcm_readi(capture_handle, buffer,
					      (size_t)buffer_frames)) !=
		    buffer_frames) {
			fprintf(stderr,
				"read from audio interface failed (%i, %s)\n",
				err, snd_strerror(err));
			exit(1);
		}
		fwrite(buffer,
		       (size_t)(buffer_frames * snd_pcm_format_width(format) /
				8 * 2),
		       1, pcm_file);
		fprintf(stdout, "read %d done\n", i);

		wr = lame_encode_buffer_interleaved(
		    lame, (short *)buffer, buffer_frames, mp3_buffer, mp3_size);
		fwrite(mp3_buffer, (size_t)wr, 1, mp3_file);

		fprintf(stdout, "mp3 write: %i\n", wr);
		fflush(stdout);
	}

	wr = lame_encode_flush(lame, mp3_buffer, mp3_size);
	fwrite(mp3_buffer, (size_t)wr, 1, mp3_file);

	lame_close(lame);
	fclose(mp3_file);
	fclose(pcm_file);

	free(buffer);
	free(mp3_buffer);

	fprintf(stdout, "buffers freed\n");

	snd_pcm_close(capture_handle);
	fprintf(stdout, "audio interface closed\n");

	exit(0);
}
