#include "miplay.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void) {
	uint8_t *encoded; size_t encoded_len; miplay_frame_t frame;
	const uint8_t payload[] = {1, 2, 3};
	assert(miplay_frame_encode(0x001a, 7, payload, sizeof(payload), &encoded, &encoded_len) == 0);
	assert(encoded_len == 12 && miplay_frame_decode(encoded, encoded_len, &frame) == 0);
	assert(frame.command == 0x001a && frame.sequence == 7 &&
	       frame.payload_length == 3 && memcmp(frame.payload, payload, 3) == 0);
	free(encoded);
	{ const uint8_t media[] = {0x24, 0, 0, 2, 0xaa, 0xbb}; const uint8_t *p; size_t n;
	  assert(miplay_media_frame_decode(media, sizeof(media), &p, &n) == 0 && n == 2 && p[0] == 0xaa); }
	{ const uint8_t bad[] = {0x23, 0, 0, 0, 0}; assert(miplay_frame_decode(bad, sizeof(bad), &frame) != 0); }
	puts("protocol tests passed"); return 0;
}
