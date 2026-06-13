#pragma once

#include <assert.h>

#include "protocol/admin_message.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/register_message.h"
#include "protocol/subscribe_message.h"

#define SUBSCRIBE_WIRE_SIZE (MESSAGE_HEADER_SIZE + SUBSCRIBE_FIXED_FIELDS_SIZE + SUBSCRIBE_FILTERS_MAX * CAN_FILTER_SIZE)
#define FRAME_WIRE_SIZE (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)

#define CONTROL_MESSAGE_MAX_WIRE_SIZE (MESSAGE_HEADER_SIZE + REGISTER_BODY_SIZE)

static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + HELLO_BODY_SIZE, "HELLO exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + REGISTER_BODY_SIZE, "REGISTER exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + REGISTER_ACK_BODY_SIZE, "REGISTER_ACK exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ERROR_BODY_SIZE, "ERROR exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + OPEN_BODY_SIZE, "OPEN exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + OPEN_ACK_BODY_SIZE, "OPEN_ACK exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + CLOSE_BODY_SIZE, "CLOSE exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + LIST_BODY_SIZE, "LIST exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE, "IFCONFIG exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE, "IFCONFIG_REPLY exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_BODY_SIZE, "ADMIN_IFCONFIG exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_PEERS_BODY_SIZE, "ADMIN_PEERS exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_KICK_BODY_SIZE, "ADMIN_KICK exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_PINS_BODY_SIZE, "ADMIN_PINS exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_FORGET_BODY_SIZE, "ADMIN_FORGET exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_PIN_ADD_BODY_SIZE, "ADMIN_PIN_ADD exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_ACL_SET_BODY_SIZE, "ADMIN_ACL_SET exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_ACL_REVOKE_BODY_SIZE, "ADMIN_ACL_REVOKE exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_ACL_LIST_BODY_SIZE, "ADMIN_ACL_LIST exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_KICK_PEER_BODY_SIZE, "ADMIN_KICK_PEER exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_AGENTS_BODY_SIZE, "ADMIN_AGENTS exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_CLIENTS_BODY_SIZE, "ADMIN_CLIENTS exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= MESSAGE_HEADER_SIZE + ADMIN_INTERFACES_BODY_SIZE, "ADMIN_INTERFACES exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= SUBSCRIBE_WIRE_SIZE, "SUBSCRIBE exceeds control buffer");
static_assert(CONTROL_MESSAGE_MAX_WIRE_SIZE >= FRAME_WIRE_SIZE, "FRAME exceeds control buffer");
