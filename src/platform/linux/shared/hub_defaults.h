#ifndef PLATFORM_LINUX_SHARED_HUB_DEFAULTS_H
#define PLATFORM_LINUX_SHARED_HUB_DEFAULTS_H

/* Defaults shared between the hub listeners and the local consumers. */

#define HUB_DEFAULT_PORT_TEXT "7227"
#define HUB_DEFAULT_UNIX_SOCKET_DIRECTORY "/run/can-hub"
#define HUB_DEFAULT_UNIX_SOCKET_PATH HUB_DEFAULT_UNIX_SOCKET_DIRECTORY "/hub.sock"

#endif
