#include <cest>

#include <cstring>

extern "C" {
#include "socketcand/domain/beacon_xml.h"
#include "socketcand/domain/interface_catalogue.h"
}

static InterfaceCatalogue catalogue;

describe("beacon_xml", []() {
    it("renders a CANBeacon with device, url and buses", []() {
        ListReplyMessage reply;
        char out[1024];
        size_t length;

        InterfaceCatalogue_Reset(&catalogue);
        memset(&reply, 0, sizeof(reply));
        reply.count = 1;
        reply.entries[0].interface_id = 7;
        strncpy(reply.entries[0].agent_name, "truck42", REGISTER_AGENT_NAME_SIZE - 1);
        strncpy(reply.entries[0].interface_name, "can0", REGISTER_INTERFACE_NAME_SIZE - 1);
        InterfaceCatalogue_AppendPage(&catalogue, &reply);

        length = BeaconXml_Render(out, sizeof(out), "host", "can://127.0.0.1:29536", &catalogue);

        expect(length > 0).toBe(true);
        expect(strstr(out, "<CANBeacon name=\"host\" type=\"SocketCAN\"") != nullptr).toBe(true);
        expect(strstr(out, "<URL>can://127.0.0.1:29536</URL>") != nullptr).toBe(true);
        expect(strstr(out, "<Bus name=\"truck42/can0\"/>") != nullptr).toBe(true);
        expect(strstr(out, "</CANBeacon>") != nullptr).toBe(true);
    });
});
