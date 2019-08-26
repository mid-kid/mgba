#include <mgba/internal/gb/sio/mobile.h>

#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>

#include <pthread.h>
#include "libmobile/mobile.h"

#include "libmobile/debug_cmd.h"

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

static void *thread_mobile_loop(__attribute__((unused)) void *argp)
{
	for (;;) {
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

	// Make sure config file is at least MOBILE_CONFIG_DATA_SIZE bytes big
	for (int i = ftell(mobile_config); i < MOBILE_CONFIG_DATA_SIZE; i++) {
		fputc(0, mobile_config);
	}

	mobile_init();
	if (pthread_create(&mobile->thread, NULL, thread_mobile_loop, NULL) != 0) return false;

	return true;
}

void GBMobileDeinit(struct GBSIODriver* driver) {
	struct GBMobile* mobile = (struct GBMobile*) driver;
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
