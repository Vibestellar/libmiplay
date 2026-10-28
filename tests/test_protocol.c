#include "libmiplay/libmiplay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  int audio_calls;
  uint64_t last_pts;
  uint8_t data[32];
  size_t len;
  libmiplay_state_t state;
} test_ctx_t;

static void on_log(libmiplay_log_level_t level, const char *message, void *userdata) {
  (void)level;
  (void)message;
  (void)userdata;
}

static void on_state(libmiplay_state_t state, void *userdata) {
  test_ctx_t *ctx = (test_ctx_t *)userdata;
  ctx->state = state;
}

static void on_audio(const uint8_t *data, size_t len, uint64_t pts, void *userdata) {
  test_ctx_t *ctx = (test_ctx_t *)userdata;
  ctx->audio_calls++;
  ctx->last_pts = pts;
  ctx->len = len;
  if (len > sizeof(ctx->data)) {
    len = sizeof(ctx->data);
  }
  memcpy(ctx->data, data, len);
}

static void on_error(libmiplay_error_t err, const char *message, void *userdata) {
  (void)err;
  (void)message;
  (void)userdata;
}

int main(void) {
  test_ctx_t ctx = {0};
  libmiplay_config_t cfg = {
      .receiver_name = "test",
      .bind_address = "127.0.0.1",
      .control_port = 0,
      .audio_port = 18899,
      .socket_timeout_ms = 50,
  };

  libmiplay_callbacks_t cb = {
      .on_log = on_log,
      .on_state_changed = on_state,
      .on_audio_frame = on_audio,
      .on_error = on_error,
      .userdata = &ctx,
  };

  libmiplay_receiver_t *receiver = NULL;
  assert(libmiplay_receiver_create(&cfg, &cb, &receiver) == LIBMIPLAY_OK);

  const uint8_t handshake[] = {1};
  const uint8_t audio[] = {2, 0, 0, 0, 0, 0, 0, 0, 10, '1', '2', '3'};
  const uint8_t teardown[] = {3};

  assert(libmiplay_receiver_feed_packet(receiver, handshake, sizeof(handshake)) == LIBMIPLAY_OK);
  assert(ctx.state == LIBMIPLAY_STATE_SESSION_NEGOTIATING);

  assert(libmiplay_receiver_feed_packet(receiver, audio, sizeof(audio)) == LIBMIPLAY_OK);
  assert(ctx.audio_calls == 1);
  assert(ctx.last_pts == 10);
  assert(ctx.len == 3);
  assert(memcmp(ctx.data, "123", 3) == 0);
  assert(ctx.state == LIBMIPLAY_STATE_STREAMING);

  assert(libmiplay_receiver_feed_packet(receiver, teardown, sizeof(teardown)) == LIBMIPLAY_OK);
  assert(ctx.state == LIBMIPLAY_STATE_STOPPED);

  libmiplay_receiver_destroy(receiver);
  puts("ok");
  return 0;
}
