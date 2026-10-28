#include "libmiplay/libmiplay.h"

#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

typedef struct {
  FILE *dump_file;
} app_ctx_t;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static void on_log(libmiplay_log_level_t level, const char *msg, void *userdata) {
  (void)userdata;
  const char *lvl = "INFO";
  if (level == LIBMIPLAY_LOG_ERROR) lvl = "ERROR";
  if (level == LIBMIPLAY_LOG_WARN) lvl = "WARN";
  if (level == LIBMIPLAY_LOG_DEBUG) lvl = "DEBUG";
  fprintf(stderr, "[%s] %s\n", lvl, msg);
}

static void on_state(libmiplay_state_t state, void *userdata) {
  (void)userdata;
  fprintf(stderr, "[STATE] %d\n", (int)state);
}

static void on_audio(const uint8_t *data, size_t len, uint64_t pts, void *userdata) {
  app_ctx_t *ctx = (app_ctx_t *)userdata;
  fprintf(stderr, "[AUDIO] pts=%" PRIu64 " len=%zu\n", pts, len);
  if (ctx && ctx->dump_file) {
    (void)fwrite(data, 1, len, ctx->dump_file);
    fflush(ctx->dump_file);
  }
}

static void on_error(libmiplay_error_t err, const char *msg, void *userdata) {
  (void)userdata;
  fprintf(stderr, "[ERROR] code=%d msg=%s\n", (int)err, msg);
}

int main(int argc, char **argv) {
  uint16_t port = 8899;
  const char *bind_addr = "0.0.0.0";
  const char *dump_path = NULL;
  bool simulate = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = (uint16_t)strtoul(argv[++i], NULL, 10);
    } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      bind_addr = argv[++i];
    } else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
      dump_path = argv[++i];
    } else if (strcmp(argv[i], "--simulate") == 0) {
      simulate = true;
    } else if (strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
              "Usage: %s [--bind IP] [--port PORT] [--dump FILE] [--simulate]\n",
              argv[0]);
      return 0;
    }
  }

  app_ctx_t ctx = {0};
  if (dump_path) {
    ctx.dump_file = fopen(dump_path, "wb");
    if (!ctx.dump_file) {
      fprintf(stderr, "failed to open dump file: %s\n", dump_path);
      return 1;
    }
  }

  libmiplay_config_t cfg = {
      .receiver_name = "libmiplay-test",
      .bind_address = bind_addr,
      .control_port = 0,
      .audio_port = port,
      .socket_timeout_ms = 200,
  };

  libmiplay_callbacks_t cb = {
      .on_log = on_log,
      .on_state_changed = on_state,
      .on_audio_frame = on_audio,
      .on_error = on_error,
      .userdata = &ctx,
  };

  libmiplay_receiver_t *receiver = NULL;
  if (libmiplay_receiver_create(&cfg, &cb, &receiver) != LIBMIPLAY_OK) {
    fprintf(stderr, "failed to create receiver\n");
    if (ctx.dump_file) fclose(ctx.dump_file);
    return 1;
  }

  if (libmiplay_receiver_start(receiver) != LIBMIPLAY_OK) {
    fprintf(stderr, "failed to start receiver\n");
    libmiplay_receiver_destroy(receiver);
    if (ctx.dump_file) fclose(ctx.dump_file);
    return 1;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  if (simulate) {
    const uint8_t handshake[] = {1};
    const uint8_t audio[] = {2, 0, 0, 0, 0, 0, 0, 0, 7, 'P', 'C', 'M'};
    const uint8_t teardown[] = {3};
    (void)libmiplay_receiver_feed_packet(receiver, handshake, sizeof(handshake));
    (void)libmiplay_receiver_feed_packet(receiver, audio, sizeof(audio));
    (void)libmiplay_receiver_feed_packet(receiver, teardown, sizeof(teardown));
    g_stop = 1;
  }

  while (!g_stop) {
    sleep(1);
  }

  (void)libmiplay_receiver_stop(receiver);
  libmiplay_receiver_destroy(receiver);
  if (ctx.dump_file) fclose(ctx.dump_file);
  return 0;
}
