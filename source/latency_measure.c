/**
 * @file latency_measure.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Измерение задержки на кодирование-декодирование
 */

#include <lame/lame.h>
#include <opus/opus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pulse/error.h>
#include <pulse/simple.h>

#define USE_MP3 0
#define USE_OPUS 1

static const int rate = 48000;
#define NSTREAMS (64U)

#define FRAMES_COUNT (120)

#define APPLICATION OPUS_APPLICATION_AUDIO
#define OPUS_BITRATE (128000)

int
main(void)
{
	int err = 0;

#if (USE_MP3 == 1)
	lame_t mp3_encoder;
	hip_t mp3_decoder;

	mp3_encoder = lame_init();
	lame_set_in_samplerate(mp3_encoder, (int)rate);
	lame_set_VBR(mp3_encoder, vbr_default);
	lame_init_params(mp3_encoder);

	mp3_decoder = hip_decode_init();
#endif
#if (USE_OPUS == 1)
	OpusEncoder *opus_encoder;
	OpusDecoder *opus_decoder;

	/*Create a new encoder state */
	opus_encoder =
	    opus_encoder_create(rate, 2, OPUS_APPLICATION_AUDIO, &err);
	if (err < 0) {
		fprintf(stderr, "failed to create an encoder: %s\n",
			opus_strerror(err));
		return EXIT_FAILURE;
	}

	err = opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
	if (err < 0) {
		fprintf(stderr, "failed to set bitrate: %s\n",
			opus_strerror(err));
		return EXIT_FAILURE;
	}

	/* Create a new decoder state. */
	opus_decoder = opus_decoder_create(rate, 2, &err);
	if (err < 0) {
		fprintf(stderr, "failed to create decoder: %s\n",
			opus_strerror(err));
		return EXIT_FAILURE;
	}
#endif

	pa_simple *rec;
	pa_simple *play;
	pa_sample_spec ss;

	ss.format = PA_SAMPLE_S16LE;
	ss.channels = 2;
	ss.rate = rate;

	int16_t *data;
	size_t frame_size = pa_frame_size(&ss);
	size_t data_size = frame_size * FRAMES_COUNT;

	const int encoded_size = 8192;
	unsigned char *encoded_buffer;
	FILE *pcm_file = fopen("file.pcm", "wb");

	static pa_buffer_attr buffer_attr;

	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	buffer_attr.prebuf = 2;
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

	encoded_buffer = malloc(encoded_size);

	int16_t *pcm_buffer1 = calloc(data_size * 4, 1);
#if (USE_MP3 == 1)
	int16_t *pcm_buffer2 = calloc(data_size * 4, 1);
#endif

	int16_t *pcm_buffer_test = calloc(2, 128 * 1024 * 1024);
	size_t left = 0U;
	size_t right = 0U;

	int t = 0;

	int first = 1;

	for (t = 0; t < 1024; t++) {
		int wr;

		size_t i;

		pa_simple_read(rec, data, data_size, &err);

		for (i = 0U; i < data_size; i++) {
			pcm_buffer_test[left + i * 2] = data[i * 2];
		}
		left += data_size / 2;

#if (USE_MP3 == 1)
		wr = lame_encode_buffer_interleaved(
		    mp3_encoder, (short *)data, FRAMES_COUNT, encoded_buffer,
		    encoded_size);
#endif
#if (USE_OPUS == 1)
		/* Encode the frame. */
		wr = opus_encode(opus_encoder, data, FRAMES_COUNT,
				 encoded_buffer, encoded_size);
		if (wr < 0) {
			fprintf(stderr, "encode failed: %s\n",
				opus_strerror(wr));
			return EXIT_FAILURE;
		}
#endif
		if (wr > 0) {
			int d;
#if (USE_MP3 == 1)
			d = hip_decode(mp3_decoder, encoded_buffer, (size_t)wr,
				       pcm_buffer1, pcm_buffer2);
#endif
#if (USE_OPUS == 1)
			d = opus_decode(opus_decoder, encoded_buffer, wr,
					pcm_buffer1, data_size * 4, 0);
			if (d < 0) {
				fprintf(stderr, "decoder failed: %s\n",
					opus_strerror(d));
				return EXIT_FAILURE;
			}
#endif
			if (d > 0) {
				if (first == 1) {
					right = left;
					first = 0;
				}

				for (i = 0U; i < (size_t)d; i++) {
#if (USE_MP3 == 1)
					pcm_buffer_test[right + i * 2 + 1] =
					    pcm_buffer1[i];
#endif
#if (USE_OPUS == 1)
					pcm_buffer_test[right + i * 2 + 1] =
					    pcm_buffer1[i * 2 + 1];

					/*pcm_buffer_test[right + i * 2] =
					    pcm_buffer1[i * 2];*/
#endif
				}
				right += (size_t)d * 2;
				// right = 0;

				d = pa_simple_write(play, pcm_buffer1,
						    ((size_t)d * frame_size),
						    &err);
				if (d < 0) {
					fprintf(stderr, "pa_error\n");
				}
			}
		}
	}

	fwrite(pcm_buffer_test, (size_t)(left + data_size), sizeof(short),
	       pcm_file);
	fclose(pcm_file);

	pa_simple_free(rec);
	pa_simple_free(play);

#if (USE_OPUS == 1)
	opus_encoder_destroy(opus_encoder);
	opus_decoder_destroy(opus_decoder);
#endif

	free(data);
	free(pcm_buffer1);
#if (USE_MP3 == 1)
	free(pcm_buffer2);
#endif
	free(pcm_buffer_test);

	return 0;
}
