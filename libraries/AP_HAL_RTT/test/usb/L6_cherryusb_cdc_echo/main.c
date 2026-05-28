/*
 * L7 — CherryUSB CDC ACM echo PoC on CUAV V5 OTG_FS (isolated from hal_usb_lld_rtt).
 */
#include "test_runner.h"
#include "usb_dc_glue.h"

void ap_rtt_iwdg_kick(void);

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#define L7_USB_OTG_FS_BASE  0x50000000UL

#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0x1209
#define USBD_PID           0x5741
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#define CDC_MAX_MPS     64

static const uint8_t l7_cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
    USB_LANGID_INIT(USBD_LANGID_STRING),
    0x10, USB_DESCRIPTOR_TYPE_STRING,
    'P', 0x00, 'o', 0x00, 'g', 0x00, 'o', 0x00, 'A', 0x00, 'P', 0x00, 'M', 0x00,
    0x1A, USB_DESCRIPTOR_TYPE_STRING,
    'L', 0x00, '7', 0x00, ' ', 0x00, 'C', 0x00, 'h', 0x00, 'e', 0x00, 'r', 0x00,
    'r', 0x00, 'y', 0x00, 'U', 0x00, 'S', 0x00, 'B', 0x00,
    0x0A, USB_DESCRIPTOR_TYPE_STRING,
    '0', 0x00, '0', 0x00, '0', 0x00, '1', 0x00,
    0x00
};

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t read_buffer[CDC_MAX_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t write_buffer[CDC_MAX_MPS];

static volatile uint8_t l7_configured;

static void l7_usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        l7_configured = 1;
        usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, sizeof(read_buffer));
        break;
    default:
        break;
    }
}

void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    if (nbytes > sizeof(write_buffer)) {
        nbytes = sizeof(write_buffer);
    }
    for (uint32_t i = 0; i < nbytes; i++) {
        write_buffer[i] = read_buffer[i];
    }
    usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, nbytes);
    usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, sizeof(read_buffer));
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
}

static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

static void l7_cdc_init(void)
{
    usbd_desc_register(0, l7_cdc_descriptor);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf1));
    usbd_add_endpoint(0, &cdc_out_ep);
    usbd_add_endpoint(0, &cdc_in_ep);
    usbd_initialize(0, L7_USB_OTG_FS_BASE, l7_usbd_event_handler);
}

int main(void)
{
    uint32_t wait_ms = 0;

    TEST_INIT("L7_CHERRYUSB_CDC");

    TEST_STEP("OTG_FS clock/GPIO + soft disconnect");
    l7_usb_hw_preinit();
    TEST_PASS();

    TEST_STEP("CherryUSB CDC ACM init @ 0x50000000");
    l7_cdc_init();
    TEST_PASS();

    TEST_STEP("Wait for USB configured (host must open CDC port)");
    while (!l7_configured && wait_ms < 15000U) {
        rt_thread_mdelay(10);
        wait_ms += 10;
    }
    TEST_ASSERT(l7_configured != 0, "USB not configured — connect host USB and open ACM");

    test_printf("L7: CDC echo active — send bytes on /dev/ttyACM* (VID=0x1209 PID=0x5741)\r\n");

    while (1) {
        rt_thread_mdelay(1000);
        ap_rtt_iwdg_kick();
    }

    TEST_DONE();
    return 0;
}
