/**
 * @file latency_measure.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Измерение задержки на кодирование-декодирование
 */

#include <lame/lame.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pulse/error.h>
#include <pulse/simple.h>

static const int rate = 44100;
#define NSTREAMS (64U)

#define FRAMES_COUNT (256U)

int
main(void)
{
	pa_simple *rec;
	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = rate;
	int *err = 0;
	short *data;
	size_t frame_size = pa_frame_size(&ss);
	size_t data_size = frame_size * FRAMES_COUNT;

	const int mp3_size = 8192;
	unsigned char *mp3_buffer;
	FILE *pcm_file = fopen("file.pcm", "wb");

	static pa_buffer_attr buffer_attr;

	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	buffer_attr.prebuf = 0;
	buffer_attr.minreq = (uint32_t)-1;
	buffer_attr.fragsize = 0;

	data = malloc(data_size);

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

	hip_t dec = hip_decode_init();

	short int *pcm_buffer1 = calloc(data_size * 4, 1);
	short int *pcm_buffer2 = calloc(data_size * 4, 1);

	short int *pcm_buffer_test = calloc(2, 128 * 1024 * 1024);
	size_t left = 0U;
	size_t right = 0U;

	int t = 0;

	int first = 1;

	for (t = 0; t < 256; t++) {
		int wr;

		size_t i;

		pa_simple_read(rec, data, data_size, err);

		for (i = 0U; i < data_size; i++) {
			pcm_buffer_test[left + i * 2] = data[i * 2];
		}
		left += data_size / 2;

		wr = lame_encode_buffer_interleaved(
		    lame, (short *)data, FRAMES_COUNT, mp3_buffer, mp3_size);
		if (wr > 0) {
			int d;
			d = hip_decode(dec, mp3_buffer, (size_t)wr, pcm_buffer1,
				       pcm_buffer2);
			if (d > 0) {
				if (first == 1) {
					right = left;
					first = 0;
				}

				for (i = 0U; i < (size_t)d; i++) {
					pcm_buffer_test[right + i * 2 + 1] =
					    pcm_buffer1[i];
				}
				right += (size_t)d;
			}
		}
	}

	fwrite(pcm_buffer_test, (size_t)(left + data_size), sizeof(short),
	       pcm_file);

	pa_simple_free(rec);

	free(data);
	free(pcm_buffer1);
	free(pcm_buffer2);
	free(pcm_buffer_test);

	return 0;
}
