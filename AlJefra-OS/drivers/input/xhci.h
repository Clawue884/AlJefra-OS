/* SPDX-License-Identifier: MIT */
/* AlJefra OS — xHCI (USB 3.0) Host Controller Driver
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_XHCI_H
#define ALJEFRA_DRV_XHCI_H

#include "../../hal/hal.h"

/* ── Capability Register offsets (from BAR0) ── */
#define XHCI_CAP_CAPLENGTH   0x00    /* Capability Register Length (8-bit) */
#define XHCI_CAP_HCIVERSION  0x02    /* Interface Version Number (16-bit) */
#define XHCI_CAP_HCSPARAMS1  0x04    /* Structural Parameters 1 (32-bit) */
#define XHCI_CAP_HCSPARAMS2  0x08    /* Structural Parameters 2 (32-bit) */
#define XHCI_CAP_HCSPARAMS3  0x0C    /* Structural Parameters 3 (32-bit) */
#define XHCI_CAP_HCCPARAMS1  0x10    /* Capability Parameters 1 (32-bit) */
#define XHCI_CAP_DBOFF       0x14    /* Doorbell Offset (32-bit) */
#define XHCI_CAP_RTSOFF      0x18    /* Runtime Register Space Offset (32-bit) */
#define XHCI_CAP_HCCPARAMS2  0x1C    /* Capability Parameters 2 (32-bit) */

/* ── Operational Register offsets (from cap_base + CAPLENGTH) ── */
#define XHCI_OP_USBCMD       0x00    /* USB Command (32-bit) */
#define XHCI_OP_USBSTS       0x04    /* USB Status (32-bit) */
#define XHCI_OP_PAGESIZE     0x08    /* Page Size (32-bit) */
#define XHCI_OP_DNCTRL       0x14    /* Device Notification Control (32-bit) */
#define XHCI_OP_CRCR         0x18    /* Command Ring Control (64-bit) */
#define XHCI_OP_DCBAAP       0x30    /* Device Context Base Address Array Pointer (64-bit) */
#define XHCI_OP_CONFIG       0x38    /* Configure (32-bit) */
#define XHCI_OP_PORTSC_BASE  0x400   /* Port Status and Control (port n at +0x400 + 0x10*n) */

/* ── USBCMD bits ── */
#define XHCI_CMD_RUN          (1u << 0)   /* Run/Stop */
#define XHCI_CMD_HCRST        (1u << 1)   /* Host Controller Reset */
#define XHCI_CMD_INTE         (1u << 2)   /* Interrupter Enable */
#define XHCI_CMD_HSEE         (1u << 3)   /* Host System Error Enable */

/* ── USBSTS bits ── */
#define XHCI_STS_HCH          (1u << 0)   /* Host Controller Halted */
#define XHCI_STS_HSE          (1u << 2)   /* Host System Error */
#define XHCI_STS_EINT         (1u << 3)   /* Event Interrupt */
#define XHCI_STS_PCD          (1u << 4)   /* Port Change Detect */
#define XHCI_STS_CNR          (1u << 11)  /* Controller Not Ready */

/* ── PORTSC bits ── */
#define XHCI_PORTSC_CCS       (1u << 0)   /* Current Connect Status */
#define XHCI_PORTSC_PED       (1u << 1)   /* Port Enabled/Disabled */
#define XHCI_PORTSC_OCA       (1u << 3)   /* Over-current Active */
#define XHCI_PORTSC_PR        (1u << 4)   /* Port Reset */
#define XHCI_PORTSC_PLS_MASK  (0xFu << 5) /* Port Link State */
#define XHCI_PORTSC_PP        (1u << 9)   /* Port Power */
#define XHCI_PORTSC_SPEED_MASK (0xFu << 10) /* Port Speed */
#define XHCI_PORTSC_CSC       (1u << 17)  /* Connect Status Change */
#define XHCI_PORTSC_PRC       (1u << 21)  /* Port Reset Change */
#define XHCI_PORTSC_WRC       (1u << 19)  /* Warm Port Reset Change */

/* Port speeds */
#define XHCI_SPEED_FULL   1
#define XHCI_SPEED_LOW    2
#define XHCI_SPEED_HIGH   3
#define XHCI_SPEED_SUPER  4

/* ── TRB (Transfer Request Block) — 16 bytes ── */
typedef struct __attribute__((packed)) {
    uint64_t param;         /* Parameter (address, etc.) */
    uint32_t status;        /* Status / Transfer Length / Interrupter */
    uint32_t control;       /* Control: TRB Type [15:10], Cycle [0], etc. */
} xhci_trb_t;

/* ── TRB Types ── */
#define XHCI_TRB_NORMAL          1
#define XHCI_TRB_SETUP           2
#define XHCI_TRB_DATA            3
#define XHCI_TRB_STATUS          4
#define XHCI_TRB_LINK            6
#define XHCI_TRB_NOOP            8
#define XHCI_TRB_ENABLE_SLOT     9
#define XHCI_TRB_DISABLE_SLOT    10
#define XHCI_TRB_ADDRESS_DEVICE  11
#define XHCI_TRB_CONFIG_EP       12
#define XHCI_TRB_EVALUATE_CTX    13
#define XHCI_TRB_RESET_EP        14
#define XHCI_TRB_STOP_EP         15
#define XHCI_TRB_SET_TR_DEQUEUE  16
#define XHCI_TRB_NOOP_CMD        23
/* Event TRB types */
#define XHCI_TRB_TRANSFER_EVENT  32
#define XHCI_TRB_CMD_COMPLETE    33
#define XHCI_TRB_PORT_STATUS     34

/* TRB control field helpers */
#define XHCI_TRB_TYPE(t)  (((uint32_t)(t) & 0x3F) << 10)
#define XHCI_TRB_GET_TYPE(c)  (((c) >> 10) & 0x3F)
#define XHCI_TRB_CYCLE    (1u << 0)
#define XHCI_TRB_IOC      (1u << 5)   /* Interrupt on Completion */
#define XHCI_TRB_IDT      (1u << 6)   /* Immediate Data */
#define XHCI_TRB_BSR      (1u << 9)   /* Block Set Address Request */
#define XHCI_TRB_DIR_IN   (1u << 16)  /* Direction: IN (device to host) */

/* Completion codes */
#define XHCI_CC_SUCCESS       1
#define XHCI_CC_SHORT_PKT     13

/* ── Slot Context (32 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t field1;    /* Route String [19:0], Speed [23:20], MTT [25], Hub [26], Context Entries [31:27] */
    uint32_t field2;    /* Max Exit Latency [15:0], Root Hub Port Number [23:16], Num Ports [31:24] */
    uint32_t field3;    /* TT Hub Slot ID, TT Port Number, TTT, Interrupter Target */
    uint32_t field4;    /* USB Device Address [7:0], Slot State [31:27] */
    uint32_t rsvd[4];
} xhci_slot_ctx_t;

/* ── Endpoint Context (32 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t field1;    /* EP State [2:0], Mult [9:8], MaxPStreams [14:10], LSA [15], Interval [23:16], Max ESIT Payload Hi [31:24] */
    uint32_t field2;    /* CErr [2:1], EP Type [5:3], HID [7], Max Burst Size [15:8], Max Packet Size [31:16] */
    uint64_t dequeue;   /* TR Dequeue Pointer [63:4], DCS [0] */
    uint32_t field4;    /* Average TRB Length [15:0], Max ESIT Payload Lo [31:16] */
    uint32_t rsvd[3];
} xhci_ep_ctx_t;

/* Endpoint types */
#define XHCI_EP_CONTROL       4
#define XHCI_EP_ISOCH_OUT     1
#define XHCI_EP_BULK_OUT      2
#define XHCI_EP_INTERRUPT_OUT 3
#define XHCI_EP_ISOCH_IN      5
#define XHCI_EP_BULK_IN       6
#define XHCI_EP_INTERRUPT_IN  7

/* ── Input Context (for Address/Configure commands) ── */
typedef struct __attribute__((packed)) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t rsvd[6];
} xhci_input_ctrl_ctx_t;

/* ── Event Ring Segment Table Entry ── */
typedef struct __attribute__((packed)) {
    uint64_t base;      /* Ring Segment Base Address */
    uint32_t size;      /* Ring Segment Size (entries) */
    uint32_t rsvd;
} xhci_erst_entry_t;

/* ── Runtime register offsets (from rts_base) ── */
#define XHCI_RT_IMAN(n)      (0x20 + 0x20 * (n))       /* Interrupter Management */
#define XHCI_RT_IMOD(n)      (0x24 + 0x20 * (n))       /* Interrupter Moderation */
#define XHCI_RT_ERSTSZ(n)    (0x28 + 0x20 * (n))       /* Event Ring Segment Table Size */
#define XHCI_RT_ERSTBA(n)    (0x30 + 0x20 * (n))       /* ERST Base Address (64-bit) */
#define XHCI_RT_ERDP(n)      (0x38 + 0x20 * (n))       /* Event Ring Dequeue Pointer (64-bit) */

/* ── USB Standard Descriptors ── */
#define USB_DESC_DEVICE       1
#define USB_DESC_CONFIG       2
#define USB_DESC_STRING       3
#define USB_DESC_INTERFACE    4
#define USB_DESC_ENDPOINT     5
#define USB_DESC_HID          0x21
#define USB_DESC_HID_REPORT   0x22

/* ── USB Device Class Codes ── */
#define USB_CLASS_HID         0x03

/* ── USB Standard Requests ── */
#define USB_REQ_GET_STATUS     0x00
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIG     0x09
#define USB_REQ_SET_INTERFACE  0x0B
#define USB_REQ_SET_PROTOCOL   0x0B  /* HID class-specific */
#define USB_REQ_SET_IDLE       0x0A  /* HID class-specific */

/* ── USB Device Descriptor (18 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

/* ── USB Configuration Descriptor (9 bytes, followed by interfaces/endpoints) ── */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

/* ── USB Interface Descriptor (9 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_desc_t;

/* ── USB Endpoint Descriptor (7 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_desc_t;

/* ── Constants ── */
#define XHCI_MAX_SLOTS       32
#define XHCI_MAX_PORTS       16
#define XHCI_CMD_RING_SIZE   32
#define XHCI_EVT_RING_SIZE   64
#define XHCI_XFER_RING_SIZE  32

/* ── Per-device (slot) state ── */
typedef struct {
    bool              active;
    uint8_t           slot_id;
    uint8_t           port;
    uint8_t           speed;        /* XHCI_SPEED_* */
    uint16_t          max_packet;   /* EP0 max packet size */
    usb_device_desc_t dev_desc;
    /* Transfer ring for EP0 (control) */
    xhci_trb_t       *ep0_ring;
    uint64_t           ep0_ring_phys;
    uint16_t           ep0_enqueue;
    uint8_t            ep0_cycle;
    /* Transfer ring for interrupt endpoint (HID) */
    xhci_trb_t       *int_ring;
    uint64_t           int_ring_phys;
    uint16_t           int_enqueue;
    uint8_t            int_cycle;
    uint8_t            int_ep_num;    /* Endpoint number (1-15) */
    uint8_t            int_ep_dci;    /* Device Context Index */
} xhci_slot_t;

/* ── xHCI Controller state ── */
typedef struct {
    hal_device_t       dev;
    volatile void     *cap_base;      /* BAR0 — Capability registers */
    volatile void     *op_base;       /* Operational registers */
    volatile void     *rt_base;       /* Runtime registers */
    volatile void     *db_base;       /* Doorbell registers */

    uint8_t            max_slots;
    uint8_t            max_ports;
    uint16_t           page_size;     /* In bytes */

    /* Device Context Base Address Array */
    uint64_t          *dcbaa;
    uint64_t           dcbaa_phys;

    /* Command Ring */
    xhci_trb_t        *cmd_ring;
    uint64_t           cmd_ring_phys;
    uint16_t           cmd_enqueue;
    uint8_t            cmd_cycle;     /* Producer cycle state */

    /* Event Ring (Interrupter 0) */
    xhci_trb_t        *evt_ring;
    uint64_t           evt_ring_phys;
    xhci_erst_entry_t *erst;
    uint64_t           erst_phys;
    uint16_t           evt_dequeue;
    uint8_t            evt_cycle;     /* Consumer cycle state */

    /* Per-slot data */
    xhci_slot_t        slots[XHCI_MAX_SLOTS];

    /* Scratchpad buffers (if needed) */
    uint64_t          *scratchpad_array;
    uint64_t           scratchpad_phys;

    bool               initialized;
} xhci_controller_t;

/* ── Public API ── */

/* Initialize xHCI controller */
hal_status_t xhci_init(xhci_controller_t *hc, hal_device_t *dev);

/* Reset and enable a port. Returns slot_id > 0 on success, 0 on failure. */
uint8_t xhci_port_reset(xhci_controller_t *hc, uint8_t port);

/* Address a device (assigns USB address). Must be called after port_reset. */
hal_status_t xhci_address_device(xhci_controller_t *hc, uint8_t slot_id);

/* Get device descriptor */
hal_status_t xhci_get_device_desc(xhci_controller_t *hc, uint8_t slot_id,
                                   usb_device_desc_t *desc);

/* Get configuration descriptor */
hal_status_t xhci_get_config_desc(xhci_controller_t *hc, uint8_t slot_id,
                                   void *buf, uint16_t buf_len);

/* Set configuration */
hal_status_t xhci_set_config(xhci_controller_t *hc, uint8_t slot_id,
                              uint8_t config_value);

/* Control transfer (generic) */
hal_status_t xhci_control_transfer(xhci_controller_t *hc, uint8_t slot_id,
                                    uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint16_t wLength, void *data);

/* Configure an interrupt endpoint for HID */
hal_status_t xhci_configure_interrupt_ep(xhci_controller_t *hc, uint8_t slot_id,
                                          uint8_t ep_num, uint16_t max_packet,
                                          uint8_t interval);

/* Poll for an interrupt transfer completion.
 * Returns HAL_OK if data available, HAL_NO_DEVICE if none. */
hal_status_t xhci_poll_interrupt(xhci_controller_t *hc, uint8_t slot_id,
                                  void *buf, uint16_t *length);

#endif /* ALJEFRA_DRV_XHCI_H */
