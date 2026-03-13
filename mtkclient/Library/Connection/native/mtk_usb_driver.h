/*
 * mtk_usb_driver.h - Native Windows USB driver for MediaTek preloader devices
 * (c) 2024-2026 GPLv3 License
 *
 * Provides low-level USB device management for mtkclient on Windows 11 x64:
 * - Device detection via SetupAPI/WinUSB
 * - USB device reset without physical disconnect
 * - Endpoint clear/halt recovery for I/O errors
 * - USB power management configuration
 * - Preloader mode device filtering
 */

#ifndef MTK_USB_DRIVER_H
#define MTK_USB_DRIVER_H

#ifdef _WIN32
#ifdef MTK_USB_EXPORTS
#define MTK_USB_API __declspec(dllexport)
#else
#define MTK_USB_API __declspec(dllimport)
#endif
#else
#define MTK_USB_API
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MediaTek USB device information structure */
typedef struct {
    uint16_t vid;
    uint16_t pid;
    int is_preloader;
    int is_bootrom;
    int is_da_mode;
    char device_path[512];
    char description[256];
    char driver_name[128];
} mtk_device_info_t;

/* USB endpoint information */
typedef struct {
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t max_packet_in;
    uint16_t max_packet_out;
    int interface_num;
} mtk_endpoint_info_t;

/* Error codes */
#define MTK_SUCCESS             0
#define MTK_ERROR_NOT_FOUND     -1
#define MTK_ERROR_ACCESS        -2
#define MTK_ERROR_IO            -3
#define MTK_ERROR_TIMEOUT       -4
#define MTK_ERROR_PIPE          -5
#define MTK_ERROR_NO_DRIVER     -6
#define MTK_ERROR_INVALID_PARAM -7
#define MTK_ERROR_NO_MEMORY     -8
#define MTK_ERROR_NOT_SUPPORTED -9
#define MTK_ERROR_OTHER         -99

/* Maximum devices that can be enumerated */
#define MTK_MAX_DEVICES 16

/*
 * Initialize the MTK USB driver library.
 * Must be called before any other function.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_init(void);

/*
 * Cleanup and release all resources.
 */
MTK_USB_API void mtk_usb_cleanup(void);

/*
 * Scan for connected MediaTek USB devices using SetupAPI.
 * Populates the devices array with found devices.
 * Returns the number of devices found, or negative error code.
 */
MTK_USB_API int mtk_usb_find_devices(mtk_device_info_t *devices, int max_devices);

/*
 * Wait for a MediaTek device to appear on the USB bus.
 * Polls every poll_interval_ms milliseconds.
 * Returns MTK_SUCCESS when device found, MTK_ERROR_TIMEOUT on timeout.
 * Populates device_info with the found device information.
 */
MTK_USB_API int mtk_usb_wait_for_device(mtk_device_info_t *device_info,
                                         int timeout_ms,
                                         int poll_interval_ms);

/*
 * Reset a USB device by its VID/PID without physical disconnection.
 * Uses SetupAPI DeviceIoControl to trigger port reset.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_reset_device(uint16_t vid, uint16_t pid);

/*
 * Reset a USB device by its device path.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_reset_device_by_path(const char *device_path);

/*
 * Clear a halted/stalled USB endpoint.
 * This recovers from I/O errors without device reset.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_clear_endpoint(const char *device_path,
                                        uint8_t endpoint);

/*
 * Disable USB selective suspend for a device.
 * Prevents Windows from power-managing the device during operations.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_disable_selective_suspend(uint16_t vid, uint16_t pid);

/*
 * Get the driver name currently bound to a device.
 * Returns MTK_SUCCESS or error code.
 */
MTK_USB_API int mtk_usb_get_driver_name(uint16_t vid, uint16_t pid,
                                         char *driver_name, int buf_size);

/*
 * Check if WinUSB or libusb driver is installed for the device.
 * Returns 1 if compatible driver found, 0 if not, negative on error.
 */
MTK_USB_API int mtk_usb_check_driver_installed(uint16_t vid, uint16_t pid);

/*
 * Get a human-readable description for error codes.
 */
MTK_USB_API const char* mtk_usb_error_string(int error_code);

/*
 * Get library version string.
 */
MTK_USB_API const char* mtk_usb_version(void);

/*
 * Enable/disable debug logging.
 * When enabled, logs are written to the specified file.
 * Pass NULL for log_file to disable logging.
 */
MTK_USB_API int mtk_usb_set_log(const char *log_file, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* MTK_USB_DRIVER_H */
