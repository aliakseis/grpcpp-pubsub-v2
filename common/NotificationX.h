#pragma once

#include <string>
#include <stdint.h>

#define NOTIFICATION_X(macro) \
	macro(uint64_t, index) \
	macro(std::string, content)

struct PlainNotification {
#define DECL_MACRO(type, name) type name;
    NOTIFICATION_X(DECL_MACRO)
#undef DECL_MACRO
};
