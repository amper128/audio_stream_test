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

#include <pulse/error.h>
#include <pulse/simple.h>

static unsigned int rate = 48000;
#define NSTREAMS 4
#define SINE_HZ 440
#define SAMPLE_HZ 48000

#define FRAMES_COUNT (256U)

int
main(void)
{
	pa_simple *play;
	pa_simple *rec;
	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = rate;
	int *err = 0;
	char *data;
	size_t data_size = pa_frame_size(&ss) * FRAMES_COUNT;

	const int mp3_size = 8192;
	unsigned char *mp3_buffer;
	FILE *mp3_file = fopen("file.mp3", "wb");

	static const pa_buffer_attr buffer_attr = {
	    .maxlength = SAMPLE_HZ * sizeof(float) *
			 NSTREAMS, /* exactly space for the entire play time */
	    .tlength = (uint32_t)-1,
	    .prebuf = 0, /* Setting prebuf to 0 guarantees us the streams will
			    run synchronously, no matter what */
	    .minreq = (uint32_t)-1,
	    .fragsize = 0};

	data = malloc(data_size);

	play = pa_simple_new(NULL,     // Use the default server.
			     "Fooapp", // Our application's name.
			     PA_STREAM_PLAYBACK,
			     NULL,	   // Use the default device.
			     "Music",	   // Description of our stream.
			     &ss,	   // Our sample format.
			     NULL,	   // Use default channel map
			     &buffer_attr, // Use default buffering attributes.
			     NULL	   // Ignore error code.
	);

	rec = pa_simple_new(NULL,     // Use the default server.
			    "Fooapp", // Our application's name.
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
	lame_set_in_samplerate(lame, (int)rate);
	lame_set_VBR(lame, vbr_default);
	lame_init_params(lame);

	while (1) {
		int wr;
		pa_simple_read(rec, data, data_size, err);

		wr = lame_encode_buffer_interleaved(
		    lame, (short *)data, FRAMES_COUNT, mp3_buffer, mp3_size);
		if (wr > 0) {
			fwrite(mp3_buffer, (size_t)wr, 1, mp3_file);
		}

		// pa_simple_write(play, data, data_size, err);
	}

	pa_simple_free(rec);
	pa_simple_free(play);

	free(data);

	return 0;
}
