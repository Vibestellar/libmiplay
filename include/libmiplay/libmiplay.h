#ifndef LIBMIPLAY_LIBMIPLAY_H
#define LIBMIPLAY_LIBMIPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMIPLAY_VERSION_MAJOR 0
#define LIBMIPLAY_VERSION_MINOR 1
#define LIBMIPLAY_VERSION_PATCH 0

typedef struct libmiplay_receiver libmiplay_receiver_t;

typedef enum {
  LIBMIPLAY_OK = 0,
  LIBMIPLAY_ERR_INVAL = -1,
  LIBMIPLAY_ERR_NOMEM = -2,
  LIBMIPLAY_ERR_IO = -3,
  LIBMIPLAY_ERR_STATE = -4,
  LIBMIPLAY_ERR_NOTSUP = -5
} libmiplay_error_t;

typedef enum {
  LIBMIPLAY_LOG_ERROR = 0,
  LIBMIPLAY_LOG_WARN = 1,
  LIBMIPLAY_LOG_INFO = 2,
  LIBMIPLAY_LOG_DEBUG = 3
} libmiplay_log_level_t;

typedef enum {
  LIBMIPLAY_STATE_IDLE = 0,
  LIBMIPLAY_STATE_LISTENING = 1,
  LIBMIPLAY_STATE_SESSION_NEGOTIATING = 2,
  LIBMIPLAY_STATE_STREAMING = 3,
  LIBMIPLAY_STATE_STOPPED = 4,
  LIBMIPLAY_STATE_ERROR = 5
} libmiplay_state_t;

typedef struct {
  const char *receiver_name;
  const char *bind_address;
  uint16_t control_port;
  uint16_t audio_port;
  int socket_timeout_ms;
} libmiplay_config_t;

typedef struct {
  void (*on_log)(libmiplay_log_level_t level, const char *message, void *userdata);
  void (*on_state_changed)(libmiplay_state_t state, void *userdata);
  void (*on_audio_frame)(const uint8_t *data, size_t len, uint64_t pts, void *userdata);
  void (*on_error)(libmiplay_error_t error, const char *message, void *userdata);
  void *userdata;
} libmiplay_callbacks_t;

const char *libmiplay_version_string(void);
int libmiplay_receiver_create(const libmiplay_config_t *config,
                              const libmiplay_callbacks_t *callbacks,
                              libmiplay_receiver_t **out_receiver);
int libmiplay_receiver_start(libmiplay_receiver_t *receiver);
int libmiplay_receiver_stop(libmiplay_receiver_t *receiver);
void libmiplay_receiver_destroy(libmiplay_receiver_t *receiver);

/* For offline protocol tests and simulated sender input. */
int libmiplay_receiver_feed_packet(libmiplay_receiver_t *receiver,
                                   const uint8_t *packet,
                                   size_t len);

#ifdef __cplusplus
}
#endif

#endif
