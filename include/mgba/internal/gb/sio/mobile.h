#ifndef GB_MOBILE_H
#define GB_MOBILE_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/gb/interface.h>

struct GBMobile {
	struct GBSIODriver d;

	pthread_t thread;
	uint8_t byte;
	uint8_t next;
};

void GBMobileCreate(struct GBMobile* mobile);

CXX_GUARD_END

#endif
