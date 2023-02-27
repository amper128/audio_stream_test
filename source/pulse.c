/**
 * @file pulse.c
 * @author Алексей Хохлов <root@amper.me>
 * @copyright WTFPL License
 * @date 2023
 * @brief Функции для работы с асинхронным API PulseAudio
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pulse_api.h>

#define NSTREAMS (64U)

static void
context_state_cb(pa_context *c, void *userdata)
{
	pulse_t *p = userdata;

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_FAILED:
		pa_threaded_mainloop_signal(p->mainloop, 0);
		break;

	default:
		break;
	}
}

static void
stream_state_cb(pa_stream *s, void *userdata)
{
	pulse_t *p = userdata;

	switch (pa_stream_get_state(s)) {
	case PA_STREAM_READY:
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		pa_threaded_mainloop_signal(p->mainloop, 0);
		break;

	default:
		break;
	}
}

static void
stream_request_cb(pa_stream *s, size_t length, void *userdata)
{
	(void)s;
	(void)length;

	pulse_t *p = userdata;

	pa_threaded_mainloop_signal(p->mainloop, 0);
}

static void
stream_latency_update_cb(pa_stream *s, void *userdata)
{
	(void)s;

	pulse_t *p = userdata;

	pa_threaded_mainloop_signal(p->mainloop, 0);
}

static void
context_poll_unless(pa_threaded_mainloop *pa, pa_context *ctx,
		    pa_context_state_t state)
{
	for (;;) {
		pa_context_state_t s;
		pa_threaded_mainloop_lock(pa);
		s = pa_context_get_state(ctx);
		pa_threaded_mainloop_unlock(pa);
		if (s == state) {
			break;
		}
		pa_threaded_mainloop_wait(pa);
	}
}

static void
stream_poll_unless(pa_threaded_mainloop *pa, pa_stream *stream,
		   pa_stream_state_t state)
{
	for (;;) {
		pa_stream_state_t s;
		pa_threaded_mainloop_lock(pa);
		s = pa_stream_get_state(stream);
		pa_threaded_mainloop_unlock(pa);
		if (s == state) {
			break;
		}
		pa_threaded_mainloop_wait(pa);
	}
}

int
pulse_close(pulse_t *pd)
{
	if (pd->mainloop) {
		pa_threaded_mainloop_stop(pd->mainloop);
	}

	if (pd->stream) {
		pa_stream_unref(pd->stream);
	}
	pd->stream = NULL;

	if (pd->context) {
		pa_context_disconnect(pd->context);
		pa_context_unref(pd->context);
	}
	pd->context = NULL;

	if (pd->mainloop) {
		pa_threaded_mainloop_free(pd->mainloop);
	}
	pd->mainloop = NULL;

	return 0;
}

int
pulse_open(pulse_t *pd, uint32_t rate, pa_sample_format_t format,
	   uint8_t channels, const char *name)
{
	char *device = NULL;
	int ret;
	pd->sample_rate = rate;
	pd->channels = channels;
	pd->name = name;
	pd->stream_name = name;
	const pa_sample_spec ss = {format, pd->sample_rate, pd->channels};

	pa_buffer_attr buffer_attr;
	/* exactly space for the entire play time */
	buffer_attr.maxlength =
	    (uint32_t)((size_t)rate * sizeof(float) * NSTREAMS);
	buffer_attr.tlength = (uint32_t)-1;
	/* Setting prebuf to 0 guarantees us the streams will run synchronously,
	 * no matter what */
	buffer_attr.prebuf = 0;
	buffer_attr.minreq = (uint32_t)-1;
	buffer_attr.fragsize = 0;

	if (!(pd->mainloop = pa_threaded_mainloop_new())) {
		pulse_close(pd);
		return 1;
	}

	if (pa_threaded_mainloop_start(pd->mainloop) < 0) {
		pulse_close(pd);
		return 1;
	}

	pa_mainloop_api *api = pa_threaded_mainloop_get_api(pd->mainloop);

	if (!(pd->context = pa_context_new(api, pd->name))) {
		pulse_close(pd);
		return 1;
	}

	pa_threaded_mainloop_lock(pd->mainloop);
	if (pa_context_connect(pd->context, NULL, 0, NULL) < 0) {
		pulse_close(pd);
		ret = pa_context_errno(pd->context);
		goto unlock_and_fail;
	}

	pa_context_set_state_callback(pd->context, context_state_cb, pd);
	pa_threaded_mainloop_unlock(pd->mainloop);

	context_poll_unless(pd->mainloop, pd->context, PA_CONTEXT_READY);

	// stream create
	pa_threaded_mainloop_lock(pd->mainloop);
	pd->stream = pa_stream_new(pd->context, pd->stream_name, &ss, NULL);
	if (!pd->stream) {
		ret = pa_context_errno(pd->context);
		goto unlock_and_fail;
	}

	pa_stream_set_state_callback(
	    pd->stream, (pa_stream_notify_cb_t)stream_state_cb, pd);
	pa_threaded_mainloop_unlock(pd->mainloop);

	pa_stream_set_read_callback(pd->stream, stream_request_cb, pd);
	pa_stream_set_write_callback(pd->stream, stream_request_cb, pd);
	pa_stream_set_latency_update_callback(pd->stream,
					      stream_latency_update_cb, pd);

	context_poll_unless(pd->mainloop, pd->context, PA_CONTEXT_READY);

	// stream connect
	pa_threaded_mainloop_lock(pd->mainloop);
	ret = pa_stream_connect_record(pd->stream, device, &buffer_attr,
				       PA_STREAM_INTERPOLATE_TIMING |
					   PA_STREAM_ADJUST_LATENCY |
					   PA_STREAM_AUTO_TIMING_UPDATE);

	if (ret < 0) {
		ret = pa_context_errno(pd->context);
		goto unlock_and_fail;
	}

	pa_threaded_mainloop_unlock(pd->mainloop);

	context_poll_unless(pd->mainloop, pd->context, PA_CONTEXT_READY);
	stream_poll_unless(pd->mainloop, pd->stream, PA_STREAM_READY);

	return 0;

unlock_and_fail:
	pa_threaded_mainloop_unlock(pd->mainloop);

	pulse_close(pd);
	return ret;
}

int
pulse_read_packet(pulse_t *pd, const void **data, size_t *size)
{
	int ret;
	size_t read_length = 0U;

	pa_threaded_mainloop_lock(pd->mainloop);

	if (!pd->context ||
	    !PA_CONTEXT_IS_GOOD(pa_context_get_state(pd->context)) ||
	    !pd->stream ||
	    !PA_STREAM_IS_GOOD(pa_stream_get_state(pd->stream))) {
		ret = 1;
		goto unlock_and_fail;
	}

	while (!(*data)) {
		int r;

		r = pa_stream_peek(pd->stream, data, &read_length);
		if (r != 0) {
			ret = pa_context_errno(pd->context);
			goto unlock_and_fail;
		}

		if (read_length == 0) {
			pa_threaded_mainloop_wait(pd->mainloop);
			if (!pd->context ||
			    !PA_CONTEXT_IS_GOOD(
				pa_context_get_state(pd->context)) ||
			    !pd->stream ||
			    !PA_STREAM_IS_GOOD(
				pa_stream_get_state(pd->stream))) {
				ret = pa_context_errno(pd->context);
				goto unlock_and_fail;
			}
		} else if (!(*data)) {
			/* There's a hole in the stream, skip it. We could
			 * generate silence, but that wouldn't work for
			 * compressed streams. */
			r = pa_stream_drop(pd->stream);
			if (r != 0) {
				ret = pa_context_errno(pd->context);
				goto unlock_and_fail;
			}
		}
	}

	*size = read_length;
	pa_stream_drop(pd->stream);

	pa_threaded_mainloop_unlock(pd->mainloop);
	return 0;

unlock_and_fail:
	pa_threaded_mainloop_unlock(pd->mainloop);
	return ret;
}
