#ifndef MIPLAY_H
#define MIPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIPLAY_VERSION_MAJOR 0
#define MIPLAY_VERSION_MINOR 1
#define MIPLAY_VERSION_PATCH 0

typedef struct miplay_server miplay_server_t;

typedef enum {
	MIPLAY_EVENT_CONNECTED,
	MIPLAY_EVENT_AUTHENTICATED,
	MIPLAY_EVENT_OPENED,
	MIPLAY_EVENT_PLAY,
	MIPLAY_EVENT_PAUSE,
	MIPLAY_EVENT_RESUME,
	MIPLAY_EVENT_CLOSED,
	MIPLAY_EVENT_VOLUME,
	MIPLAY_EVENT_METADATA,
	MIPLAY_EVENT_AUDIO,
	MIPLAY_EVENT_ERROR
} miplay_event_t;

typedef void (*miplay_event_cb)(void *userdata, miplay_event_t event,
                                const void *data, size_t length);
typedef void (*miplay_log_cb)(void *userdata, int level, const char *message);
typedef void (*miplay_audio_cb)(void *userdata, const uint8_t *data,
                                size_t length);

typedef struct {
	const char *name;
	const char *bind_address;
	uint16_t control_port; /* 0 selects an ephemeral port, normally 8899. */
	uint16_t discovery_port; /* Reserved for future mDNS responder; defaults to 56666. */
	miplay_event_cb event;
	miplay_audio_cb audio;
	miplay_log_cb log;
	void *userdata;
} miplay_config_t;

typedef struct {
	uint16_t command;
	uint16_t sequence;
	const uint8_t *payload;
	size_t payload_length;
} miplay_frame_t;

/* Creates an inactive receiver. The config strings are copied. */
miplay_server_t *miplay_server_create(const miplay_config_t *config);
int miplay_server_start(miplay_server_t *server);
int miplay_server_stop(miplay_server_t *server);
void miplay_server_destroy(miplay_server_t *server);
uint16_t miplay_server_port(const miplay_server_t *server);

/* Strict command-frame codec. A frame is owned by the caller. */
int miplay_frame_encode(uint16_t command, uint16_t sequence,
                        const void *payload, size_t payload_length,
                        uint8_t **out, size_t *out_length);
int miplay_frame_decode(const void *buffer, size_t length,
                        miplay_frame_t *frame);
const char *miplay_command_name(uint16_t command);

/* Validates and extracts the 0x24 + three-byte-length media framing. */
int miplay_media_frame_decode(const void *buffer, size_t length,
                              const uint8_t **payload, size_t *payload_length);

/* Constructs the advertised appsData TXT value. out is NUL terminated. */
int miplay_apps_data(const uint8_t device_id[16], uint16_t control_port,
                     char *out, size_t out_length);

/* Feeds a complete media frame to the configured callback (offline/testing). */
int miplay_server_feed_media(miplay_server_t *server, const void *frame,
                             size_t length);

const char *miplay_strerror(int error);

#ifdef __cplusplus
}
#endif
#endif
