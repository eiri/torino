#include "tusb.h"
#include <string.h>

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64,
#ifdef TORINO_RELAY
    .idVendor = 0x1ed8,
    .idProduct = 0x0037,
#else
    .idVendor = 0xcafe,
    .idProduct = 0x4011,
#endif
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device;
}

#ifdef TORINO_RELAY
static const uint8_t report_descriptor[] = {
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x09, 0x01,
    0x81, 0x02, 0x09, 0x01, 0x91, 0x02, 0xc0,
};
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return report_descriptor;
}
#endif

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    static const uint8_t config[] = {
#ifdef TORINO_RELAY
        TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
        TUD_HID_INOUT_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(report_descriptor), 0x01, 0x81, 64, 1),
        TUD_CDC_DESCRIPTOR(1, 0, 0x83, 8, 0x02, 0x82, 64),
#else
        TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
        TUD_CDC_DESCRIPTOR(0, 0, 0x81, 8, 0x02, 0x82, 64),
#endif
    };
    return config;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language) {
    (void)language;
    static uint16_t output[40];
    static const char *const strings[] = {
#ifdef TORINO_RELAY
        "", "Torino", "Mustang LT25", "torino-discovery-01",
#else
        "", "Torino", "Read-only Mustang host probe", "torino-host-probe-01",
#endif
    };
    size_t length;
    if (index == 0) {
        output[1] = 0x0409;
        length = 1;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        length = strlen(strings[index]);
        if (length > 39) return NULL;
        for (size_t i = 0; i < length; i++) output[i + 1] = (uint8_t)strings[index][i];
    }
    output[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return output;
}
