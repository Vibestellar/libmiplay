#ifndef LIBMIPLAY_MIPLAY_PROTOCOL_H
#define LIBMIPLAY_MIPLAY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  MIPLAY_PKT_HANDSHAKE = 1,
  MIPLAY_PKT_AUDIO_FRAME = 2,
  MIPLAY_PKT_TEARDOWN = 3
} miplay_packet_type_t;

typedef struct {
  miplay_packet_type_t type;
  uint64_t pts;
  const uint8_t *payload;
  size_t payload_len;
} miplay_packet_t;

int miplay_parse_packet(const uint8_t *buf, size_t len, miplay_packet_t *out);

#endif
