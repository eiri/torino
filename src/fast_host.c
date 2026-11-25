#include "host_link.h"
#include "tusb.h"
#include "pio_usb_ll.h"
#include "hardware/sync.h"
#include "timing.h"
#include "host/hcd.h"

atomic_uint endpoint_timing, host_port, discovery_retries, host_sessions;

void host_forget(void) {
    uint32_t interrupts = save_and_disable_interrupts();
    PIO_USB_ROOT_PORT(0)->suspended = true;
    restore_interrupts(interrupts);
    hcd_event_device_remove(1, false);
}

void host_discover(uint32_t now, bool idle) {
    static uint32_t since;
    root_port_t *root = PIO_USB_ROOT_PORT(0);
    uint32_t interrupts = save_and_disable_interrupts();
    port_pin_status_t line = pio_usb_bus_get_line_state(root);
    atomic_store(&host_port, (unsigned)line | (unsigned)root->connected << 8 |
        (unsigned)root->suspended << 9);
    restore_interrupts(interrupts);
    if (!idle || line != PORT_PIN_FS_IDLE) { since = now; return; }
    if (now - since < 2000) return;
    since = now;
    interrupts = save_and_disable_interrupts();
    root->is_fullspeed = true;
    root->connected = true;
    root->suspended = true;
    restore_interrupts(interrupts);
    tuh_edpt_abort_xfer(0, 0);
    hcd_device_close(1, 0);
    hcd_event_device_remove(1, false);
    hcd_event_device_attach(1, false);
    atomic_fetch_add(&discovery_retries, 1);
}

void host_tune_polling(uint8_t address, uint16_t vid, uint16_t pid) {
    uint32_t interrupts = save_and_disable_interrupts();
    for (unsigned i = 0; i < PIO_USB_EP_POOL_CNT; i++) {
        endpoint_t *ep = PIO_USB_ENDPOINT(i);
        if (ep->root_idx || ep->dev_addr != address || !(ep->ep_num & 0x80) || !(ep->ep_num & 15) || !ep->size) continue;
        uint8_t before = ep->interval;
        uint8_t after = host_interval(vid, pid, tuh_speed_get(address) == TUSB_SPEED_FULL,
            ep->ep_num, ep->attr & 3, ep->size, before);
        ep->interval = after;
        ep->interval_counter = 0;
        atomic_store(&endpoint_timing, after | (unsigned)before << 8 | (unsigned)(ep->attr & 3) << 16);
    }
    restore_interrupts(interrupts);
}
