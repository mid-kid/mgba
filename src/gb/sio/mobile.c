#include <mgba/internal/gb/sio/mobile.h>

#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>

#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "libmobile/mobile.h"

#include "libmobile/debug_cmd.h"

int mobile_socket;
FILE *mobile_config;

bool mobile_board_config_read(unsigned char *dest, const uintptr_t offset, const size_t size)
{
	fseek(mobile_config, offset, SEEK_SET);
	return fread(dest, 1, size, mobile_config) == size;
}

bool mobile_board_config_write(const unsigned char *src, const uintptr_t offset, const size_t size)
{
	fseek(mobile_config, offset, SEEK_SET);
	return fwrite(src, 1, size, mobile_config) == size;
}

bool mobile_board_tcp_connect(const char *host, const char *port)
{
	fprintf(stderr, "Connecting to: %s:%s\n", host, port);

	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	struct addrinfo *result;
	int gai_errno = getaddrinfo(host, port, &hints, &result);
	if (gai_errno) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_errno));
		return false;
	}

	int sock;
	struct addrinfo *info;
	for (info = result; info != NULL; info = info->ai_next) {
		sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (sock == -1) continue;
		if (connect(sock, info->ai_addr, info->ai_addrlen) != -1) break;
		close(sock);
	}
	freeaddrinfo(result);
	if (!info) {
		fprintf(stderr, "Could not connect\n");
		return false;
	}

	mobile_socket = sock;
	return true;
}

bool mobile_board_tcp_listen(const char *port)
{
	if (!mobile_socket) {
		struct addrinfo hints = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_flags = AI_PASSIVE
		};
		struct addrinfo *result;
		int gai_errno = getaddrinfo(NULL, port, &hints, &result);
		if (gai_errno != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_errno));
			return false;
		}

		int sock;
		struct addrinfo *info;
		for (info = result; info != NULL; info = info->ai_next) {
			sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
			if (sock == -1) continue;
			if (bind(sock, info->ai_addr, info->ai_addrlen) == 0) break;
			close(sock);
		}
		freeaddrinfo(result);
		if (info == NULL) {
			fprintf(stderr, "Could not bind\n");
			return false;
		}

		int flags = fcntl(sock, F_GETFL, 0);
		if (flags != -1) flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
		if (flags == -1) {
			perror("fcntl");
			close(sock);
			return false;
		}

		if (listen(sock, 1) == -1) {
			perror("listen");
			close(sock);
			return false;
		}

		mobile_socket = sock;
	}

	int sock = accept(mobile_socket, NULL, NULL);
	if (sock == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
		perror("accept");
		return false;
	}
	close(mobile_socket);
	mobile_socket = sock;

	return true;
}

void mobile_board_tcp_disconnect(void)
{
	close(mobile_socket);
	mobile_socket = 0;
}

bool mobile_board_tcp_send(const void *data, const unsigned size)
{
	if (send(mobile_socket, data, size, 0) == -1) {
		perror("send");
		return false;
	}
	return true;
}

int mobile_board_tcp_receive(void *data)
{
	ssize_t len = recv(mobile_socket, data, MOBILE_MAX_TCP_SIZE, MSG_DONTWAIT);
	if (len != -1) return len;
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
		perror("recv");
		return -1;
	}
	return 0;
}

pthread_mutex_t thread_mobile_loop_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *thread_mobile_loop(__attribute__((unused)) void *argp)
{
	for (;;) {
		// TODO: Use a Mutex.
		usleep(50000);
		mobile_loop();
		fflush(stdout);
	}

	return NULL;
}

bool GBMobileInit(struct GBSIODriver* driver) {
	struct GBMobile* mobile = (struct GBMobile*) driver;

	mobile->byte = 0;
	mobile->next = 0xD2;

	const char *fname_config = "config.bin";
	mobile_config = fopen(fname_config, "r+b");
	if (!mobile_config) mobile_config = fopen(fname_config, "w+b");
	if (!mobile_config) return false;
	fseek(mobile_config, 0, SEEK_END);

	mobile_socket = 0;

	// Make sure config file is at least MOBILE_CONFIG_DATA_SIZE bytes big
	for (int i = ftell(mobile_config); i < MOBILE_CONFIG_DATA_SIZE; i++) {
		fputc(0, mobile_config);
	}

	//uint32_t mgba_clock = mTimingCurrentTime(driver->p->p->timing);

	mobile_init();
	if (pthread_create(&mobile->thread, NULL, thread_mobile_loop, NULL) != 0) return false;

	return true;
}

void GBMobileDeinit(struct GBSIODriver* driver) {
	struct GBMobile* mobile = (struct GBMobile*) driver;
	if (mobile_socket) close(mobile_socket);
	fclose(mobile_config);
	pthread_cancel(mobile->thread);
}

static void GBMobileWriteSB(struct GBSIODriver* driver, uint8_t value) {
	struct GBMobile* mobile = (struct GBMobile*) driver;
	mobile->byte = mobile->next;
	mobile->next = value;
}

static uint8_t GBMobileWriteSC(struct GBSIODriver* driver, uint8_t value) {
	struct GBMobile* mobile = (struct GBMobile*) driver;
	if ((value & 0x81) == 0x81) {
		driver->p->pendingSB = mobile_transfer(mobile->byte);
	}
	return value;
}

void GBMobileCreate(struct GBMobile* mobile) {
	mobile->d.init = GBMobileInit;
	mobile->d.deinit = GBMobileDeinit;
	mobile->d.writeSB = GBMobileWriteSB;
	mobile->d.writeSC = GBMobileWriteSC;
}
