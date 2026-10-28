#include "miplay.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAGIC 0x24
#define MAX_PAYLOAD (4U * 1024U * 1024U)
#define DEFAULT_PORT 8899

struct miplay_server {
	char *name;
	char *bind_address;
	uint16_t requested_port;
	uint16_t port;
	miplay_event_cb event;
	miplay_audio_cb audio;
	miplay_log_cb log;
	void *userdata;
	int fd;
	int client_fd;
	int running;
	pthread_t thread;
	pthread_mutex_t lock;
};

static void report(miplay_server_t *s, int level, const char *message) {
	if (s->log) s->log(s->userdata, level, message);
}
static void event(miplay_server_t *s, miplay_event_t type,
                  const void *data, size_t length) {
	if (s->event) s->event(s->userdata, type, data, length);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | p[3];
}
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v;
}

int miplay_frame_encode(uint16_t command, uint16_t sequence, const void *payload,
                        size_t payload_length, uint8_t **out, size_t *out_length) {
	uint8_t *frame;
	if (!out || !out_length || payload_length > MAX_PAYLOAD ||
	    (payload_length && !payload)) return -1;
	frame = malloc(9 + payload_length);
	if (!frame) return -2;
	frame[0] = MAGIC; put16(frame + 1, command); put16(frame + 3, sequence);
	put32(frame + 5, (uint32_t)payload_length);
	if (payload_length) memcpy(frame + 9, payload, payload_length);
	*out = frame; *out_length = 9 + payload_length; return 0;
}

int miplay_frame_decode(const void *buffer, size_t length, miplay_frame_t *frame) {
	const uint8_t *p = buffer; uint32_t payload_length;
	if (!p || !frame || length < 9 || p[0] != MAGIC) return -1;
	payload_length = get32(p + 5);
	if (payload_length > MAX_PAYLOAD || length != 9 + payload_length) return -1;
	frame->command = get16(p + 1); frame->sequence = get16(p + 3);
	frame->payload = p + 9; frame->payload_length = payload_length; return 0;
}

int miplay_media_frame_decode(const void *buffer, size_t length,
                              const uint8_t **payload, size_t *payload_length) {
	const uint8_t *p = buffer; size_t n;
	if (!p || !payload || !payload_length || length < 4 || p[0] != MAGIC) return -1;
	n = ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | p[3];
	if (n > length - 4) return -1;
	*payload = p + 4; *payload_length = n; return 0;
}

const char *miplay_command_name(uint16_t c) {
	switch (c) {
	case 0x0000: return "OPEN"; case 0x0002: return "CLOSE";
	case 0x0004: return "PAUSE"; case 0x0006: return "RESUME";
	case 0x000c: return "SET_VOLUME"; case 0x000e: return "GET_VOLUME";
	case 0x0012: return "SET_MEDIA_INFO"; case 0x001a: return "HEARTBEAT";
	case 0x001c: return "GET_STATE"; case 0x001e: return "GET_DEVICE_INFO";
	case 0x0022: return "NOTIFY"; case 0x0028: return "LEGACY_CHALLENGE";
	case 0x0029: return "LEGACY_CHALLENGE_ACK"; case 0x0036: return "SOURCE_VERSION";
	case 0x0040: return "SET_PLAY_SOURCE"; case 0x0058: return "SET_LOCAL_DEVICE_INFO";
	case 0x1400: return "SAFETY_INFO"; case 0x1402: return "SAFETY_AUTH";
	default: return "UNKNOWN";
	}
}

static int send_frame(int fd, uint16_t command, uint16_t seq,
                      const void *payload, size_t length) {
	uint8_t *frame; size_t frame_length, sent = 0; int rc;
	rc = miplay_frame_encode(command, seq, payload, length, &frame, &frame_length);
	if (rc) return rc;
	while (sent < frame_length) {
		ssize_t n = send(fd, frame + sent, frame_length - sent, 0);
		if (n <= 0) { free(frame); return -1; }
		sent += (size_t)n;
	}
	free(frame); return 0;
}

static int legacy_response(const uint8_t *challenge, size_t n, char out[41]) {
	unsigned char md5[MD5_DIGEST_LENGTH], mac[EVP_MAX_MD_SIZE]; unsigned int mac_len;
	char key[MD5_DIGEST_LENGTH * 2 + 1]; size_t i;
	EVP_Digest("0.0.0.0", 7, md5, NULL, EVP_md5(), NULL);
	for (i = 0; i < sizeof(md5); ++i) snprintf(key + i * 2, 3, "%02x", md5[i]);
	key[32] = 0;
	HMAC(EVP_sha1(), key, 32, challenge, n, mac, &mac_len);
	for (i = 0; i < 20; ++i) snprintf(out + i * 2, 3, "%02x", mac[i]);
	out[40] = 0; return 0;
}

static void *client_thread(void *arg) {
	miplay_server_t *s = arg; uint8_t buf[8192], *pending = NULL;
	size_t pending_len = 0; int authenticated = 0, opened = 0;
	char challenge[] = "012345678901234"; uint16_t challenge_seq = 1;
	if (send_frame(s->client_fd, 0x0028, challenge_seq, challenge, 15)) goto done;
	event(s, MIPLAY_EVENT_CONNECTED, NULL, 0);
	for (;;) {
		ssize_t n = recv(s->client_fd, buf, sizeof(buf), 0);
		if (n <= 0) break;
		{ uint8_t *tmp = realloc(pending, pending_len + (size_t)n);
		  if (!tmp) { report(s, 3, "out of memory while buffering control frame"); break; }
		  pending = tmp; memcpy(pending + pending_len, buf, (size_t)n); pending_len += (size_t)n; }
		while (pending_len >= 9) {
			miplay_frame_t f; size_t total = 9 + get32(pending + 5);
			uint8_t *reply = NULL; size_t reply_len = 0;
			if (pending[0] != MAGIC || get32(pending + 5) > MAX_PAYLOAD) goto done;
			if (pending_len < total) break;
			if (miplay_frame_decode(pending, total, &f)) goto done;
			if (f.command == 0x0029 && !authenticated) {
				char expected[41]; legacy_response((const uint8_t *)challenge, 15, expected);
				if (f.sequence != challenge_seq || f.payload_length != 40 ||
				    memcmp(f.payload, expected, 40)) goto done;
				authenticated = 1; event(s, MIPLAY_EVENT_AUTHENTICATED, NULL, 0);
			} else if (!authenticated) goto done;
			else {
				uint16_t ack = (f.command == 0x000c) ? 0x000d :
					(f.command == 0x000e) ? 0x000f :
					(f.command == 0x001a) ? 0x001b :
					(f.command == 0x001c) ? 0x001d :
					(f.command == 0x0040) ? 0x0041 : 0xffff;
				switch (f.command) {
				case 0x0000: opened = 1; event(s, MIPLAY_EVENT_OPENED, f.payload, f.payload_length); break;
				case 0x0004: event(s, MIPLAY_EVENT_PAUSE, NULL, 0); break;
				case 0x0006: event(s, MIPLAY_EVENT_RESUME, NULL, 0); break;
				case 0x0002: goto done;
				case 0x000c: event(s, MIPLAY_EVENT_VOLUME, f.payload, f.payload_length); break;
				case 0x0012: event(s, MIPLAY_EVENT_METADATA, f.payload, f.payload_length); break;
				default: break;
				}
				if (ack != 0xffff) send_frame(s->client_fd, ack, f.sequence, NULL, 0);
			}
			memmove(pending, pending + total, pending_len - total); pending_len -= total;
			free(reply); (void)reply_len; (void)opened;
		}
	}
done:
	free(pending); close(s->client_fd); s->client_fd = -1;
	if (s->running) event(s, MIPLAY_EVENT_CLOSED, NULL, 0);
	return NULL;
}

static void *accept_thread(void *arg) {
	miplay_server_t *s = arg;
	while (s->running) {
		int fd = accept(s->fd, NULL, NULL);
		if (fd < 0) { if (errno == EINTR) continue; if (!s->running) break; report(s, 2, "accept failed"); continue; }
		if (s->client_fd >= 0) { close(fd); continue; }
		s->client_fd = fd;
		client_thread(s);
	}
	return NULL;
}

miplay_server_t *miplay_server_create(const miplay_config_t *c) {
	miplay_server_t *s;
	if (!c) return NULL;
	s = calloc(1, sizeof(*s)); if (!s) return NULL;
	s->name = strdup(c->name ? c->name : "MiPlay Receiver");
	s->bind_address = strdup(c->bind_address ? c->bind_address : "0.0.0.0");
	s->requested_port = c->control_port ? c->control_port : DEFAULT_PORT;
	s->event = c->event; s->audio = c->audio; s->log = c->log; s->userdata = c->userdata;
	s->fd = -1; s->client_fd = -1; pthread_mutex_init(&s->lock, NULL);
	if (!s->name || !s->bind_address) { miplay_server_destroy(s); return NULL; }
	return s;
}

int miplay_server_start(miplay_server_t *s) {
	struct sockaddr_in addr; int yes = 1; socklen_t len = sizeof(addr);
	if (!s || s->running) return -1;
	s->fd = socket(AF_INET, SOCK_STREAM, 0); if (s->fd < 0) return -1;
	setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons(s->requested_port);
	if (inet_pton(AF_INET, s->bind_address, &addr.sin_addr) != 1 || bind(s->fd, (struct sockaddr *)&addr, sizeof(addr)) ||
	    listen(s->fd, 4) || getsockname(s->fd, (struct sockaddr *)&addr, &len)) { close(s->fd); s->fd = -1; return -1; }
	s->port = ntohs(addr.sin_port); s->running = 1;
	if (pthread_create(&s->thread, NULL, accept_thread, s)) { s->running = 0; close(s->fd); s->fd = -1; return -1; }
	return 0;
}

int miplay_server_stop(miplay_server_t *s) {
	if (!s || !s->running) return 0;
	s->running = 0; shutdown(s->fd, SHUT_RDWR); close(s->fd); s->fd = -1;
	if (s->client_fd >= 0) shutdown(s->client_fd, SHUT_RDWR);
	pthread_join(s->thread, NULL); return 0;
}
void miplay_server_destroy(miplay_server_t *s) {
	if (!s) return;
	miplay_server_stop(s);
	free(s->name);
	free(s->bind_address);
	pthread_mutex_destroy(&s->lock); free(s);
}
uint16_t miplay_server_port(const miplay_server_t *s) { return s ? s->port : 0; }

int miplay_server_feed_media(miplay_server_t *s, const void *frame, size_t length) {
	const uint8_t *payload; size_t n;
	if (!s || miplay_media_frame_decode(frame, length, &payload, &n)) return -1;
	if (s->audio) s->audio(s->userdata, payload, n);
	event(s, MIPLAY_EVENT_AUDIO, payload, n); return 0;
}

int miplay_apps_data(const uint8_t id[16], uint16_t port, char *out, size_t out_length) {
	char uuid[37], json[100]; unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int n;
	char raw[256]; size_t raw_len;
	if (!id || !out || !port) return -1;
	snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	         id[0],id[1],id[2],id[3],id[4],id[5],id[6],id[7],id[8],id[9],id[10],id[11],id[12],id[13],id[14],id[15]);
	snprintf(json, sizeof(json), "{\"mico\":{\"device_id\":\"%s\"}}", uuid);
	memset(raw, 0, sizeof(raw)); ((uint8_t *)raw)[0] = 0x04; ((uint8_t *)raw)[1] = 0x83;
	put16((uint8_t *)raw + 2, port);
	EVP_Digest(id, 16, digest, &n, EVP_sha256(), NULL); raw[4] = (digest[0] & 0xfc) | 2;
	memcpy(raw + 5, digest + 1, 5); memcpy(raw + 25, json, strlen(json)); raw_len = 25 + strlen(json);
	/* appsData is most useful to callers as a TXT string; use OpenSSL's base64 encoder. */
	{ int encoded_len = 4 * ((int)(raw_len + 3) / 3); unsigned char container[256];
	  if (out_length < (size_t)encoded_len + 1) return -1;
	  container[0] = 0x81; container[1] = 0; container[2] = (unsigned char)raw_len; memcpy(container + 3, raw, raw_len);
	  EVP_EncodeBlock((unsigned char *)out, container, (int)raw_len + 3);
	}
	return 0;
}

const char *miplay_strerror(int error) {
	switch (error) { case 0: return "success"; case -1: return "invalid argument or frame"; case -2: return "out of memory"; default: return "unknown error"; }
}
