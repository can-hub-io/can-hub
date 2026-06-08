#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/frame_message.h"

/*
 * Translates a single socketcand ASCII command (the inner text handed out by
 * AsciiFramer, brackets excluded) to a tagged struct, and renders the outbound
 * lines the server sends back. Pure string<->struct, freestanding, no stdio:
 * all hex/decimal conversion is done by hand.
 *
 * Bus name holds the namespaced interface "agent_name/interface_name".
 */

#define SOCKETCAND_BUS_NAME_SIZE 160

typedef enum tsocketcand_command_e {
    kSOCKETCAND_COMMAND_UNKNOWN = 0,
    kSOCKETCAND_COMMAND_OPEN,
    kSOCKETCAND_COMMAND_RAWMODE,
    kSOCKETCAND_COMMAND_BCMMODE,
    kSOCKETCAND_COMMAND_SEND,
    kSOCKETCAND_COMMAND_ECHO,
    kSOCKETCAND_COMMAND_MAX,
} TSOCKETCAND_COMMAND;

typedef struct {
    uint8_t kind;
    char bus[SOCKETCAND_BUS_NAME_SIZE];
    FrameMessage frame;
} SocketcandCommand;

bool SocketcandCodec_Parse(const char *text, size_t length, SocketcandCommand *command);

size_t SocketcandCodec_RenderGreeting(char *out, size_t out_size);
size_t SocketcandCodec_RenderOk(char *out, size_t out_size);
size_t SocketcandCodec_RenderEcho(char *out, size_t out_size);
size_t SocketcandCodec_RenderError(char *out, size_t out_size, const char *detail);
size_t SocketcandCodec_RenderFrame(char *out, size_t out_size, const FrameMessage *frame);
