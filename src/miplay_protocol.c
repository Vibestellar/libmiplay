#include "miplay_protocol.h"

#include <errno.h>
#include <string.h>

static uint64_t read_u64_be(const uint8_t *in) {
  uint64_t v = 0;
  for (size_t i = 0; i < 8; i++) {
    v = (v << 8) | in[i];
  }
  return v;
}

int miplay_parse_packet(const uint8_t *buf, size_t len, miplay_packet_t *out) {
  if (!buf || !out || len < 1) {
    return -EINVAL;
  }

  memset(out, 0, sizeof(*out));

  switch (buf[0]) {
    case MIPLAY_PKT_HANDSHAKE:
      out->type = MIPLAY_PKT_HANDSHAKE;
      return 0;
    case MIPLAY_PKT_AUDIO_FRAME:
      if (len < 9) {
        return -EINVAL;
      }
      out->type = MIPLAY_PKT_AUDIO_FRAME;
      out->pts = read_u64_be(&buf[1]);
      out->payload = &buf[9];
      out->payload_len = len - 9;
      return 0;
    case MIPLAY_PKT_TEARDOWN:
      out->type = MIPLAY_PKT_TEARDOWN;
      return 0;
    default:
      return -ENOTSUP;
  }
}
