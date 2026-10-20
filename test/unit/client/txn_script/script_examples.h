#pragma once

/* The ten examples of doc/transaction-scripts.md, in order. */

static const char EXAMPLE_1[] =
    "template response\n"
    "    success  id == 0x7E8 and byte[1] == 0x50\n"
    "    timeout  50ms\n"
    "\n"
    "send 0x7E0 data 02 10 03 expect response\n";

static const char EXAMPLE_2[] =
    "REQUEST  = 0x7E0\n"
    "RESPONSE = 0x7E8\n"
    "\n"
    "template read-response\n"
    "    success  id == RESPONSE and byte[1] == 0x62\n"
    "    timeout  50ms\n"
    "\n"
    "send REQUEST data 03 22 F1 90 expect read-response where byte[2] == 0xF1 and byte[3] == 0x90\n"
    "send REQUEST data 03 22 F1 89 expect read-response where byte[2] == 0xF1 and byte[3] == 0x89\n";

static const char EXAMPLE_3[] =
    "template uds-response\n"
    "    success  id == 0x7E8 and byte[1] == 0x71\n"
    "    failure  id == 0x7E8 and byte[1] == 0x7F\n"
    "    pending  id == 0x7E8 and byte[1] == 0x7F and byte[3] == 0x78\n"
    "    on mismatch abort\n"
    "    timeout  2s\n"
    "\n"
    "send 0x7E0 data 04 31 01 FF 00 expect uds-response\n";

static const char EXAMPLE_4[] =
    "template any-ecu-response\n"
    "    success  id & 0x1FFFFF00 == 0x18DAF100 and byte[1] == 0x7E\n"
    "    timeout  100ms\n"
    "\n"
    "send 0x18DA10F1 data 02 3E 00 expect any-ecu-response\n";

static const char EXAMPLE_5[] =
    "template optional-wake-up\n"
    "    success  id == 0x7E8\n"
    "    timeout  200ms then continue\n"
    "\n"
    "send 0x7DF data 02 3E 80 gap 10ms\n"
    "send 0x7DF data 02 3E 80 gap 10ms\n"
    "send 0x7DF data 02 3E 80 expect optional-wake-up\n";

static const char EXAMPLE_6[] =
    "NODES = 0..15\n"
    "\n"
    "template alive\n"
    "    for node in NODES\n"
    "        success  id & 0x7F0 == 0x700 and id & 0x0F == node\n"
    "    timeout  500ms then continue\n"
    "\n"
    "send 0x7FF data 01 expect alive\n";

static const char EXAMPLE_7[] =
    "NODES = 0..15\n"
    "\n"
    "template configured\n"
    "    success  id & 0x7F0 == 0x580 and byte[0] == 0x60\n"
    "    timeout  100ms\n"
    "\n"
    "for node in NODES\n"
    "    send 0x600 + node data 2B 00 10 00 node expect configured where id == 0x580 + node\n";

static const char EXAMPLE_8[] =
    "FIRMWARE    = read(\"battery.bin\")\n"
    "FIRMWARE_ID = 0x123\n"
    "BATTERIES   = 0..31\n"
    "\n"
    "template batch-acknowledgement\n"
    "    for battery in BATTERIES\n"
    "        success  id & 0x7E0 == 0x700 and id & 0x1F == battery and byte[0] == 0x01\n"
    "    on mismatch abort\n"
    "    timeout  2s\n"
    "\n"
    "for block in chunks(FIRMWARE, 128)\n"
    "    for frame in chunks(block, 8)\n"
    "        send FIRMWARE_ID data frame\n"
    "    expect batch-acknowledgement where byte[1..2] == little_endian(sum16(block))\n";

static const char EXAMPLE_9[] =
    "exclusive\n"
    "\n"
    "FIRMWARE    = read(\"inverter.bin\")\n"
    "FIRMWARE_ID = 0x18FF5000\n"
    "INVERTERS   = 0..7\n"
    "VENDOR_CRC  = crc(width 16, polynomial 0x8005, initial 0xFFFF, reflect_input yes, reflect_output yes, final_xor 0x0000)\n"
    "\n"
    "template block-received\n"
    "    for inverter in INVERTERS\n"
    "        success  id & 0x1FFFFF00 == 0x18FF5100 and id & 0xFF == inverter and byte[0] == 0x01\n"
    "    on mismatch abort\n"
    "    timeout  1s\n"
    "\n"
    "template image-verified\n"
    "    for inverter in INVERTERS\n"
    "        success  id & 0x1FFFFF00 == 0x18FF5100 and id & 0xFF == inverter and byte[0] == 0x02\n"
    "    on mismatch abort\n"
    "    timeout  10s\n"
    "\n"
    "for block in chunks(FIRMWARE, 256)\n"
    "    for frame in chunks(block, 8, pad=0xFF)\n"
    "        send FIRMWARE_ID data frame gap 1ms\n"
    "    expect block-received where byte[1..2] == little_endian(VENDOR_CRC(block))\n"
    "\n"
    "send FIRMWARE_ID data 00 FF FF FF FF FF FF FF expect image-verified where byte[1..4] == big_endian(crc32(FIRMWARE))\n";

static const char EXAMPLE_10[] =
    "CELLS = 0..31\n"
    "\n"
    "template quorum\n"
    "    for 24 of cell in CELLS\n"
    "        success  id == 0x650 and byte[0] == 0xA5 and byte[2] == cell\n"
    "    timeout  1s\n"
    "\n"
    "send 0x640 data 01 expect quorum\n";
