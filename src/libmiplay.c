#include "libmiplay/libmiplay.h"

#include "miplay_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

struct libmiplay_receiver {
  libmiplay_config_t config;
  libmiplay_callbacks_t callbacks;
  int sockfd;
  bool running;
  pthread_t io_thread;
  pthread_mutex_t state_lock;
  libmiplay_state_t state;
};

static void emit_log(libmiplay_receiver_t *r, libmiplay_log_level_t level, const char *msg) {
  if (r->callbacks.on_log) {
    r->callbacks.on_log(level, msg, r->callbacks.userdata);
  }
}

static void set_state(libmiplay_receiver_t *r, libmiplay_state_t state) {
  pthread_mutex_lock(&r->state_lock);
  r->state = state;
  pthread_mutex_unlock(&r->state_lock);
  if (r->callbacks.on_state_changed) {
    r->callbacks.on_state_changed(state, r->callbacks.userdata);
  }
}

static void emit_error(libmiplay_receiver_t *r, libmiplay_error_t err, const char *msg) {
  if (r->callbacks.on_error) {
    r->callbacks.on_error(err, msg, r->callbacks.userdata);
  }
}

static int handle_packet(libmiplay_receiver_t *r, const uint8_t *packet, size_t len) {
  miplay_packet_t parsed;
  int rc = miplay_parse_packet(packet, len, &parsed);
  if (rc != 0) {
    emit_error(r, rc == -ENOTSUP ? LIBMIPLAY_ERR_NOTSUP : LIBMIPLAY_ERR_INVAL,
               "unsupported or malformed packet");
    return LIBMIPLAY_ERR_INVAL;
  }

  switch (parsed.type) {
    case MIPLAY_PKT_HANDSHAKE:
      set_state(r, LIBMIPLAY_STATE_SESSION_NEGOTIATING);
      emit_log(r, LIBMIPLAY_LOG_INFO, "session negotiation packet received");
      return LIBMIPLAY_OK;
    case MIPLAY_PKT_AUDIO_FRAME:
      set_state(r, LIBMIPLAY_STATE_STREAMING);
      if (r->callbacks.on_audio_frame) {
        r->callbacks.on_audio_frame(parsed.payload, parsed.payload_len, parsed.pts,
                                    r->callbacks.userdata);
      }
      return LIBMIPLAY_OK;
    case MIPLAY_PKT_TEARDOWN:
      set_state(r, LIBMIPLAY_STATE_STOPPED);
      emit_log(r, LIBMIPLAY_LOG_INFO, "session teardown packet received");
      return LIBMIPLAY_OK;
  }

  return LIBMIPLAY_ERR_NOTSUP;
}

static int bind_socket(const libmiplay_config_t *cfg) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return -1;
  }

  struct timeval tv;
  int timeout_ms = cfg->socket_timeout_ms > 0 ? cfg->socket_timeout_ms : 200;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg->audio_port);

  if (cfg->bind_address && strlen(cfg->bind_address) > 0) {
    if (inet_pton(AF_INET, cfg->bind_address, &addr.sin_addr) != 1) {
      close(fd);
      return -1;
    }
  } else {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }

  return fd;
}

static void *io_loop(void *arg) {
  libmiplay_receiver_t *r = (libmiplay_receiver_t *)arg;
  uint8_t buffer[2048];

  while (1) {
    pthread_mutex_lock(&r->state_lock);
    bool should_run = r->running;
    pthread_mutex_unlock(&r->state_lock);
    if (!should_run) {
      break;
    }

    ssize_t n = recvfrom(r->sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
    if (n == 0) {
      continue;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      emit_error(r, LIBMIPLAY_ERR_IO, "socket receive failed");
      set_state(r, LIBMIPLAY_STATE_ERROR);
      break;
    }

    (void)handle_packet(r, buffer, (size_t)n);
  }

  return NULL;
}

const char *libmiplay_version_string(void) { return "0.1.0"; }

int libmiplay_receiver_create(const libmiplay_config_t *config,
                              const libmiplay_callbacks_t *callbacks,
                              libmiplay_receiver_t **out_receiver) {
  if (!config || !callbacks || !out_receiver || config->audio_port == 0) {
    return LIBMIPLAY_ERR_INVAL;
  }

  libmiplay_receiver_t *r = calloc(1, sizeof(*r));
  if (!r) {
    return LIBMIPLAY_ERR_NOMEM;
  }

  r->config = *config;
  r->callbacks = *callbacks;
  r->sockfd = -1;
  r->state = LIBMIPLAY_STATE_IDLE;
  pthread_mutex_init(&r->state_lock, NULL);

  *out_receiver = r;
  return LIBMIPLAY_OK;
}

int libmiplay_receiver_start(libmiplay_receiver_t *receiver) {
  if (!receiver) {
    return LIBMIPLAY_ERR_INVAL;
  }

  pthread_mutex_lock(&receiver->state_lock);
  if (receiver->running) {
    pthread_mutex_unlock(&receiver->state_lock);
    return LIBMIPLAY_ERR_STATE;
  }
  pthread_mutex_unlock(&receiver->state_lock);

  receiver->sockfd = bind_socket(&receiver->config);
  if (receiver->sockfd < 0) {
    emit_error(receiver, LIBMIPLAY_ERR_IO, "failed to bind audio socket");
    return LIBMIPLAY_ERR_IO;
  }

  pthread_mutex_lock(&receiver->state_lock);
  receiver->running = true;
  pthread_mutex_unlock(&receiver->state_lock);
  set_state(receiver, LIBMIPLAY_STATE_LISTENING);

  if (pthread_create(&receiver->io_thread, NULL, io_loop, receiver) != 0) {
    pthread_mutex_lock(&receiver->state_lock);
    receiver->running = false;
    pthread_mutex_unlock(&receiver->state_lock);
    close(receiver->sockfd);
    receiver->sockfd = -1;
    emit_error(receiver, LIBMIPLAY_ERR_IO, "failed to create io thread");
    return LIBMIPLAY_ERR_IO;
  }

  emit_log(receiver, LIBMIPLAY_LOG_INFO, "receiver started");
  return LIBMIPLAY_OK;
}

int libmiplay_receiver_stop(libmiplay_receiver_t *receiver) {
  if (!receiver) {
    return LIBMIPLAY_ERR_INVAL;
  }

  pthread_mutex_lock(&receiver->state_lock);
  bool was_running = receiver->running;
  receiver->running = false;
  pthread_mutex_unlock(&receiver->state_lock);

  if (!was_running) {
    return LIBMIPLAY_OK;
  }

  if (receiver->sockfd >= 0) {
    shutdown(receiver->sockfd, SHUT_RDWR);
  }
  pthread_join(receiver->io_thread, NULL);

  if (receiver->sockfd >= 0) {
    close(receiver->sockfd);
    receiver->sockfd = -1;
  }

  set_state(receiver, LIBMIPLAY_STATE_STOPPED);
  emit_log(receiver, LIBMIPLAY_LOG_INFO, "receiver stopped");
  return LIBMIPLAY_OK;
}

void libmiplay_receiver_destroy(libmiplay_receiver_t *receiver) {
  if (!receiver) {
    return;
  }
  (void)libmiplay_receiver_stop(receiver);
  pthread_mutex_destroy(&receiver->state_lock);
  free(receiver);
}

int libmiplay_receiver_feed_packet(libmiplay_receiver_t *receiver,
                                   const uint8_t *packet,
                                   size_t len) {
  if (!receiver || !packet || len == 0) {
    return LIBMIPLAY_ERR_INVAL;
  }
  return handle_packet(receiver, packet, len);
}
