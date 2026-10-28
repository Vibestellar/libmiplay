#include "miplay.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void on_signal(int sig) { (void)sig; stop = 1; }
static void log_message(void *u, int level, const char *message) {
	(void)u; fprintf(stderr, "[%d] %s\n", level, message);
}
static void on_event(void *u, miplay_event_t e, const void *data, size_t n) {
	(void)u; (void)data; printf("event=%d bytes=%zu\n", (int)e, n); fflush(stdout);
}
static void on_audio(void *u, const uint8_t *data, size_t n) {
	FILE *f = u; if (f) fwrite(data, 1, n, f);
}
int main(int argc, char **argv) {
	miplay_config_t c = {0}; miplay_server_t *s; const char *file = NULL;
	int opt, port = 8899; FILE *audio = NULL;
	while ((opt = getopt(argc, argv, "p:o:n:")) != -1) {
		if (opt == 'p') port = atoi(optarg);
		else if (opt == 'o') file = optarg;
		else if (opt == 'n') c.name = optarg;
		else return 2;
	}
	if (file && !(audio = fopen(file, "wb"))) { perror(file); return 1; }
	c.control_port = (uint16_t)port; c.event = on_event; c.log = log_message;
	c.audio = audio ? on_audio : NULL; c.userdata = audio;
	s = miplay_server_create(&c);
	if (!s || miplay_server_start(s)) { fprintf(stderr, "cannot start MiPlay receiver\n"); return 1; }
	printf("listening on TCP %u (legacy control; SafetyData/reverse-WFD unavailable)\n", miplay_server_port(s));
	signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
	while (!stop) sleep(1);
	miplay_server_destroy(s); if (audio) fclose(audio); return 0;
}
