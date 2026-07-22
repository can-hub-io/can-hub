#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>

int32_t ListenEndpoint_OpenSocket(int32_t socktype, int32_t *address_family);
bool ListenEndpoint_BuildSockaddr(int32_t address_family, const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length);
void ListenEndpoint_FormatOrigin(const struct sockaddr_storage *address, char *out, size_t size);
