#pragma once

// PS4 stub for libusb.h (USB library not available on PS4)

#include <stdint.h>
#include <sys/time.h>

#define LIBUSB_CALL

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

struct libusb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

#define LIBUSB_SUCCESS 0
#define LIBUSB_ERROR_NOT_FOUND -5
#define LIBUSB_ERROR_BUSY -6
#define LIBUSB_ERROR_NO_DEVICE -4
#define LIBUSB_ERROR_TIMEOUT -7
#define LIBUSB_ERROR_ACCESS -3
#define LIBUSB_ERROR_IO -1
#define LIBUSB_ERROR_OVERFLOW -8
#define LIBUSB_ERROR_PIPE -9
#define LIBUSB_ERROR_INTERRUPTED -10
#define LIBUSB_ERROR_NO_MEM -11
#define LIBUSB_ERROR_NOT_SUPPORTED -12
#define LIBUSB_ERROR_OTHER -99
#define LIBUSB_TRANSFER_COMPLETED 0

#define LIBUSB_ENDPOINT_IN 0x80
#define LIBUSB_ENDPOINT_OUT 0x00
#define LIBUSB_REQUEST_TYPE_CLASS 0x20
#define LIBUSB_REQUEST_TYPE_STANDARD 0x00
#define LIBUSB_RECIPIENT_INTERFACE 0x01
#define LIBUSB_RECIPIENT_DEVICE 0x00
#define LIBUSB_RECIPIENT_ENDPOINT 0x02
#define LIBUSB_REQUEST_GET_STATUS 0x00
#define LIBUSB_REQUEST_SET_CONFIGURATION 0x09
#define LIBUSB_REQUEST_GET_CONFIGURATION 0x08
#define LIBUSB_REQUEST_GET_DESCRIPTOR 0x06
#define LIBUSB_REQUEST_SET_INTERFACE 0x11
#define LIBUSB_TRANSFER_TYPE_CONTROL 0
#define LIBUSB_TRANSFER_TYPE_BULK 2
#define LIBUSB_TRANSFER_TYPE_INTERRUPT 3
#define LIBUSB_TRANSFER_TYPE_MASK 0x03
#define LIBUSB_CONTROL_SETUP_SIZE 8

#define LIBUSB_TRANSFER_COMPLETED 0
#define LIBUSB_TRANSFER_ERROR 1
#define LIBUSB_TRANSFER_TIMED_OUT 2
#define LIBUSB_TRANSFER_CANCELLED 3
#define LIBUSB_TRANSFER_STALL 4
#define LIBUSB_TRANSFER_NO_DEVICE 5
#define LIBUSB_TRANSFER_OVERFLOW 6

struct libusb_transfer;
typedef void (*libusb_transfer_cb_fn)(struct libusb_transfer* transfer);

struct libusb_iso_packet_descriptor {
    unsigned int length;
    unsigned int actual_length;
    int status;
};

struct libusb_transfer {
    libusb_device_handle* dev_handle;
    unsigned char endpoint;
    unsigned char type;
    unsigned int timeout;
    unsigned char* buffer;
    int length;
    int actual_length;
    void* user_data;
    int status;
    libusb_transfer_cb_fn callback;
    int num_iso_packets;
    struct libusb_iso_packet_descriptor iso_packet_desc[1];
};

static inline int libusb_init(libusb_context** ctx) { return LIBUSB_ERROR_NOT_FOUND; }
static inline void libusb_exit(libusb_context* ctx) {}
static inline ssize_t libusb_get_device_list(libusb_context* ctx, libusb_device*** list) { return 0; }
static inline void libusb_free_device_list(libusb_device** list, int unref_devices) {}
static inline int libusb_get_device_descriptor(libusb_device* dev, struct libusb_device_descriptor* desc) { return LIBUSB_ERROR_NOT_FOUND; }
static inline int libusb_open(libusb_device* dev, libusb_device_handle** handle) { return LIBUSB_ERROR_NOT_FOUND; }
static inline void libusb_close(libusb_device_handle* handle) {}
static inline int libusb_claim_interface(libusb_device_handle* handle, int iface) { return LIBUSB_ERROR_NOT_FOUND; }
static inline int libusb_release_interface(libusb_device_handle* handle, int iface) { return 0; }
static inline int libusb_set_configuration(libusb_device_handle* handle, int config) { return 0; }
static inline int libusb_kernel_driver_active(libusb_device_handle* handle, int iface) { return 0; }
static inline int libusb_detach_kernel_driver(libusb_device_handle* handle, int iface) { return 0; }
static inline int libusb_attach_kernel_driver(libusb_device_handle* handle, int iface) { return 0; }
static inline int libusb_control_transfer(libusb_device_handle* handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned char* data, uint16_t wLength, unsigned int timeout) { return 0; }
static inline int libusb_bulk_transfer(libusb_device_handle* handle, unsigned char endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout) { return 0; }
static inline int libusb_interrupt_transfer(libusb_device_handle* handle, unsigned char endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout) { return 0; }
static inline uint8_t libusb_get_bus_number(libusb_device* dev) { return 0; }
static inline uint8_t libusb_get_device_address(libusb_device* dev) { return 0; }
static inline uint8_t libusb_get_port_number(libusb_device* dev) { return 0; }
static inline void libusb_ref_device(libusb_device* dev) {}
static inline const char* libusb_error_name(int code) { return "stub"; }
static inline struct libusb_transfer* libusb_alloc_transfer(int iso_packets) { return nullptr; }
static inline void libusb_free_transfer(struct libusb_transfer* transfer) {}
static inline int libusb_submit_transfer(struct libusb_transfer* transfer) { return LIBUSB_ERROR_NOT_FOUND; }
static inline int libusb_cancel_transfer(struct libusb_transfer* transfer) { return 0; }
static inline void libusb_fill_interrupt_transfer(struct libusb_transfer* transfer, libusb_device_handle* handle, unsigned char endpoint, unsigned char* buffer, int length, libusb_transfer_cb_fn cb, void* data, unsigned int timeout) {}
static inline void libusb_fill_bulk_transfer(struct libusb_transfer* transfer, libusb_device_handle* handle, unsigned char endpoint, unsigned char* buffer, int length, libusb_transfer_cb_fn cb, void* data, unsigned int timeout) {}
static inline int libusb_handle_events_timeout_completed(libusb_context* ctx, struct timeval* tv, int* completed) { return 0; }
static inline int libusb_handle_events(libusb_context* ctx) { return 0; }
static inline int libusb_handle_events_timeout(libusb_context* ctx, struct timeval* tv) { return 0; }
static inline int libusb_get_configuration(libusb_device_handle* handle, int* config) { *config = 0; return 0; }
static inline void libusb_unref_device(libusb_device* dev) {}
static inline void libusb_fill_control_setup(unsigned char* buffer, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength) {}
static inline void libusb_fill_control_transfer(struct libusb_transfer* transfer, libusb_device_handle* handle, unsigned char* buffer, libusb_transfer_cb_fn cb, void* data, unsigned int timeout) {}
static inline void libusb_fill_iso_transfer(struct libusb_transfer* transfer, libusb_device_handle* handle, unsigned char endpoint, unsigned char* buffer, int length, int num_iso_packets, libusb_transfer_cb_fn cb, void* data, unsigned int timeout) {}
