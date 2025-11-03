/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort.h
 * @brief C API for HailoRT library.
 *
 * C API for Hailo runtime (HailoRT) library for neural network inference on Hailo devices.
 **/

#ifndef _HAILORT_H_
#define _HAILORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "platform.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>


/** @defgroup group_defines HailoRT API definitions
 *  @{
 */

#define HAILO_MAX_ENUM (INT_MAX)
#define HAILO_INFINITE (UINT32_MAX)
#define HAILO_DEFAULT_ETH_SCAN_TIMEOUT_MS (10000)
#define HAILO_DEFAULT_ETH_DEVICE_PORT (0)
#define HAILO_DEFAULT_ETH_MAX_PAYLOAD_SIZE (1456)
#define HAILO_DEFAULT_ETH_MAX_NUMBER_OF_RETRIES (3)
#define HAILO_ETH_ADDRESS_ANY ("0.0.0.0")
#define HAILO_ETH_PORT_ANY (0)
#define HAILO_MAX_NAME_SIZE (128)
#define HAILO_MAX_STREAM_NAME_SIZE (HAILO_MAX_NAME_SIZE)
#define HAILO_MAX_BOARD_NAME_LENGTH (32)
#define HAILO_MAX_DEVICE_ID_LENGTH (32)
#define HAILO_MAX_SERIAL_NUMBER_LENGTH (16)
#define HAILO_MAX_PART_NUMBER_LENGTH (16)
#define HAILO_MAX_PRODUCT_NAME_LENGTH (42)
#define HAILO_DEFAULT_INIT_SAMPLING_PERIOD_US (HAILO_SAMPLING_PERIOD_1100US)
#define HAILO_DEFAULT_INIT_AVERAGING_FACTOR (HAILO_AVERAGE_FACTOR_256)
#define HAILO_DEFAULT_BUFFERS_THRESHOLD (0)
#define HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC (106300000)
#define HAILO_MAX_STREAMS_COUNT (40)
#define HAILO_DEFAULT_BATCH_SIZE (0)
#define HAILO_MAX_NETWORK_GROUPS (8)
#define HAILO_MAX_NETWORK_GROUP_NAME_SIZE (HAILO_MAX_NAME_SIZE)
/* Network name is always attached to network group name with '/' separator */
#define HAILO_MAX_NETWORK_NAME_SIZE (HAILO_MAX_NETWORK_GROUP_NAME_SIZE + 1 + HAILO_MAX_NAME_SIZE)
#define HAILO_MAX_NETWORKS_IN_NETWORK_GROUP (8)
#define HAILO_PCIE_ANY_DOMAIN (UINT32_MAX)
#define HAILO_DEFAULT_VSTREAM_QUEUE_SIZE (2)
#define HAILO_DEFAULT_VSTREAM_TIMEOUT_MS (10000)
#define HAILO_DEFAULT_ASYNC_INFER_TIMEOUT_MS (10000)
#define HAILO_DEFAULT_ASYNC_INFER_QUEUE_SIZE (2)
#define HAILO_DEFAULT_DEVICE_COUNT (1)

#define HAILO_SOC_ID_LENGTH (32)
#define HAILO_ETH_MAC_LENGTH (6)
#define HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH (12)
#define HAILO_SOC_PM_VALUES_BYTES_LENGTH (24)
#define HAILO_GPIO_MASK_VALUES_LENGTH (16)
#define HAILO_MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER (4)

#define HAILO_UNIQUE_VDEVICE_GROUP_ID ("UNIQUE")
#define HAILO_DEFAULT_VDEVICE_GROUP_ID HAILO_UNIQUE_VDEVICE_GROUP_ID

#define HAILO_SCHEDULER_PRIORITY_NORMAL (16)
#define HAILO_SCHEDULER_PRIORITY_MAX (31)
#define HAILO_SCHEDULER_PRIORITY_MIN (0)

#define MAX_NUMBER_OF_PLANES (4)
#define NUMBER_OF_PLANES_NV12_NV21 (2)
#define NUMBER_OF_PLANES_I420 (3)

#define INVALID_QUANT_INFO {0.0f, 0.0f, 0.0f, 0.0f}

#define HAILO_RANDOM_SEED (UINT32_MAX)

typedef float float32_t;
typedef double float64_t;
typedef uint16_t nms_bbox_counter_t;

/** HailoRT return codes */
#define HAILO_STATUS_VARIABLES\
    HAILO_STATUS__X(0,  HAILO_SUCCESS                                 /*!< Success - No error */)\
    HAILO_STATUS__X(1,  HAILO_UNINITIALIZED                           /*!< No error code was initialized */)\
    HAILO_STATUS__X(2,  HAILO_INVALID_ARGUMENT                        /*!< Invalid argument passed to function */)\
    HAILO_STATUS__X(3,  HAILO_OUT_OF_HOST_MEMORY                      /*!< Cannot allocate more memory at host */)\
    HAILO_STATUS__X(4,  HAILO_TIMEOUT                                 /*!< Received a timeout */)\
    HAILO_STATUS__X(5,  HAILO_INSUFFICIENT_BUFFER                     /*!< Buffer is insufficient */)\
    HAILO_STATUS__X(6,  HAILO_INVALID_OPERATION                       /*!< Invalid operation */)\
    HAILO_STATUS__X(7,  HAILO_NOT_IMPLEMENTED                         /*!< Code has not been implemented */)\
    HAILO_STATUS__X(8,  HAILO_INTERNAL_FAILURE                        /*!< Unexpected internal failure */)\
    HAILO_STATUS__X(9,  HAILO_DATA_ALIGNMENT_FAILURE                  /*!< Data is not aligned */)\
    HAILO_STATUS__X(10, HAILO_CHUNK_TOO_LARGE                         /*!< Chunk too large */)\
    HAILO_STATUS__X(11, HAILO_INVALID_LOGGER_LEVEL                    /*!< Used non-compiled level */)\
    HAILO_STATUS__X(12, HAILO_CLOSE_FAILURE                           /*!< Failed to close fd */)\
    HAILO_STATUS__X(13, HAILO_OPEN_FILE_FAILURE                       /*!< Failed to open file */)\
    HAILO_STATUS__X(14, HAILO_FILE_OPERATION_FAILURE                  /*!< File operation failure */)\
    HAILO_STATUS__X(15, HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION    /*!< Unsupported control protocol version */)\
    HAILO_STATUS__X(16, HAILO_UNSUPPORTED_FW_VERSION                  /*!< Unsupported firmware version */)\
    HAILO_STATUS__X(17, HAILO_INVALID_CONTROL_RESPONSE                /*!< Invalid control response */)\
    HAILO_STATUS__X(18, HAILO_FW_CONTROL_FAILURE                      /*!< Control failed in firmware */)\
    HAILO_STATUS__X(19, HAILO_ETH_FAILURE                             /*!< Ethernet operation has failed */)\
    HAILO_STATUS__X(20, HAILO_ETH_INTERFACE_NOT_FOUND                 /*!< Ethernet interface not found */)\
    HAILO_STATUS__X(21, HAILO_ETH_RECV_FAILURE                        /*!< Ethernet failed at recv operation */)\
    HAILO_STATUS__X(22, HAILO_ETH_SEND_FAILURE                        /*!< Ethernet failed at send operation */)\
    HAILO_STATUS__X(23, HAILO_INVALID_FIRMWARE                        /*!< Firmware bin is invalid */)\
    HAILO_STATUS__X(24, HAILO_INVALID_CONTEXT_COUNT                   /*!< Host build too many contexts */)\
    HAILO_STATUS__X(25, HAILO_INVALID_FRAME                           /*!< Part or all of the result data is invalid */)\
    HAILO_STATUS__X(26, HAILO_INVALID_HEF                             /*!< Invalid HEF */)\
    HAILO_STATUS__X(27, HAILO_PCIE_NOT_SUPPORTED_ON_PLATFORM          /*!< PCIe not supported on platform */)\
    HAILO_STATUS__X(28, HAILO_INTERRUPTED_BY_SIGNAL                   /*!< Blocking syscall was interrupted by a signal */)\
    HAILO_STATUS__X(29, HAILO_START_VDMA_CHANNEL_FAIL                 /*!< Starting VDMA channel failure */)\
    HAILO_STATUS__X(30, HAILO_SYNC_VDMA_BUFFER_FAIL                   /*!< Synchronizing VDMA buffer failure */)\
    HAILO_STATUS__X(31, HAILO_STOP_VDMA_CHANNEL_FAIL                  /*!< Stopping VDMA channel failure */)\
    HAILO_STATUS__X(32, HAILO_CLOSE_VDMA_CHANNEL_FAIL                 /*!< Closing VDMA channel failure */)\
    HAILO_STATUS__X(33, HAILO_ATR_TABLES_CONF_VALIDATION_FAIL         /*!< Validating address translation tables failure, for FW control use */)\
    HAILO_STATUS__X(34, HAILO_EVENT_CREATE_FAIL                       /*!< Creating event failure */)\
    HAILO_STATUS__X(35, HAILO_READ_EVENT_FAIL                         /*!< Reading event failure */)\
    HAILO_STATUS__X(36, HAILO_DRIVER_OPERATION_FAILED                 /*!< Driver operation (i.e ioctl) returned failure. Read driver log for more info (dmesg for linux) */)\
    HAILO_STATUS__X(37, HAILO_INVALID_FIRMWARE_MAGIC                  /*!< Invalid FW magic */)\
    HAILO_STATUS__X(38, HAILO_INVALID_FIRMWARE_CODE_SIZE              /*!< Invalid FW code size */)\
    HAILO_STATUS__X(39, HAILO_INVALID_KEY_CERTIFICATE_SIZE            /*!< Invalid key certificate size */)\
    HAILO_STATUS__X(40, HAILO_INVALID_CONTENT_CERTIFICATE_SIZE        /*!< Invalid content certificate size */)\
    HAILO_STATUS__X(41, HAILO_MISMATCHING_FIRMWARE_BUFFER_SIZES       /*!< FW buffer sizes mismatch */)\
    HAILO_STATUS__X(42, HAILO_INVALID_FIRMWARE_CPU_ID                 /*!< Invalid CPU ID in FW */)\
    HAILO_STATUS__X(43, HAILO_CONTROL_RESPONSE_MD5_MISMATCH           /*!< MD5 of control response does not match expected MD5 */)\
    HAILO_STATUS__X(44, HAILO_GET_CONTROL_RESPONSE_FAIL               /*!< Get control response failed */)\
    HAILO_STATUS__X(45, HAILO_GET_D2H_EVENT_MESSAGE_FAIL              /*!< Reading device-to-host message failure */)\
    HAILO_STATUS__X(46, HAILO_MUTEX_INIT_FAIL                         /*!< Mutex initialization failure */)\
    HAILO_STATUS__X(47, HAILO_OUT_OF_DESCRIPTORS                      /*!< Cannot allocate more descriptors */)\
    HAILO_STATUS__X(48, HAILO_UNSUPPORTED_OPCODE                      /*!< Unsupported opcode was sent to device */)\
    HAILO_STATUS__X(49, HAILO_USER_MODE_RATE_LIMITER_NOT_SUPPORTED    /*!< User mode rate limiter not supported on platform */)\
    HAILO_STATUS__X(50, HAILO_RATE_LIMIT_MAXIMUM_BANDWIDTH_EXCEEDED   /*!< Rate limit exceeded HAILO_DEFAULT_MAX_ETHERNET_BANDWIDTH_BYTES_PER_SEC */)\
    HAILO_STATUS__X(51, HAILO_ANSI_TO_UTF16_CONVERSION_FAILED         /*!< Failed converting ANSI string to UNICODE */)\
    HAILO_STATUS__X(52, HAILO_UTF16_TO_ANSI_CONVERSION_FAILED         /*!< Failed converting UNICODE string to ANSI */)\
    HAILO_STATUS__X(53, HAILO_UNEXPECTED_INTERFACE_INFO_FAILURE       /*!< Failed retrieving interface info */)\
    HAILO_STATUS__X(54, HAILO_UNEXPECTED_ARP_TABLE_FAILURE            /*!< Failed retrieving arp table */)\
    HAILO_STATUS__X(55, HAILO_MAC_ADDRESS_NOT_FOUND                   /*!< MAC address not found in the arp table */)\
    HAILO_STATUS__X(56, HAILO_NO_IPV4_INTERFACES_FOUND                /*!< No interfaces found with an IPv4 address */)\
    HAILO_STATUS__X(57, HAILO_SHUTDOWN_EVENT_SIGNALED                 /*!< A shutdown event has been signaled */)\
    HAILO_STATUS__X(58, HAILO_THREAD_ALREADY_ACTIVATED                /*!< The given thread has already been activated */)\
    HAILO_STATUS__X(59, HAILO_THREAD_NOT_ACTIVATED                    /*!< The given thread has not been activated */)\
    HAILO_STATUS__X(60, HAILO_THREAD_NOT_JOINABLE                     /*!< The given thread is not joinable */)\
    HAILO_STATUS__X(61, HAILO_NOT_FOUND                               /*!< Could not find element */)\
    HAILO_STATUS__X(62, HAILO_COMMUNICATION_CLOSED                    /*!< The communication between endpoints is closed */)\
    HAILO_STATUS__X(63, HAILO_STREAM_ABORT                            /*!< Stream recv/send was aborted */)\
    HAILO_STATUS__X(64, HAILO_DRIVER_NOT_INSTALLED                    /*!< Driver is not installed/running on the system. */)\
    HAILO_STATUS__X(65, HAILO_NOT_AVAILABLE                           /*!< Component is not available */)\
    HAILO_STATUS__X(66, HAILO_TRAFFIC_CONTROL_FAILURE                 /*!< Traffic control failure */)\
    HAILO_STATUS__X(67, HAILO_INVALID_SECOND_STAGE                    /*!< Second stage bin is invalid */)\
    HAILO_STATUS__X(68, HAILO_INVALID_PIPELINE                        /*!< Pipeline is invalid */)\
    HAILO_STATUS__X(69, HAILO_NETWORK_GROUP_NOT_ACTIVATED             /*!< Network group is not activated */)\
    HAILO_STATUS__X(70, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED          /*!< VStream pipeline is not activated */)\
    HAILO_STATUS__X(71, HAILO_OUT_OF_FW_MEMORY                        /*!< Cannot allocate more memory at fw */)\
    HAILO_STATUS__X(72, HAILO_STREAM_NOT_ACTIVATED                    /*!< Stream is not activated */)\
    HAILO_STATUS__X(73, HAILO_DEVICE_IN_USE                           /*!< The device is already in use */)\
    HAILO_STATUS__X(74, HAILO_OUT_OF_PHYSICAL_DEVICES                 /*!< There are not enough physical devices */)\
    HAILO_STATUS__X(75, HAILO_INVALID_DEVICE_ARCHITECTURE             /*!< Invalid device architecture */)\
    HAILO_STATUS__X(76, HAILO_INVALID_DRIVER_VERSION                  /*!< Invalid driver version */)\
    HAILO_STATUS__X(77, HAILO_RPC_FAILED                              /*!< RPC failed */)\
    HAILO_STATUS__X(78, HAILO_INVALID_SERVICE_VERSION                 /*!< Invalid service version */)\
    HAILO_STATUS__X(79, HAILO_NOT_SUPPORTED                           /*!< Not supported operation */)\
    HAILO_STATUS__X(80, HAILO_NMS_BURST_INVALID_DATA                  /*!< Invalid data in NMS burst */)\
    HAILO_STATUS__X(81, HAILO_OUT_OF_HOST_CMA_MEMORY                  /*!< Cannot allocate more CMA memory at host */)\
    HAILO_STATUS__X(82, HAILO_QUEUE_IS_FULL                           /*!< Cannot push more items into the queue */)\
    HAILO_STATUS__X(83, HAILO_DMA_MAPPING_ALREADY_EXISTS              /*!< DMA mapping already exists */)\
    HAILO_STATUS__X(84, HAILO_CANT_MEET_BUFFER_REQUIREMENTS           /*!< can't meet buffer requirements */)\
    HAILO_STATUS__X(85, HAILO_DRIVER_INVALID_RESPONSE                 /*!< Driver returned invalid response. Make sure the driver version is the same as libhailort  */)\
    HAILO_STATUS__X(86, HAILO_DRIVER_INVALID_IOCTL                    /*!< Driver cannot handle ioctl. Can happen on libhailort vs driver version mismatch or when ioctl function is not supported */)\
    HAILO_STATUS__X(87, HAILO_DRIVER_TIMEOUT                          /*!< Driver operation returned a timeout. Device reset may be required. */)\
    HAILO_STATUS__X(88, HAILO_DRIVER_INTERRUPTED                      /*!< Driver operation interrupted by system request (i.e can happen on application exit) */)\
    HAILO_STATUS__X(89, HAILO_CONNECTION_REFUSED                      /*!< Connection was refused by other side */)\
    HAILO_STATUS__X(90, HAILO_DRIVER_WAIT_CANCELED                    /*!< Driver operation was canceled */)\
    HAILO_STATUS__X(91, HAILO_HEF_FILE_CORRUPTED                      /*!< HEF file is corrupted */)\
    HAILO_STATUS__X(92, HAILO_HEF_NOT_SUPPORTED                       /*!< HEF file is not supported. Make sure the DFC version is compatible. */)\
    HAILO_STATUS__X(93, HAILO_HEF_NOT_COMPATIBLE_WITH_DEVICE          /*!< HEF file is not compatible with device. */)\
    HAILO_STATUS__X(94, HAILO_INVALID_HEF_USE                         /*!< Invalid HEF use (i.e. when using HEF from a file path without first copying it's content to a mapped buffer while shared_weights is enabled) */)\
    HAILO_STATUS__X(95, HAILO_OPERATION_ABORTED                       /*!< Operation was aborted */)\
    HAILO_STATUS__X(96, HAILO_DEVICE_NOT_CONNECTED                    /*!< Device is not connected */)\
    HAILO_STATUS__X(97, HAILO_DEVICE_TEMPORARILY_UNAVAILABLE          /*!< Device is temporarily unavailable, try again later */)\

typedef enum {
#define HAILO_STATUS__X(value, name) name = value,
    HAILO_STATUS_VARIABLES
#undef HAILO_STATUS__X

    /** Must be last! */
    HAILO_STATUS_COUNT,

    /** Max enum value to maintain ABI Integrity */
    HAILO_STATUS_MAX_ENUM                       = HAILO_MAX_ENUM
} hailo_status;

#define HAILO_STREAM_ABORTED_BY_USER HAILO_STREAM_ABORT /* 'HAILO_STREAM_ABORTED_BY_USER' is deprecated. Use 'HAILO_STREAM_ABORT' instead */
#define HAILO_DRIVER_FAIL HAILO_DRIVER_OPERATION_FAILED /* 'HAILO_DRIVER_FAIL' is deprecated. Use 'HAILO_DRIVER_OPERATION_FAILED' instead */
#define HAILO_PCIE_DRIVER_NOT_INSTALLED HAILO_DRIVER_NOT_INSTALLED /* 'HAILO_PCIE_DRIVER_NOT_INSTALLED' is deprecated. Use 'HAILO_DRIVER_NOT_INSTALLED' instead */

/** HailoRT library version */
typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
} hailo_version_t;

/** Represents the device (chip) */
typedef struct _hailo_device *hailo_device;

/** Represents a virtual device which manages several physical devices */
typedef struct _hailo_vdevice *hailo_vdevice;

/** Compiled HEF model that can be loaded to Hailo devices */
typedef struct _hailo_hef *hailo_hef;

/** Input (host to device) stream representation */
typedef struct _hailo_input_stream *hailo_input_stream;

/** Output (device to host) stream representation */
typedef struct _hailo_output_stream *hailo_output_stream;

/** Loaded network_group that can be activated */
typedef struct _hailo_configured_network_group *hailo_configured_network_group;

/** Activated network_group that can be used to send/receive data */
typedef struct _hailo_activated_network_group *hailo_activated_network_group;

/** Object used for input stream transformation, store all necessary allocated buffers */
typedef struct _hailo_input_transform_context *hailo_input_transform_context;

/** Object used for output stream transformation, store all necessary allocated buffers */
typedef struct _hailo_output_transform_context *hailo_output_transform_context;

/** Object used to demux muxed stream */
typedef struct _hailo_output_demuxer *hailo_output_demuxer;

/** Input virtual stream */
typedef struct _hailo_input_vstream *hailo_input_vstream;

/** Output virtual stream */
typedef struct _hailo_output_vstream *hailo_output_vstream;

/** Enum that represents the type of devices that would be measured */
typedef enum hailo_dvm_options_e {
    /** VDD_CORE DVM */
    HAILO_DVM_OPTIONS_VDD_CORE = 0,

    /** VDD_IO DVM */
    HAILO_DVM_OPTIONS_VDD_IO,

    /** MIPI_AVDD DVM */
    HAILO_DVM_OPTIONS_MIPI_AVDD,

    /** MIPI_AVDD_H DVM */
    HAILO_DVM_OPTIONS_MIPI_AVDD_H,

    /** USB_AVDD_IO DVM */
    HAILO_DVM_OPTIONS_USB_AVDD_IO,

    /** VDD_TOP DVM */
    HAILO_DVM_OPTIONS_VDD_TOP,

    /** USB_AVDD_IO_HV DVM */
    HAILO_DVM_OPTIONS_USB_AVDD_IO_HV,

    /** AVDD_H DVM */
    HAILO_DVM_OPTIONS_AVDD_H,

    /** SDIO_VDD_IO DVM */
    HAILO_DVM_OPTIONS_SDIO_VDD_IO,

    /** OVERCURRENT_PROTECTION DVM */
    HAILO_DVM_OPTIONS_OVERCURRENT_PROTECTION,

    /** Must be last! */
    HAILO_DVM_OPTIONS_COUNT,

    /** Select the default DVM option according to the supported features */
    HAILO_DVM_OPTIONS_AUTO = INT_MAX,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DVM_OPTIONS_MAX_ENUM = HAILO_MAX_ENUM
} hailo_dvm_options_t;

/** Enum that represents what would be measured on the selected device */
typedef enum hailo_power_measurement_types_e {
    /** SHUNT_VOLTAGE measurement type, measured in mV */
    HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE = 0,

    /** BUS_VOLTAGE measurement type, measured in mV */
    HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE,

    /** POWER measurement type, measured in W */
    HAILO_POWER_MEASUREMENT_TYPES__POWER,

    /** CURRENT measurement type, measured in mA */
    HAILO_POWER_MEASUREMENT_TYPES__CURRENT,

    /** Must be last! */
    HAILO_POWER_MEASUREMENT_TYPES__COUNT,

    /** Select the default measurement type according to the supported features */
    HAILO_POWER_MEASUREMENT_TYPES__AUTO = INT_MAX,

    /** Max enum value to maintain ABI Integrity */
    HAILO_POWER_MEASUREMENT_TYPES__MAX_ENUM = HAILO_MAX_ENUM
} hailo_power_measurement_types_t;

/** Enum that represents all the bit options and related conversion times for each bit setting for Bus Voltage and Shunt Voltage */
typedef enum hailo_sampling_period_e {
    HAILO_SAMPLING_PERIOD_140US = 0,
    HAILO_SAMPLING_PERIOD_204US,
    HAILO_SAMPLING_PERIOD_332US,
    HAILO_SAMPLING_PERIOD_588US,
    HAILO_SAMPLING_PERIOD_1100US,
    HAILO_SAMPLING_PERIOD_2116US,
    HAILO_SAMPLING_PERIOD_4156US,
    HAILO_SAMPLING_PERIOD_8244US,

    /** Max enum value to maintain ABI Integrity */
    HAILO_SAMPLING_PERIOD_MAX_ENUM = HAILO_MAX_ENUM
} hailo_sampling_period_t;

/** Enum that represents all the AVG bit settings and related number of averages for each bit setting */
typedef enum hailo_averaging_factor_e {
    HAILO_AVERAGE_FACTOR_1 = 0,
    HAILO_AVERAGE_FACTOR_4,
    HAILO_AVERAGE_FACTOR_16,
    HAILO_AVERAGE_FACTOR_64,
    HAILO_AVERAGE_FACTOR_128,
    HAILO_AVERAGE_FACTOR_256,
    HAILO_AVERAGE_FACTOR_512,
    HAILO_AVERAGE_FACTOR_1024,

    /** Max enum value to maintain ABI Integrity */
    HAILO_AVERAGE_FACTOR_MAX_ENUM = HAILO_MAX_ENUM
} hailo_averaging_factor_t;

/** Enum that represents buffers on the device for power measurements storing */
typedef enum hailo_measurement_buffer_index_e {
    HAILO_MEASUREMENT_BUFFER_INDEX_0 = 0,
    HAILO_MEASUREMENT_BUFFER_INDEX_1,
    HAILO_MEASUREMENT_BUFFER_INDEX_2,
    HAILO_MEASUREMENT_BUFFER_INDEX_3,

    /** Max enum value to maintain ABI Integrity */
    HAILO_MEASUREMENT_BUFFER_INDEX_MAX_ENUM = HAILO_MAX_ENUM
} hailo_measurement_buffer_index_t;

/** Data of the power measurement samples */
typedef struct {
    float32_t average_value;
    float32_t average_time_value_milliseconds;
    float32_t min_value;
    float32_t max_value;
    uint32_t total_number_of_samples;
} hailo_power_measurement_data_t;

/** Additional scan params, for future compatibility */
typedef struct _hailo_scan_devices_params_t hailo_scan_devices_params_t;

/** PCIe device information */
typedef struct {
    uint32_t domain;
    uint32_t bus;
    uint32_t device;
    uint32_t func;
} hailo_pcie_device_info_t;

/** Hailo device ID string - BDF for PCIe devices, IP address for Ethernet devices. **/
typedef struct {
    char id[HAILO_MAX_DEVICE_ID_LENGTH];
} hailo_device_id_t;

/** Hailo device type */
typedef enum {
    HAILO_DEVICE_TYPE_PCIE,
    HAILO_DEVICE_TYPE_ETH,
    HAILO_DEVICE_TYPE_INTEGRATED,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DEVICE_TYPE_MAX_ENUM = HAILO_MAX_ENUM
} hailo_device_type_t;

/** Scheduler algorithm */
typedef enum hailo_scheduling_algorithm_e {
    /** Scheduling disabled */
    HAILO_SCHEDULING_ALGORITHM_NONE = 0,
    /** Round Robin */
    HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN,

    /** Max enum value to maintain ABI Integrity */
    HAILO_SCHEDULING_ALGORITHM_MAX_ENUM = HAILO_MAX_ENUM
} hailo_scheduling_algorithm_t;

/** Virtual device parameters */
typedef struct {
    /**
     * Requested number of physical devices. if @a device_ids is not NULL represents
     * the number of ::hailo_device_id_t in @a device_ids.
     */
    uint32_t device_count;

    /**
     * Specific device ids to create the vdevice from. If NULL, the vdevice will try to occupy
     * devices from the available pool.
     */
    hailo_device_id_t *device_ids;

    /** The scheduling algorithm to use for network group scheduling */
    hailo_scheduling_algorithm_t scheduling_algorithm;
    /** Key for using a shared VDevice. To create a unique VDevice, use HAILO_UNIQUE_VDEVICE_GROUP_ID */
    const char *group_id;
    /** Flag specifies whether to create the VDevice in HailoRT service or not. Defaults to false */
    bool multi_process_service;
} hailo_vdevice_params_t;

/** Device architecture */
typedef enum hailo_device_architecture_e {
    HAILO_ARCH_HAILO8_A0 = 0,
    HAILO_ARCH_HAILO8,
    HAILO_ARCH_HAILO8L,
    HAILO_ARCH_HAILO15H,
    HAILO_ARCH_HAILO15L,
    HAILO_ARCH_HAILO15M,
    HAILO_ARCH_HAILO10H,
    HAILO_ARCH_MARS,

    /** Max enum value to maintain ABI Integrity */
    HAILO_ARCH_MAX_ENUM = HAILO_MAX_ENUM
} hailo_device_architecture_t;

typedef enum {
    HAILO_CPU_ID_0 = 0,
    HAILO_CPU_ID_1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_CPU_ID_MAX_ENUM = HAILO_MAX_ENUM
} hailo_cpu_id_t;

/** Hailo firmware version */
typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
} hailo_firmware_version_t;

/** Hailo device identity */
typedef struct {
    uint32_t protocol_version;
    hailo_firmware_version_t fw_version;
    uint32_t logger_version;
    uint8_t board_name_length;
    char board_name[HAILO_MAX_BOARD_NAME_LENGTH];
    bool is_release;
    bool extended_context_switch_buffer;
    bool extended_fw_check;
    hailo_device_architecture_t device_architecture;
    uint8_t serial_number_length;
    char serial_number[HAILO_MAX_SERIAL_NUMBER_LENGTH];
    uint8_t part_number_length;
    char part_number[HAILO_MAX_PART_NUMBER_LENGTH];
    uint8_t product_name_length;
    char product_name[HAILO_MAX_PRODUCT_NAME_LENGTH];
} hailo_device_identity_t;

typedef struct {
    bool is_release;
    bool extended_context_switch_buffer;
    bool extended_fw_check;
    hailo_firmware_version_t fw_version;
} hailo_core_information_t;

/* Hailo device boot source */
typedef enum {
    HAILO_DEVICE_BOOT_SOURCE_INVALID = 0,
    HAILO_DEVICE_BOOT_SOURCE_PCIE,
    HAILO_DEVICE_BOOT_SOURCE_FLASH,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DEVICE_BOOT_SOURCE_MAX = HAILO_MAX_ENUM
} hailo_device_boot_source_t;

/** Hailo device supported features */
typedef struct {
    /** Is ethernet supported */
    bool ethernet;
    /** Is mipi supported */
    bool mipi;
    /** Is pcie supported */
    bool pcie;
    /** Is current monitoring supported */
    bool current_monitoring;
    /** Is current mdio supported */
    bool mdio;
    /** Is power measurement supported */
    bool power_measurement;
} hailo_device_supported_features_t;

/** Hailo extended device information */
typedef struct {
    /** The core clock rate */
    uint32_t neural_network_core_clock_rate;
    /** Hailo device supported features */
    hailo_device_supported_features_t supported_features;
    /** Device boot source */
    hailo_device_boot_source_t boot_source;
    /** SOC id */
    uint8_t soc_id[HAILO_SOC_ID_LENGTH];
    /** Device lcs */
    uint8_t lcs;
    /** Device Ethernet Mac address */
    uint8_t eth_mac_address[HAILO_ETH_MAC_LENGTH];
    /** Hailo device unit level tracking id*/
    uint8_t unit_level_tracking_id[HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH];
    /** Hailo device pm values */
    uint8_t soc_pm_values[HAILO_SOC_PM_VALUES_BYTES_LENGTH];
    /** Hailo device GPIO mask values */
    uint16_t gpio_mask;
} hailo_extended_device_information_t;

/** Endianness (byte order) */
typedef enum {
    HAILO_BIG_ENDIAN = 0,
    HAILO_LITTLE_ENDIAN = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_ENDIANNESS_MAX_ENUM = HAILO_MAX_ENUM
} hailo_endianness_t;

/** I2C slave configuration */
typedef struct {
    hailo_endianness_t endianness;
    uint16_t slave_address;
    uint8_t register_address_size;
    uint8_t bus_index;
    bool should_hold_bus;
} hailo_i2c_slave_config_t;

/** Firmware user config information */
typedef struct {
    uint32_t version;
    uint32_t entry_count;
    uint32_t total_size;
} hailo_fw_user_config_information_t;

/** Data format types */
typedef enum {
    /**
     * Chosen automatically to match the format expected by the device, usually UINT8.
     * Can be checked using ::hailo_stream_info_t format.type.
     */
    HAILO_FORMAT_TYPE_AUTO                  = 0,

    /** Data format type uint8_t - 1 byte per item, host/device side */
    HAILO_FORMAT_TYPE_UINT8                 = 1,

    /** Data format type uint16_t - 2 bytes per item, host/device side */
    HAILO_FORMAT_TYPE_UINT16                = 2,

    /** Data format type float32_t - used only on host side (Translated in the quantization process) */
    HAILO_FORMAT_TYPE_FLOAT32               = 3,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FORMAT_TYPE_MAX_ENUM              = HAILO_MAX_ENUM
} hailo_format_type_t;

/**
 * Data format orders, i.e. how the rows, columns and features are ordered:
 *  - N: Number of images in the batch
 *  - H: Height of the image
 *  - W: Width of the image
 *  - C: Number of channels of the image (e.g. 3 for RGB, 1 for grayscale...)
 */
typedef enum {
    /**
     * Chosen automatically to match the format expected by the device.
     */
    HAILO_FORMAT_ORDER_AUTO                             = 0,

    /**
     *  - Host side: [N, H, W, C]
     *  - Device side: [N, H, W, C], where width is padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_NHWC                             = 1,

    /**
     *  - Not used for host side
     *  - Device side: [N, H, C, W], where width is padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_NHCW                             = 2,

    /**
     * FCR means first channels (features) are sent to HW:
     *  - Host side: [N, H, W, C]
     *  - Device side: [N, H, W, C]:
     *      - Input - channels are expected to be aligned to 8 bytes
     *      - Output - width is padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_FCR                              = 3,

    /**
     * F8CR means first 8-channels X width are sent to HW:
     *  - Host side: [N, H, W, C]
     *  - Device side: [N, H, W, 8C], where channels are padded to 8 elements:
     *  - ROW1:
     *      - W X 8C_1, W X 8C_2, ... , W X 8C_n
     *  - ROW2:
     *      - W X 8C_1, W X 8C_2, ... , W X 8C_n
     * ...
     */
    HAILO_FORMAT_ORDER_F8CR                             = 4,

    /**
     * Output format of argmax layer:
     * - Host side: [N, H, W, 1]
     * - Device side: [N, H, W, 1], where width is padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_NHW                              = 5,

    /**
     * Channels only:
     * - Host side: [N,C]
     * - Device side: [N, C], where channels are padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_NC                               = 6,

    /**
     * Bayer format:
     * - Host side: [N, H, W, 1]
     * - Device side: [N, H, W, 1], where width is padded to 8 bytes
     */
    HAILO_FORMAT_ORDER_BAYER_RGB                        = 7,

    /**
     * Bayer format, same as ::HAILO_FORMAT_ORDER_BAYER_RGB where
     * Channel is 12-bit
     */
    HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB                 = 8,

    /**
     * Deprecated. Should use HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS, HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE (user formats)
     * or HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP (device format) instead.
     */
    HAILO_FORMAT_ORDER_HAILO_NMS                        = 9,

    /**
     * - Not used for host side
     * - Device side: [N, H, W, C], where channels are 4 (RGB + 1 padded zero byte) and width is padded to 8 elements
     */
    HAILO_FORMAT_ORDER_RGB888                           = 10,

    /**
     * - Host side: [N, C, H, W]
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_NCHW                             = 11,

    /**
     * YUV format, encoding 2 pixels in 32 bits
     *      [Y0, U0, Y1, V0] represents [Y0, U0, V0], [Y1, U0, V0]
     * - Host side: [Y0, U0, Y1, V0]
     * - Device side: [Y0, U0, Y1, V0]
     */
    HAILO_FORMAT_ORDER_YUY2                             = 12,

    /**
     * YUV format, encoding 8 pixels in 96 bits
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, U0, V0, U1, V1] represents
     *          [Y0, U0, V0], [Y1, U0, V0], [Y2, U0, V0], [Y3, U0, V0], [Y4, U1, V1], [Y5, U1, V1], [Y6, U1, V1], [Y7, U1, V1]
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_NV12                             = 13,

    /**
     * YUV format, encoding 8 pixels in 96 bits
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, V0, U0, V1, U1] represents
     *          [Y0, V0, U0], [Y1, V0, U0], [Y2, V0, U0], [Y3, V0, U0], [Y4, V1, U1], [Y5, V1, U1], [Y6, V1, U1], [Y7, V1, U1]
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_NV21                             = 14,

    /**
     * Internal implementation for HAILO_FORMAT_ORDER_NV12 format
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, U0, V0, U1, V1] is represented by [Y0, Y1, Y2, Y3, U0, V0, Y4, Y5, Y6, Y7, U1, V1]
     * - Not used for host side
     */
    HAILO_FORMAT_ORDER_HAILO_YYUV                       = 15,

    /**
     * Internal implementation for HAILO_FORMAT_ORDER_NV21 format
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, V0, U0, V1, U1] is represented by [Y0, Y1, Y2, Y3, V0, U0, Y4, Y5, Y6, Y7, V1, U1]
     * - Not used for host side
     */
    HAILO_FORMAT_ORDER_HAILO_YYVU                       = 16,

    /**
     * RGB, where every row is padded to 4.
     * - Host side: [N, H, W, C], where width*channels are padded to 4.
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_RGB4                             = 17,

    /**
     * YUV format, encoding 8 pixels in 96 bits
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, U0, U1, V0, V1] represents
     *          [Y0, U0, V0,], [Y1, U0, V0], [Y2, U0, V0], [Y3, U0, V0], [Y4, U1, V1], [Y5, U1, V1], [Y6, U1, V1], [Y7, U1, V1]
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_I420                             = 18,

    /**
     * Internal implementation for HAILO_FORMAT_ORDER_I420 format
     *      [Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, U0, U1, V0, V1] is represented by [Y0, Y1, Y2, Y3, U0, V0, Y4, Y5, Y6, Y7, U1, V1]
     * - Not used for host side
     */
    HAILO_FORMAT_ORDER_HAILO_YYYYUV                     = 19,

    /**
     * NMS_WITH_BYTE_MASK format
     *
     * - Host side
     *      \code
     *      struct (packed) {
     *          uint16_t detections_count;
     *          hailo_detection_with_byte_mask_t[detections_count];
     *      };
     *      \endcode
     *
     *      The host format type supported ::HAILO_FORMAT_TYPE_UINT8.
     *
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK         = 20,

    /**
     * NMS bbox
     * - Device side
     *
     *      Result of NMS layer on chip (Internal implementation)
     *
     * - Not used for host side
     */
    HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP                = 21,

    /**
     * NMS bbox
     * - Host side
     *
     *      For each class (::hailo_nms_shape_t.number_of_classes), the layout is
     *          \code
     *          struct (packed) {
     *              float32_t bbox_count;
     *              hailo_bbox_float32_t bbox[bbox_count];
     *          };
     *          \endcode
     *
     *
     *      Maximum amount of bboxes per class is ::hailo_nms_shape_t.max_bboxes_per_class.
     *
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS               = 22,

    /**
     * NMS bbox
     * - Host side
     *
     *      For all classes the layout is
     *          \code
     *          struct (packed) {
     *              uint16_t bbox_count;
     *              hailo_detection_t bbox[bbox_count];
     *          };
     *          \endcode
     *
     *
     *      Maximum amount of bboxes is ::hailo_nms_shape_t.max_bboxes_total.
     *      It is possible to use ::hailo_detections_t to parse the data.
     *
     *      The host format type supported ::HAILO_FORMAT_TYPE_UINT8.
     *
     * - Not used for device side
     */
    HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE               = 23,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FORMAT_ORDER_MAX_ENUM             = HAILO_MAX_ENUM
} hailo_format_order_t;

/** Data format flags */
typedef enum {
    HAILO_FORMAT_FLAGS_NONE                 = 0,

    /**
     * If not set, HailoRT performs the quantization (scaling) step.
     * If set:
     * - Input data: HailoRT assumes that the data is already quantized (scaled) by the user,
     *   so it does not perform the quantization (scaling) step.
     * - Output data: The data will be returned to the user without rescaling (i.e., the data won't be rescaled by HailoRT).
     * @note This flag is deprecated and its usage is ignored. Determine whether to quantize (or de-quantize) the data will be decided by
     *       the src-data and dst-data types.
     */
    HAILO_FORMAT_FLAGS_QUANTIZED            = 1 << 0,

    /**
     * If set, the frame height/width are transposed. Supported orders:
     *      - ::HAILO_FORMAT_ORDER_NHWC
     *      - ::HAILO_FORMAT_ORDER_NHW
     *      - ::HAILO_FORMAT_ORDER_BAYER_RGB
     *      - ::HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB
     *      - ::HAILO_FORMAT_ORDER_FCR
     *      - ::HAILO_FORMAT_ORDER_F8CR
     * 
     * When set on host side, ::hailo_stream_info_t shape of the stream will be a transposed version of the host buffer
     * (The height and width will be swapped)
     */
    HAILO_FORMAT_FLAGS_TRANSPOSED          = 1 << 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FORMAT_FLAGS_MAX_ENUM             = HAILO_MAX_ENUM
} hailo_format_flags_t;

/** Hailo data format */
typedef struct {
    hailo_format_type_t type;
    hailo_format_order_t order;
    hailo_format_flags_t flags;
} hailo_format_t;

/** Indicates how transformations on the data should be done */
typedef enum {
    /** The vstream will not run the transformation (The data will be in hw format) */
    HAILO_STREAM_NO_TRANSFORM               = 0,

    /** The transformation process will be part of the vstream send/recv (The data will be in host format). */
    HAILO_STREAM_TRANSFORM_COPY             = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_STREAM_MAX_ENUM                   = HAILO_MAX_ENUM
} hailo_stream_transform_mode_t;

/** Stream direction - host to device or device to host */
typedef enum {
    HAILO_H2D_STREAM                    = 0,
    HAILO_D2H_STREAM                    = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_STREAM_DIRECTION_MAX_ENUM     = HAILO_MAX_ENUM
} hailo_stream_direction_t;

/** Stream flags */
typedef enum {
    HAILO_STREAM_FLAGS_NONE     = 0,        /*!< No flags */
    HAILO_STREAM_FLAGS_ASYNC    = 1 << 0,   /*!< Async stream */

    /** Max enum value to maintain ABI Integrity */
    HAILO_STREAM_FLAGS_MAX_ENUM     = HAILO_MAX_ENUM
} hailo_stream_flags_t;

/** Hailo dma buffer direction */
typedef enum {
    /** Buffers sent from the host (H) to the device (D). Used for input streams */
    HAILO_DMA_BUFFER_DIRECTION_H2D    = 0,

    /** Buffers received from the device (D) to the host (H). Used for output streams */
    HAILO_DMA_BUFFER_DIRECTION_D2H    = 1,

    /** Buffers can be used both send to the device and received from the device */
    HAILO_DMA_BUFFER_DIRECTION_BOTH   = 2,

    /** Max enum value to maintain ABI Integrity */
    HAILO_DMA_BUFFER_DIRECTION_MAX_ENUM  = HAILO_MAX_ENUM
} hailo_dma_buffer_direction_t;

// ************************************* NOTE - START ************************************* //
// Dma buffer allocation isn't currently supported and is for internal use only             //
// **************************************************************************************** //
/** Hailo buffer flags */
typedef enum {
    HAILO_BUFFER_FLAGS_NONE             = 0,        /*!< No flags - heap allocated buffer */
    HAILO_BUFFER_FLAGS_DMA              = 1 << 0,   /*!< Buffer is mapped to DMA (will be page aligned implicitly) */
    HAILO_BUFFER_FLAGS_CONTINUOUS       = 1 << 1,   /*!< Buffer is physically continuous (will be page aligned implicitly) */
    HAILO_BUFFER_FLAGS_SHARED_MEMORY    = 1 << 2,   /*!< Buffer is shared memory (will be page aligned implicitly) */

    /** Max enum value to maintain ABI Integrity */
    HAILO_BUFFER_FLAGS_MAX_ENUM     = HAILO_MAX_ENUM
} hailo_buffer_flags_t;

/** Hailo buffer parameters */
typedef struct {
    hailo_buffer_flags_t flags;
} hailo_buffer_parameters_t;
// ************************************** NOTE - END ************************************** //
// Dma buffer allocation isn't currently supported and is for internal use only             //
// **************************************************************************************** //

/** Input or output data transform parameters */
typedef struct {
    hailo_stream_transform_mode_t transform_mode;
    hailo_format_t user_buffer_format;
} hailo_transform_params_t;

/** Demuxer params */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_demux_params_t;

/** Quantization information.
 * Property of ::hailo_stream_info_t, ::hailo_vstream_info_t.
 * Hailo devices require input data to be quantized/scaled before it is sent. Similarly, data outputted
 * from the device needs to be 'de-quantized'/rescaled as well.
 * Each input/output layer is assigned two floating point values that are parameters to an input/output
 * transformation: qp_zp (zero_point) and qp_scale. These values are stored in the HEF.
 * - Input transformation: Input data is divided by qp_scale and then qp_zp is added to the result.
 * - Output transformation: qp_zp is subtracted from output data and then the result is multiplied by qp_scale.
 *
 * If the output's quant_info is `INVALID_QUANT_INFO`, it means there are multiple quant_infos. In that case,
 * use ::hailo_get_output_stream_quant_infos or ::hailo_get_output_vstream_quant_infos to get the quant info list.
*/
typedef struct {
    /** zero_point */
    float32_t qp_zp;

    /** scale */
    float32_t qp_scale;

    /** min limit value */
    float32_t limvals_min;

    /** max limit value */
    float32_t limvals_max;
} hailo_quant_info_t;

/** PCIe input stream (host to device) parameters */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_pcie_input_stream_params_t;

/** PCIe output stream (device to host) parameters */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_pcie_output_stream_params_t;


/** Core input stream (host to device) parameters */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_integrated_input_stream_params_t;

/** Core output stream (device to host) parameters */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_integrated_output_stream_params_t;

typedef enum {
    HAILO_STREAM_INTERFACE_PCIE = 0,
    HAILO_STREAM_INTERFACE_ETH,
    HAILO_STREAM_INTERFACE_MIPI,
    HAILO_STREAM_INTERFACE_INTEGRATED,

    /** Max enum value to maintain ABI Integrity */
   HAILO_STREAM_INTERFACE_MAX_ENUM = HAILO_MAX_ENUM
} hailo_stream_interface_t;

/** Hailo stream parameters */
typedef struct {
    hailo_stream_interface_t stream_interface;
    hailo_stream_direction_t direction;
    hailo_stream_flags_t flags;
    union {
        hailo_pcie_input_stream_params_t pcie_input_params;
        hailo_integrated_input_stream_params_t integrated_input_params;
        hailo_pcie_output_stream_params_t pcie_output_params;
        hailo_integrated_output_stream_params_t integrated_output_params;
    };
} hailo_stream_parameters_t;

/** Hailo stream parameters per stream_name */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_stream_parameters_t stream_params;
} hailo_stream_parameters_by_name_t;

/** Virtual stream statistics flags */
typedef enum {
    HAILO_VSTREAM_STATS_NONE            = 0,        /*!< No stats */
    HAILO_VSTREAM_STATS_MEASURE_FPS     = 1 << 0,   /*!< Measure vstream FPS */
    HAILO_VSTREAM_STATS_MEASURE_LATENCY = 1 << 1,   /*!< Measure vstream latency */

    /** Max enum value to maintain ABI Integrity */
    HAILO_VSTREAM_STATS_MAX_ENUM        = HAILO_MAX_ENUM
} hailo_vstream_stats_flags_t;

/** Pipeline element statistics flags */
typedef enum {
    HAILO_PIPELINE_ELEM_STATS_NONE                  = 0,        /*!< No stats */
    HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS           = 1 << 0,   /*!< Measure element FPS */
    HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY       = 1 << 1,   /*!< Measure element latency */
    HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE    = 1 << 2,   /*!< Measure element queue size */

    /** Max enum value to maintain ABI Integrity */
    HAILO_PIPELINE_ELEM_STATS_MAX_ENUM              = HAILO_MAX_ENUM
} hailo_pipeline_elem_stats_flags_t;

/** Virtual stream params */
typedef struct {
    hailo_format_t user_buffer_format;
    uint32_t timeout_ms;
    uint32_t queue_size;
    hailo_vstream_stats_flags_t vstream_stats_flags;
    hailo_pipeline_elem_stats_flags_t pipeline_elements_stats_flags;
} hailo_vstream_params_t;

/** Input virtual stream parameters */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_vstream_params_t params;
} hailo_input_vstream_params_by_name_t;

/** Output virtual stream parameters */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_vstream_params_t params;
} hailo_output_vstream_params_by_name_t;

/** Output virtual stream name by group */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    uint8_t pipeline_group_index;
} hailo_output_vstream_name_by_group_t;

/** Image shape */
typedef struct {
    uint32_t height;
    uint32_t width;
    uint32_t features;
} hailo_3d_image_shape_t;

typedef enum
{
  HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR,
  HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF,
} hailo_pix_buffer_memory_type_t;

/** image buffer plane */
typedef struct {
    /** actual data */
    uint32_t bytes_used;
    uint32_t plane_size;
    /* Union in case the buffer is a user buffer or DMA buffer */
    union
    {
        void *user_ptr;
        int fd;
    };
} hailo_pix_buffer_plane_t;

/** image buffer */
typedef struct {
    uint32_t index;
    hailo_pix_buffer_plane_t planes[MAX_NUMBER_OF_PLANES];
    uint32_t number_of_planes;
    hailo_pix_buffer_memory_type_t memory_type;
} hailo_pix_buffer_t;

/** dma buffer - intended for use with Linux's dma-buf sub system */
typedef struct {
    int fd;
    size_t size;
} hailo_dma_buffer_t;

typedef struct {
    uint32_t class_group_index;
    char original_name[HAILO_MAX_STREAM_NAME_SIZE];
} hailo_nms_defuse_info_t;

typedef enum {
    HAILO_BURST_TYPE_H8_BBOX = 0,
    HAILO_BURST_TYPE_H15_BBOX,
    HAILO_BURST_TYPE_H8_PER_CLASS,
    HAILO_BURST_TYPE_H15_PER_CLASS,
    HAILO_BURST_TYPE_H15_PER_FRAME,

    HAILO_BURST_TYPE_COUNT
} hailo_nms_burst_type_t;

/** NMS Internal HW Info */
typedef struct {
    /** Amount of NMS classes */
    uint32_t number_of_classes;
    /** Maximum amount of bboxes per nms class */
    uint32_t max_bboxes_per_class;
    /** Maximum amount of total bboxes */
    uint32_t max_bboxes_total;
    /** Internal usage */
    uint32_t bbox_size;
    /** Internal usage */
    uint32_t chunks_per_frame;
    bool is_defused;
    hailo_nms_defuse_info_t defuse_info;
    /** Size of NMS burst in bytes */
    uint32_t burst_size;
    /** NMS burst type */
    hailo_nms_burst_type_t burst_type;
} hailo_nms_info_t;

/** NMS Fuse Input */
typedef struct {
    void *buffer;
    size_t size;
    hailo_nms_info_t nms_info;
} hailo_nms_fuse_input_t;

/** Shape of nms result */
typedef struct {
    /** Amount of NMS classes */
    uint32_t number_of_classes;
    /** Maximum amount of bboxes per nms class */
    uint32_t max_bboxes_per_class;
    /** Maximum amount of total bboxes */
    uint32_t max_bboxes_total;
    /** Maximum accumulated mask size for all of the detections in a frame.
     *  Used only with 'HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK' format order.
     *  The default value is (`input_image_size` * 2)
     */
    uint32_t max_accumulated_mask_size;
} hailo_nms_shape_t;

#pragma pack(push, 1)
typedef struct {
    uint16_t y_min;
    uint16_t x_min;
    uint16_t y_max;
    uint16_t x_max;
    uint16_t score;
} hailo_bbox_t;

typedef struct {
    float32_t y_min;
    float32_t x_min;
    float32_t y_max;
    float32_t x_max;
    float32_t score;
} hailo_bbox_float32_t;

typedef struct {
    float32_t y_min;
    float32_t x_min;
    float32_t y_max;
    float32_t x_max;
} hailo_rectangle_t;

typedef struct {
    float32_t y_min;
    float32_t x_min;
    float32_t y_max;
    float32_t x_max;
    float32_t score;
    uint16_t class_id;
} hailo_detection_t;

#if defined(_MSC_VER)
// TODO: warning C4200
#pragma warning(push)
#pragma warning(disable: 4200)
#endif
typedef struct {
    /** Number of detections */
    uint16_t count;

    /** Array of detections (it's size is determined by count field) */
    hailo_detection_t detections[0];
} hailo_detections_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
    /** Detection's box coordinates */
    hailo_rectangle_t box;

    /** Detection's score */
    float32_t score;

    /** Detection's class id */
    uint16_t class_id;

    /** Mask size in bytes */
    size_t mask_size;

    /**
     * Byte Mask:
     * The mask is a binary mask that defines a region of interest (ROI) of the image.
     * Mask pixel values of 1 indicate image pixels that belong to the ROI.
     * Mask pixel values of 0 indicate image pixels that are part of the background.
     *
     * The size of the mask is the size of the box, in the original input image's dimensions.
     * Mask width = ceil((box.x_max - box.x_min) * image_width)
     * Mask height = ceil((box.y_max - box.y_min) * image_height)
     * First pixel represents the pixel (x_min * image_width, y_min * image_height) in the original input image.
    */
    uint8_t *mask;
} hailo_detection_with_byte_mask_t;
#pragma pack(pop)

/**
 * Completion info struct passed to the ::hailo_stream_write_async_callback_t after the async operation is
 * done or has failed.
 */
typedef struct {
    /**
     * Status of the async transfer:
     *  - ::HAILO_SUCCESS - The transfer is complete.
     *  - ::HAILO_STREAM_ABORT - The transfer was canceled (can happen after network deactivation).
     *  - Any other ::hailo_status on unexpected errors.
     */
    hailo_status status;

    /** Address of the buffer passed to the async operation */
    const void *buffer_addr;

    /** Size of the buffer passed to the async operation. */
    size_t buffer_size;

    /** User specific data. Can be used as a context for the callback. */
    void *opaque;
} hailo_stream_write_async_completion_info_t;

/**
 * Async stream write complete callback prototype.
 */
typedef void (*hailo_stream_write_async_callback_t)(const hailo_stream_write_async_completion_info_t *info);

/**
 * Completion info struct passed to the ::hailo_stream_read_async_callback_t after the async operation is
 * done or has failed.
 */
typedef struct {
    /**
     * Status of the async transfer:
     *  - ::HAILO_SUCCESS - The transfer is complete.
     *  - ::HAILO_STREAM_ABORT - The transfer was canceled (can happen after network deactivation).
     *  - Any other ::hailo_status on unexpected errors.
     */
    hailo_status status;

    /** Address of the buffer passed to the async operation */
    void *buffer_addr;

    /** Size of the buffer passed to the async operation. */
    size_t buffer_size;

    /** User specific data. Can be used as a context for the callback. */
    void *opaque;
} hailo_stream_read_async_completion_info_t;
/**

 * Async stream read complete callback prototype.
 */
typedef void (*hailo_stream_read_async_callback_t)(const hailo_stream_read_async_completion_info_t *info);

/**
 * Input or output stream information. In case of multiple inputs or outputs, each one has
 * its own stream.
 */
typedef struct {
    /* Union to contain shapes and nms parameters - they cannot exist at the same time */
    union
    {
        struct
        {
            hailo_3d_image_shape_t shape;
            hailo_3d_image_shape_t hw_shape;
        };

        hailo_nms_info_t nms_info;
    };

    uint32_t hw_data_bytes;
    uint32_t hw_frame_size;
    hailo_format_t format;
    hailo_stream_direction_t direction;
    uint8_t index;
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_quant_info_t quant_info;
    bool is_mux;
} hailo_stream_info_t;

/**
 * Input or output vstream information.
 */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    char network_name[HAILO_MAX_NETWORK_NAME_SIZE];
    hailo_stream_direction_t direction;
    /* Buffer format sent/received from the vstream. The user normally override this format 
       by passing its own user_buffer_format inside ::hailo_vstream_params_t structure.*/
    hailo_format_t format;

    union
    {
        /* Frame shape */
        hailo_3d_image_shape_t shape;
        /* NMS shape, only valid if format.order is one of the NMS orders */
        hailo_nms_shape_t nms_shape;
    };

    hailo_quant_info_t quant_info;
} hailo_vstream_info_t;

/** Power modes */
typedef enum {
    HAILO_POWER_MODE_PERFORMANCE       = 0,
    HAILO_POWER_MODE_ULTRA_PERFORMANCE = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_POWER_MODE_MAX_ENUM          = HAILO_MAX_ENUM
} hailo_power_mode_t;

/** Latency measurement flags */
typedef enum {
    HAILO_LATENCY_NONE              = 0,
    HAILO_LATENCY_MEASURE           = 1 << 0,
    HAILO_LATENCY_CLEAR_AFTER_GET   = 1 << 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_LATENCY_MAX_ENUM          = HAILO_MAX_ENUM
} hailo_latency_measurement_flags_t;

typedef struct {
    /**
     * This parameter determines the number of frames that will be sent for inference in a single batch.
     * If a scheduler is enabled, this parameter determines the 'burst size' - the max number of frames after which the scheduler will attempt
     *  to switch to another model.
     * If scheduler is disabled, the number of frames for inference should be a multiplication of batch_size (unless model is in single context).
     *
     * User is advised to modify this (single network parameter) or @a hailo_configure_network_group_params_t batch size parameter. Not both.
     * In case user wishes to work with the same batch size for all networks inside a network group, user is advised to set batch_size in @a hailo_configure_network_group_params_t.
     * In case user wished to work with batch size per network, user is advised to use this parameter.

     * @note The default value is @a HAILO_DEFAULT_BATCH_SIZE - which means the batch is determined by HailoRT automatically.
     */
    uint16_t batch_size;
} hailo_network_parameters_t;

typedef struct {
    char name[HAILO_MAX_NETWORK_NAME_SIZE];
    hailo_network_parameters_t network_params;
} hailo_network_parameters_by_name_t;

/** Hailo configure parameters per network_group */
typedef struct {
    char name[HAILO_MAX_NETWORK_GROUP_NAME_SIZE];
    /** This parameter is only used in multi-context network_groups. In case of name missmatch, default value @a HAILO_DEFAULT_BATCH_SIZE is used */
    uint16_t batch_size;
    hailo_power_mode_t power_mode;
    hailo_latency_measurement_flags_t latency;
    bool enable_kv_cache;
    size_t stream_params_by_name_count;
    hailo_stream_parameters_by_name_t stream_params_by_name[HAILO_MAX_STREAMS_COUNT];
    size_t network_params_by_name_count;
    hailo_network_parameters_by_name_t network_params_by_name[HAILO_MAX_NETWORKS_IN_NETWORK_GROUP];
} hailo_configure_network_group_params_t;

/** Hailo configure parameters */
typedef struct {
    size_t network_group_params_count;
    hailo_configure_network_group_params_t network_group_params[HAILO_MAX_NETWORK_GROUPS];
} hailo_configure_params_t;

/** Hailo network_group parameters */
typedef struct {
    EMPTY_STRUCT_PLACEHOLDER
} hailo_activate_network_group_params_t;

/** Hailo network group info */
typedef struct {
    char name[HAILO_MAX_NETWORK_GROUP_NAME_SIZE];
    bool is_multi_context;
} hailo_network_group_info_t;

/** Hailo layer name */
typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
} hailo_layer_name_t;

typedef struct {
    char name[HAILO_MAX_NETWORK_NAME_SIZE];
} hailo_network_info_t;

/** Notification IDs and structures section start */
#pragma pack(push, 1)

/** Notification IDs, for each notification, one of the ::hailo_notification_message_parameters_t union will be set. */
typedef enum {
    /** Matches hailo_notification_message_parameters_t::rx_error_notification. */
    HAILO_NOTIFICATION_ID_ETHERNET_RX_ERROR = 0,
    /** Matches hailo_notification_message_parameters_t::health_monitor_temperature_alarm_notification */
    HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM,
    /** Matches hailo_notification_message_parameters_t::health_monitor_dataflow_shutdown_notification */
    HAILO_NOTIFICATION_ID_HEALTH_MONITOR_DATAFLOW_SHUTDOWN,
    /** Matches hailo_notification_message_parameters_t::health_monitor_overcurrent_alert_notification */
    HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM,
    /** Matches hailo_notification_message_parameters_t::health_monitor_lcu_ecc_error_notification */
    HAILO_NOTIFICATION_ID_LCU_ECC_CORRECTABLE_ERROR,
    /** Matches hailo_notification_message_parameters_t::health_monitor_lcu_ecc_error_notification */
    HAILO_NOTIFICATION_ID_LCU_ECC_UNCORRECTABLE_ERROR,
    /** Matches hailo_notification_message_parameters_t::health_monitor_cpu_ecc_notification */
    HAILO_NOTIFICATION_ID_CPU_ECC_ERROR,
    /** Matches hailo_notification_message_parameters_t::health_monitor_cpu_ecc_notification */
    HAILO_NOTIFICATION_ID_CPU_ECC_FATAL,
    /** Matches hailo_notification_message_parameters_t::debug_notification */
    HAILO_NOTIFICATION_ID_DEBUG,
    /** Matches hailo_notification_message_parameters_t::context_switch_breakpoint_reached_notification */
    HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_BREAKPOINT_REACHED,
    /** Matches hailo_notification_message_parameters_t::health_monitor_clock_changed_notification */
    HAILO_NOTIFICATION_ID_HEALTH_MONITOR_CLOCK_CHANGED_EVENT,
    /** Matches hailo_notification_message_parameters_t::hailo_hw_infer_manager_infer_done_notification */
    HAILO_NOTIFICATION_ID_HW_INFER_MANAGER_INFER_DONE,
    /** Matches hailo_notification_message_parameters_t::context_switch_run_time_error */
    HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_RUN_TIME_ERROR_EVENT,
    /** Matches hailo_notification_message_parameters_t::nn_core_crc_error */
    HAILO_NOTIFICATION_ID_NN_CORE_CRC_ERROR_EVENT,

    /** Must be last! */
    HAILO_NOTIFICATION_ID_COUNT,

    /** Max enum value to maintain ABI Integrity */
    HAILO_NOTIFICATION_ID_MAX_ENUM = HAILO_MAX_ENUM
} hailo_notification_id_t;

/** Rx error notification message */
typedef struct {
    uint32_t error;
    uint32_t queue_number;
    uint32_t rx_errors_count;
} hailo_rx_error_notification_message_t;

/** Debug notification message */
typedef struct {
    uint32_t connection_status;
    uint32_t connection_type;
    uint32_t vdma_is_active;
    uint32_t host_port;
    uint32_t host_ip_addr;
} hailo_debug_notification_message_t;

/** Health monitor - Dataflow shutdown notification message */
typedef struct {
    float32_t ts0_temperature;
    float32_t ts1_temperature;
} hailo_health_monitor_dataflow_shutdown_notification_message_t;

typedef enum {
    HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__GREEN = 0,
    HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__ORANGE = 1,
    HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__RED = 2
} hailo_temperature_protection_temperature_zone_t;

/** Health monitor - Temperature alarm notification message */
typedef struct {
    hailo_temperature_protection_temperature_zone_t temperature_zone;
    uint32_t alarm_ts_id;
    float32_t ts0_temperature;
    float32_t ts1_temperature;
} hailo_health_monitor_temperature_alarm_notification_message_t;

typedef enum {
    HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__GREEN = 0,
    HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__RED = 1
} hailo_overcurrent_protection_overcurrent_zone_t;

/** Health monitor - Overcurrent alert notification message */
typedef struct {
    hailo_overcurrent_protection_overcurrent_zone_t overcurrent_zone;
    float32_t exceeded_alert_threshold;
    bool is_last_overcurrent_violation_reached;
} hailo_health_monitor_overcurrent_alert_notification_message_t;

/** Health monitor - LCU ECC error notification message */
typedef struct {
    /* bitmap - bit per cluster */
    uint16_t cluster_error;
} hailo_health_monitor_lcu_ecc_error_notification_message_t;

/** Health monitor - CPU ECC error notification message */
typedef struct {
    uint32_t memory_bitmap;
} hailo_health_monitor_cpu_ecc_notification_message_t;

/** Performance stats (value of '-1' in any field indicates that there was an error retrieving that specific stat) */
typedef struct {
    /** Percentage */
    float32_t cpu_utilization;
    /** Bytes */
    int64_t ram_size_total;
    /** Bytes */
    int64_t ram_size_used;
    /** Percentage */
    float32_t nnc_utilization;
    /** Per second (not implemented)*/
    int32_t ddr_noc_total_transactions;
    /** Percentage (round numbers between 1-100) */
    int32_t dsp_utilization;
} hailo_performance_stats_t;

/** Health stats (value of '-1' in any field indicates that there was an error retrieving that specific stat) */
typedef struct {
    /** Degrees celsius */
    float32_t on_die_temperature;
    /** mV */
    int32_t on_die_voltage;
    /** Bit mask */
    int32_t bist_failure_mask;
} hailo_health_stats_t;

/** Context switch - breakpoint reached notification message */
typedef struct {
    uint8_t network_group_index;
    uint32_t batch_index;
    uint16_t context_index;
    uint16_t action_index;
} hailo_context_switch_breakpoint_reached_message_t;

/** Health monitor - System's clock has been changed notification message */
typedef struct {
    uint32_t previous_clock;
    uint32_t current_clock;
} hailo_health_monitor_clock_changed_notification_message_t;

typedef struct {
    uint32_t infer_cycles;
} hailo_hw_infer_manager_infer_done_notification_message_t;

typedef struct {
    uint64_t cache_id_bitmask;
} hailo_start_update_cache_offset_notification_message_t;

typedef struct {
    uint32_t exit_status;
    uint8_t network_group_index;
    uint16_t batch_index;
    uint16_t context_index;
    uint16_t action_index;
} hailo_context_switch_run_time_error_message_t;

/** Union of all notification messages parameters. See ::hailo_notification_t */
typedef union {
    /** Ethernet rx error */
    hailo_rx_error_notification_message_t rx_error_notification;
    /** Internal usage */
    hailo_debug_notification_message_t debug_notification;
    /** Dataflow shutdown due to health monitor event */
    hailo_health_monitor_dataflow_shutdown_notification_message_t health_monitor_dataflow_shutdown_notification;
    /** Chip temperature alarm */
    hailo_health_monitor_temperature_alarm_notification_message_t health_monitor_temperature_alarm_notification;
    /** Chip overcurrent alert */
    hailo_health_monitor_overcurrent_alert_notification_message_t health_monitor_overcurrent_alert_notification;
    /** Core ecc error notification */
    hailo_health_monitor_lcu_ecc_error_notification_message_t health_monitor_lcu_ecc_error_notification;
    /** Chip ecc error notification */
    hailo_health_monitor_cpu_ecc_notification_message_t health_monitor_cpu_ecc_notification;
    /** Internal usage */
    hailo_context_switch_breakpoint_reached_message_t context_switch_breakpoint_reached_notification;
    /** Neural network core clock changed due to health monitor event */
    hailo_health_monitor_clock_changed_notification_message_t health_monitor_clock_changed_notification;
    /* HW infer manager finished infer notification */
    hailo_hw_infer_manager_infer_done_notification_message_t hw_infer_manager_infer_done_notification;
    /** context switch run time error event */
    hailo_context_switch_run_time_error_message_t context_switch_run_time_error;
    /** Start cache offset update notification */
    hailo_start_update_cache_offset_notification_message_t start_update_cache_offset_notification;
} hailo_notification_message_parameters_t;

/** Notification data that will be passed to the callback passed in ::hailo_notification_callback */
typedef struct {
    hailo_notification_id_t id;
    uint32_t sequence;
    hailo_notification_message_parameters_t body;
} hailo_notification_t;

#pragma pack(pop)
/** Notification IDs and structures section end */

/**
 * A notification callback. See ::hailo_set_notification_callback
 *
 * @param[in] device                The ::hailo_device that got the notification.
 * @param[in] notification          The notification data.
 * @param[in] opaque                User specific data.
 * @warning Throwing exceptions in the callback is not supported!
 */
typedef void (*hailo_notification_callback)(hailo_device device, const hailo_notification_t *notification, void *opaque);
/** Hailo device reset modes */
typedef enum {
    HAILO_RESET_DEVICE_MODE_CHIP        = 0,
    HAILO_RESET_DEVICE_MODE_NN_CORE     = 1,
    HAILO_RESET_DEVICE_MODE_SOFT        = 2,
    HAILO_RESET_DEVICE_MODE_FORCED_SOFT = 3,

    HAILO_RESET_DEVICE_MODE_MAX_ENUM    = HAILO_MAX_ENUM
} hailo_reset_device_mode_t;

typedef enum {
    HAILO_WATCHDOG_MODE_HW_SW           = 0,
    HAILO_WATCHDOG_MODE_HW_ONLY         = 1,

    HAILO_WATCHDOG_MODE_MAX_ENUM        = HAILO_MAX_ENUM
} hailo_watchdog_mode_t;

/**
 * Hailo chip temperature info. The temperature is in Celsius.
 */
typedef struct {
    float32_t ts0_temperature;
    float32_t ts1_temperature;
    uint16_t sample_count;
} hailo_chip_temperature_info_t;

typedef struct {
    float32_t temperature_threshold;
    float32_t hysteresis_temperature_threshold;
    uint32_t throttling_nn_clock_freq;
} hailo_throttling_level_t;

typedef struct {
    bool overcurrent_protection_active;
    uint8_t current_overcurrent_zone;
    float32_t red_overcurrent_threshold;
    bool overcurrent_throttling_active;
    bool temperature_throttling_active;
    uint8_t current_temperature_zone;
    int8_t current_temperature_throttling_level;
    hailo_throttling_level_t temperature_throttling_levels[HAILO_MAX_TEMPERATURE_THROTTLING_LEVELS_NUMBER];
    int32_t orange_temperature_threshold;
    int32_t orange_hysteresis_temperature_threshold;
    int32_t red_temperature_threshold;
    int32_t red_hysteresis_temperature_threshold;
    uint32_t requested_overcurrent_clock_freq;
    uint32_t requested_temperature_clock_freq;
} hailo_health_info_t;

typedef struct {
    void* buffer;
    size_t size;
} hailo_stream_raw_buffer_t;

typedef struct {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_stream_raw_buffer_t raw_buffer;
} hailo_stream_raw_buffer_by_name_t;

typedef struct {
    float64_t avg_hw_latency_ms;
} hailo_latency_measurement_result_t;

typedef struct {
    char stream_name[HAILO_MAX_STREAM_NAME_SIZE];
    uint32_t rate;
} hailo_rate_limit_t;

typedef enum {
    HAILO_SENSOR_TYPES_GENERIC = 0,
    HAILO_SENSOR_TYPES_ONSEMI_AR0220AT,
    HAILO_SENSOR_TYPES_RASPICAM,
    HAILO_SENSOR_TYPES_ONSEMI_AS0149AT,
    HAILO_SENSOR_TYPES_HAILO8_ISP = 0x80000000,

    /** Max enum value to maintain ABI Integrity */
    HAILO_SENSOR_TYPES_MAX_ENUM = HAILO_MAX_ENUM
} hailo_sensor_types_t;

typedef enum {
    HAILO_FW_LOGGER_INTERFACE_PCIE     = 1 << 0,
    HAILO_FW_LOGGER_INTERFACE_UART     = 1 << 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FW_LOGGER_INTERFACE_MAX_ENUM = HAILO_MAX_ENUM
} hailo_fw_logger_interface_t;

typedef enum {
    HAILO_FW_LOGGER_LEVEL_TRACE = 0,
    HAILO_FW_LOGGER_LEVEL_DEBUG = 1,
    HAILO_FW_LOGGER_LEVEL_INFO  = 2,
    HAILO_FW_LOGGER_LEVEL_WARN  = 3,
    HAILO_FW_LOGGER_LEVEL_ERROR = 4,
    HAILO_FW_LOGGER_LEVEL_FATAL = 5,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FW_LOGGER_LEVEL_MAX_ENUM = HAILO_MAX_ENUM
} hailo_fw_logger_level_t;


typedef enum {
    HAILO_LOG_TYPE__RUNTIME = 0,
    HAILO_LOG_TYPE__SYSTEM_CONTROL = 1,
    HAILO_LOG_TYPE__NNC = 2,

    /** Max enum value to maintain ABI Integrity */
    HAILO_LOG_TYPE_MAX__ENUM = HAILO_MAX_ENUM
} hailo_log_type_t;

#define HAILO_DEFAULT_TRANSFORM_PARAMS                                       \
    {                                                                        \
        .transform_mode = HAILO_STREAM_TRANSFORM_COPY,                       \
        .user_buffer_format = {                                              \
            .type =  HAILO_FORMAT_TYPE_AUTO,                                 \
            .order = HAILO_FORMAT_ORDER_AUTO,                                \
            .flags = HAILO_FORMAT_FLAGS_QUANTIZED,                           \
        },                                                                   \
    }                                                                        \

#define HAILO_PCIE_STREAM_PARAMS_DEFAULT                                     \
    {                                                                        \
    }

#define HAILO_ACTIVATE_NETWORK_GROUP_PARAMS_DEFAULT                          \
    {                                                                        \
    }

/**
 * Retrieves hailort library version.
 * 
 * @param[out] version         Will be filled with hailort library version.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_library_version(hailo_version_t *version);

/**
 * Returns a string format of @ status.
 * 
 * @param[in] status         A ::hailo_status to be converted to string format.
 * @return Upon success, returns @ status as a string format. Otherwise, returns @a nullptr.
 */
HAILORTAPI const char* hailo_get_status_message(hailo_status status);

/** @} */ // end of group_defines

/** @defgroup group_device_functions Device functions
 *  @{
 */

/**
 * Returns information on all available devices in the system.
 *
 * @param[in] params                    Scan params, used for future compatibility, only NULL is allowed.
 * @param[out] device_ids               Array of ::hailo_device_id_t that were scanned.
 * @param[inout] device_ids_length      As input - the size of @a device_ids array. As output - the number of
 *                                      devices scanned.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_scan_devices(hailo_scan_devices_params_t *params, hailo_device_id_t *device_ids,
    size_t *device_ids_length);

/**
 * Creates a device by the given device id.
 *
 * @param[in] device_id      Device id, can represent several device types:
 *                              [-] for pcie devices - pcie bdf (XXXX:XX:XX.X)
 *                              [-] for ethernet devices - ip address (xxx.xxx.xxx.xxx)
 *                           If NULL is given, uses an arbitrary device found on the system.
 * @param[out] device        A pointer to a ::hailo_device that receives the allocated PCIe device.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note To release a device, call the ::hailo_release_device function with the returned ::hailo_device.
 */
HAILORTAPI hailo_status hailo_create_device_by_id(const hailo_device_id_t *device_id, hailo_device *device);

/**
 * Returns information on all available pcie devices in the system.
 *
 * @param[out] pcie_device_infos         A pointer to a buffer of ::hailo_pcie_device_info_t that receives the
 *                                       information.
 * @param[in]  pcie_device_infos_length  The number of ::hailo_pcie_device_info_t elements in the buffer pointed to by
 *                                       @a pcie_device_infos.
 * @param[out] number_of_devices         This variable will be filled with the number of devices. If the buffer is
 *                                       insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER error is
 *                                       returned.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_scan_pcie_devices(
   hailo_pcie_device_info_t *pcie_device_infos, size_t pcie_device_infos_length, size_t *number_of_devices);

/**
 * Parse PCIe device BDF string into hailo device info structure.
 *
 * @param[in] device_info_str   BDF device info, format \<domain\>.\<bus\>.\<device\>.\<func\>.
 * @param[out] device_info      A pointer to a ::hailo_pcie_device_info_t that receives the parsed device info.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note Call ::hailo_scan_pcie_devices to get all available hailo pcie devices.
 */
HAILORTAPI hailo_status hailo_parse_pcie_device_info(const char *device_info_str,
    hailo_pcie_device_info_t *device_info);

/**
 * Creates a PCIe device.
 *
 * @param[in] device_info    Information about the device to open. If NULL is given, uses an arbitrary device found on
 *                           the system.
 * @param[out] device        A pointer to a ::hailo_device that receives the allocated PCIe device.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note To release a device, call the ::hailo_release_device function with the returned ::hailo_device.
 */
HAILORTAPI hailo_status hailo_create_pcie_device(hailo_pcie_device_info_t *device_info, hailo_device *device);

/**
 * Releases an open device.
 *
 * @param[in] device   A ::hailo_device object to be released.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_device(hailo_device device);

/**
 * Returns the device type of the given device id string.
 *
 * @param[in] device_id       A :hailo_device_id_t device id to check.
 * @param[out] device_type    A :hailo_device_type_t returned device type.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_device_get_type_by_device_id(const hailo_device_id_t *device_id,
    hailo_device_type_t *device_type);

/**
 * Sends identify control to a Hailo device.
 *
 * @param[in] device              A ::hailo_device to be identified.
 * @param[out] device_identity    Information about the device.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_identify(hailo_device device, hailo_device_identity_t *device_identity);

/**
 * Receive information about the core cpu.
 *
 * @param[in] device              A ::hailo_device object.
 * @param[out] core_information   Information about the device.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_core_identify(hailo_device device, hailo_core_information_t *core_information);

/**
 * Get extended device information from a Hailo device.
 *
 * @param[in] device                            A ::hailo_device to get extended device info from.
 * @param[out] extended_device_information      Extended information about the device.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_extended_device_information(hailo_device device, hailo_extended_device_information_t *extended_device_information);

/**
 * Configure fw logger level and interface of sending.
 *
 * @param[in] device          A ::hailo_device object.
 * @param[in] level           The minimum logger level.
 * @param[in] interface_mask  Output interfaces (mix of ::hailo_fw_logger_interface_t).
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_fw_logger(hailo_device device, hailo_fw_logger_level_t level,
    uint32_t interface_mask);

/**
 * Change throttling state of temperature protection and overcurrent protection components.
 * In case that change throttling state of temperature protection didn't succeed,
 * the change throttling state of overcurrent protection is executed.
 *
 * @param[in] device            A ::hailo_device object.
 * @param[in] should_activate   Should be true to enable or false to disable.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_throttling_state(hailo_device device, bool should_activate);

/**
 * Get current throttling state of temperature protection and overcurrent protection components.
 * If any throttling is enabled, the function return true.
 *
 * @param[in] device            A ::hailo_device object.
 * @param[out] is_active        A pointer to the temperature protection or overcurrent protection components throttling state: true if active, false otherwise.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_throttling_state(hailo_device device, bool *is_active);

/**
 * Enable firmware watchdog.
 *
 * @param[in] device            A ::hailo_device object.
 * @param[in] cpu_id            A @a hailo_cpu_id_t indicating which CPU WD to enable.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note Advanced API. Please use with care.
 */
HAILORTAPI hailo_status hailo_wd_enable(hailo_device device, hailo_cpu_id_t cpu_id);

/**
 * Disable firmware watchdog.
 *
 * @param[in] device            A ::hailo_device object.
 * @param[in] cpu_id            A @a hailo_cpu_id_t indicating which CPU WD to disable.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note Advanced API. Please use with care.
 */
HAILORTAPI hailo_status hailo_wd_disable(hailo_device device, hailo_cpu_id_t cpu_id);

/**
 * Configure firmware watchdog.
 *
 * @param[in] device            A ::hailo_device object.
 * @param[in] cpu_id            A @a hailo_cpu_id_t indicating which CPU WD to configure.
 * @param[in] wd_cycles         Number of cycles until watchdog is triggered.
 * @param[in] wd_mode           A @a hailo_watchdog_mode_t indicating which WD mode to configure.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note Advanced API. Please use with care.
 */
HAILORTAPI hailo_status hailo_wd_config(hailo_device device, hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode);

/**
 * Read the FW previous system state.
 *
 * @param[in] device                  A ::hailo_device object.
 * @param[in] cpu_id                  A @a hailo_cpu_id_t indicating which CPU to state to read.
 * @param[out] previous_system_state  A @a uint32_t to be filled with the previous system state.
 *                                    0 indicating external reset, 1 indicating WD HW reset,
 *                                    2 indicating WD SW reset, 3 indicating SW control reset.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note Advanced API. Please use with care.
 */
HAILORTAPI hailo_status hailo_get_previous_system_state(hailo_device device, hailo_cpu_id_t cpu_id, uint32_t *previous_system_state);

/**
 * Enable/Disable Pause frames.
 *
 * @param[in] device                  A ::hailo_device object.
 * @param[in] rx_pause_frames_enable  Indicating whether to enable or disable pause frames.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_pause_frames(hailo_device device, bool rx_pause_frames_enable);

/**
 * Get device id which is the identification string of the device (i.e bdf for pcie devices)
 *
 * @param[in]  device           A ::hailo_device object.
 * @param[out] id               The returned device id.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_device_id(hailo_device device, hailo_device_id_t *id);

/**
 * Get temperature information on the device
 *
 * @param[in] device          A ::hailo_device object.
 * @param[out] temp_info      A @a hailo_chip_temperature_info_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Temperature in Celsius of the two internal temperature sensors (TS).
 */
HAILORTAPI hailo_status hailo_get_chip_temperature(hailo_device device, hailo_chip_temperature_info_t *temp_info);

/**
 * Reset device
 * 
 * @param[in] device    A ::hailo_device object.
 * @param[in] mode      The mode of the reset.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_reset_device(hailo_device device, hailo_reset_device_mode_t mode);

/**
 * Updates firmware to device flash.
 * 
 * @param[in]  device                 A ::hailo_device object.
 * @param[in]  firmware_buffer        A pointer to a buffer that contains the firmware to be updated on the @a device.
 * @param[in]  firmware_buffer_size   The size in bytes of the buffer pointed by @a firmware_buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Check ::hailo_extended_device_information_t.boot_source returned from ::hailo_get_extended_device_information
 *       to verify if the fw is booted from flash.
 */
HAILORTAPI hailo_status hailo_update_firmware(hailo_device device, void *firmware_buffer, uint32_t firmware_buffer_size);

/**
 * Updates second stage to device flash.
 * 
 * @param[in]  device                 A ::hailo_device object.
 * @param[in]  second_stage_buffer        A pointer to a buffer that contains the second_stage to be updated on the @a device.
 * @param[in]  second_stage_buffer_size   The size in bytes of the buffer pointed by @a second_stage_buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Check ::hailo_extended_device_information_t.boot_source returned from ::hailo_get_extended_device_information
 *       to verify if the fw is booted from flash.
 */
HAILORTAPI hailo_status hailo_update_second_stage(hailo_device device, void *second_stage_buffer, uint32_t second_stage_buffer_size);

/**
 * Sets a callback to be called when a notification with ID @a notification_id will be received
 *
 * @param[in] device                A ::hailo_device to register the callback to.
 * @param[in] callback              The callback function to be called.
 * @param[in] notification_id       The ID of the notification.
 * @param[in] opaque                User specific data.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_notification_callback(hailo_device device,
    hailo_notification_callback callback,
    hailo_notification_id_t notification_id, void *opaque);

/**
 * Removes a previously set callback with ID @a notification_id
 *
 * @param[in] device                A ::hailo_device to register the callback to.
 * @param[in] notification_id       The ID of the notification to remove.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_remove_notification_callback(hailo_device device,
    hailo_notification_id_t notification_id);

/**
 * Reset the sensor that is related to the section index config.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] section_index         Flash section index to load config from. [0-6]
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_reset_sensor(hailo_device device, uint8_t section_index);

/**
 * Set the I2C bus to which the sensor of the specified type is connected.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] sensor_type           The sensor type.
 * @param[in] bus_index             The I2C bus index of the sensor.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_sensor_i2c_bus_index(hailo_device device, hailo_sensor_types_t sensor_type, uint8_t bus_index);

/**
 * Load the configuration with I2C in the section index.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] section_index         Flash section index to load config from. [0-6]
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_load_and_start_sensor(hailo_device device, uint8_t section_index);

/**
 *  Read data from an I2C slave over a hailo device.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] slave_config          The ::hailo_i2c_slave_config_t configuration of the slave.
 * @param[in] register_address      The address of the register from which the data will be read.
 * @param[in] data                  Pointer to a buffer that would store the read data.
 * @param[in] length                The number of bytes to read into the buffer pointed by @a data.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_i2c_read(hailo_device device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address, uint8_t *data, uint32_t length);

/**
 *  Write data to an I2C slave over a hailo device.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] slave_config          The ::hailo_i2c_slave_config_t configuration of the slave.
 * @param[in] register_address      The address of the register to which the data will be written.
 * @param[in] data                  A pointer to a buffer that contains the data to be written to the slave.
 * @param[in] length                The size of @a data in bytes.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_i2c_write(hailo_device device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address, const uint8_t *data, uint32_t length);

/**
 * Dump config of given section index into a csv file.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] section_index         Flash section index to load config from. [0-7]
 * @param[in] config_file_path      File path to dump section configuration into.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_dump_sensor_config(hailo_device device, uint8_t section_index, const char *config_file_path);

/**
 * Store sensor configuration to Hailo chip flash memory.
 *
 * @param[in] device                A ::hailo_device object.
 * @param[in] section_index         Flash section index to write to. [0-6]
 * @param[in] sensor_type           Sensor type.
 * @param[in] reset_config_size     Size of reset configuration.
 * @param[in] config_height         Configuration resolution height.
 * @param[in] config_width          Configuration resolution width.
 * @param[in] config_fps            Configuration FPS.
 * @param[in] config_file_path      Sensor configuration file path.
 * @param[in] config_name           Sensor configuration name.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_store_sensor_config(hailo_device device, uint32_t section_index,
    hailo_sensor_types_t sensor_type, uint32_t reset_config_size, uint16_t config_height, uint16_t config_width,
    uint16_t config_fps, const char *config_file_path, const char *config_name);

/**
 * Store sensor ISP configuration to Hailo chip flash memory.
 *
 * @param[in] device                            A ::hailo_device object.
 * @param[in] reset_config_size                 Size of reset configuration.
 * @param[in] config_height                     Configuration resolution height.
 * @param[in] config_width                      Configuration resolution width.
 * @param[in] config_fps                        Configuration FPS.
 * @param[in] isp_static_config_file_path       ISP static configuration file path.
 * @param[in] isp_runtime_config_file_path      ISP runtime configuration file path.
 * @param[in] config_name                       Sensor configuration name.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_store_isp_config(hailo_device device, uint32_t reset_config_size, uint16_t config_height, uint16_t config_width,
    uint16_t config_fps, const char *isp_static_config_file_path, const char *isp_runtime_config_file_path, const char *config_name);

/**
 *  Test chip memories using smart BIST mechanism.
 * 
 * @param[in]     device - A ::hailo_device object.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 */
HAILORTAPI hailo_status hailo_test_chip_memories(hailo_device device);

/** @} */ // end of group_device_functions

/** @defgroup group_vdevice_functions VDevice functions
 *  @{
 */

/**
 * Init vdevice params with default values.
 *
 * @param[out] params                   A @a hailo_vdevice_params_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_init_vdevice_params(hailo_vdevice_params_t *params);

/**
 * Creates a vdevice.
 * 
 * @param[in]  params        A @a hailo_vdevice_params_t (may be NULL). Can be initialzed to default values using ::hailo_init_vdevice_params.
 * @param[out] vdevice       A pointer to a ::hailo_vdevice that receives the allocated vdevice.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
 * @note To release a vdevice, call the ::hailo_release_vdevice function with the returned ::hailo_vdevice.
 */
HAILORTAPI hailo_status hailo_create_vdevice(hailo_vdevice_params_t *params, hailo_vdevice *vdevice);

/**
 * Configure the vdevice from an hef.
 *
 * @param[in]  vdevice                     A ::hailo_vdevice object to be configured.
 * @param[in]  hef                         A ::hailo_hef object to configure the @a vdevice by.
 * @param[in]  params                      A @a hailo_configure_params_t (may be NULL). Can be initialzed to default values using ::hailo_init_configure_params_by_vdevice.
 * @param[out] network_groups              Array of network_groups that were loaded from the HEF file.
 * @param[inout] number_of_network_groups  As input - the size of network_groups array. As output - the number of network_groups loaded.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_configure_vdevice(hailo_vdevice vdevice, hailo_hef hef,
    hailo_configure_params_t *params, hailo_configured_network_group *network_groups, size_t *number_of_network_groups);

/**
 * Gets the underlying physical devices from a vdevice.
 *
 * @param[in]  vdevice                     A @a hailo_vdevice object to fetch physical devices from.
 * @param[out] devices                     Array of ::hailo_device to be fetched from vdevice.
 * @param[inout] number_of_devices         As input - the size of @a devices array. As output - the number of physical devices under vdevice.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note The returned physical devices are held in the scope of @a vdevice.
 */
HAILORTAPI hailo_status hailo_get_physical_devices(hailo_vdevice vdevice, hailo_device *devices,
    size_t *number_of_devices);

/**
 * Gets the physical devices' ids from a vdevice.
 *
 * @param[in]  vdevice                     A @a hailo_vdevice object to fetch physical devices from.
 * @param[out] devices_ids                 Array of ::hailo_device_id_t to be fetched from vdevice.
 * @param[inout] number_of_devices         As input - the size of @a devices_ids array. As output - the number of
 *                                         physical devices under vdevice.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note The returned physical devices are held in the scope of @a vdevice.
 */
HAILORTAPI hailo_status hailo_vdevice_get_physical_devices_ids(hailo_vdevice vdevice, hailo_device_id_t *devices_ids,
    size_t *number_of_devices);

/**
 * Release an open vdevice.
 * 
 * @param[in] vdevice   A :: hailo_vdevice object to be released.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_vdevice(hailo_vdevice vdevice);

/** @} */ // end of group_vdevice_functions

/** @defgroup group_power_measurement_functions Power measurement functions
 *  @{
 */

/**
 * Perform a single power measurement.
 * 
 * @param[in]   device             A ::hailo_device object.
 * @param[in]   dvm                Which DVM will be measured. Default (::HAILO_DVM_OPTIONS_AUTO) will be different according to the board: <br>
 *                                 - Default (::HAILO_DVM_OPTIONS_AUTO) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
 *                                 It sums ::HAILO_DVM_OPTIONS_VDD_CORE, ::HAILO_DVM_OPTIONS_MIPI_AVDD and ::HAILO_DVM_OPTIONS_AVDD_H.
 *                                 Only ::HAILO_POWER_MEASUREMENT_TYPES__POWER can measured with this option.
 *                                 - Default (::HAILO_DVM_OPTIONS_AUTO) for platforms supporting current monitoring (such as M.2 and mPCIe): OVERCURRENT_PROTECTION.
 * @param[in]   measurement_type   The type of the measurement. Choosing ::HAILO_POWER_MEASUREMENT_TYPES__AUTO
 *                                 will select the default value according to the supported features.
 * @param[out]  measurement        The measured value. Measured units are determined due to
 *                                 ::hailo_power_measurement_types_t.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_power_measurement(hailo_device device, hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type, float32_t *measurement);

/**
 * Start performing a long power measurement.
 * 
 * @param[in]   device               A ::hailo_device object.
 * @param[in]   averaging_factor     Number of samples per time period, sensor configuration value.
 * @param[in]   sampling_period      Related conversion time, sensor configuration value.
 *                                   The sensor samples the power every sampling_period {us} and averages every
 *                                   averaging_factor samples. The sensor provides a new value every: (2 * sampling_period * averaging_factor) {ms}.
 *                                   The firmware wakes up every interval_milliseconds {ms} and checks the sensor.
 *                                   If there is a new value to read from the sensor, the firmware reads it.
 *                                   Note that the average calculated by the firmware is 'average of averages',
 *                                   because it averages values that have already been averaged by the sensor.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_start_power_measurement(hailo_device device,
    hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period);

/**
 * Set parameters for long power measurement.
 * 
 * @param[in]   device             A ::hailo_device object.
 * @param[in]   buffer_index       A ::hailo_measurement_buffer_index_t represents the buffer on the firmware the data would be saved at.
 *                                 Should match the one passed to ::hailo_get_power_measurement.
 * @param[in]   dvm                Which DVM will be measured. Default (::HAILO_DVM_OPTIONS_AUTO) will be different according to the board: <br>
 *                                 - Default (::HAILO_DVM_OPTIONS_AUTO) for EVB is an approximation to the total power consumption of the chip in PCIe setups.
 *                                 It sums ::HAILO_DVM_OPTIONS_VDD_CORE, ::HAILO_DVM_OPTIONS_MIPI_AVDD and ::HAILO_DVM_OPTIONS_AVDD_H.
 *                                 Only ::HAILO_POWER_MEASUREMENT_TYPES__POWER can measured with this option.
 *                                 - Default (::HAILO_DVM_OPTIONS_AUTO) for platforms supporting current monitoring (such as M.2 and mPCIe): OVERCURRENT_PROTECTION.
 * @param[in]   measurement_type   The type of the measurement. Choosing ::HAILO_POWER_MEASUREMENT_TYPES__AUTO
 *                                 will select the default value according to the supported features.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_power_measurement(hailo_device device, hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type);

/**
 * Read measured power from a long power measurement
 * 
 * @param[in]   device                A ::hailo_device object.
 * @param[in]   buffer_index          A ::hailo_measurement_buffer_index_t represents the buffer on the firmware the data would be saved at.
 *                                    Should match the one passed to ::hailo_set_power_measurement.
 * @param[in]   should_clear          Flag indicating if the results saved at the firmware will be deleted after reading.
 * @param[out]  measurement_data      The measurement data, ::hailo_power_measurement_data_t. Measured units are
 *                                    determined due to ::hailo_power_measurement_types_t passed to ::hailo_set_power_measurement
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_power_measurement(hailo_device device, hailo_measurement_buffer_index_t buffer_index, bool should_clear,
     hailo_power_measurement_data_t *measurement_data);

/**
 * Stop performing a long power measurement.
 * 
 * @param[in]   device             A ::hailo_device object.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_stop_power_measurement(hailo_device device);

/** @} */ // end of group_power_measurement_functions

/** @defgroup group_hef_functions HEF parsing functions
 *  @{
 */

/**
 * Creates an HEF from file.
 *
 * @param[out] hef             A pointer to a @a hailo_hef that receives the allocated HEF.
 * @param[in]  file_name       The name of the hef file.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note To release an HEF, call the ::hailo_release_hef function with the returned @a hef.
 */
HAILORTAPI hailo_status hailo_create_hef_file(hailo_hef *hef, const char *file_name);

/**
 * Creates an HEF from buffer.
 *
 * @param[out] hef             A pointer to a @a hailo_hef that receives the allocated HEF.
 * @param[in]  buffer          A pointer to a buffer that contains the HEF content.
 * @param[in]  size            The size in bytes of the HEF content pointed by @a buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note To release an HEF, call the ::hailo_release_hef function with the returned @a hef.
 */
HAILORTAPI hailo_status hailo_create_hef_buffer(hailo_hef *hef, const void *buffer, size_t size);

/**
 * Release an open HEF.
 *
 * @param[in] hef   A ::hailo_hef object to be released.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_hef(hailo_hef hef);

/**
 * Gets all stream infos.
 *
 * @param[in] hef                     A ::hailo_hef object that contains the information.
 * @param[in] name                    The name of the network or network_group which contains the stream_infos.
 *                                    In case network group name is given, the function returns all stream infos 
 *                                    of all the networks of the given network group.
 *                                    In case network name is given (provided by @a hailo_hef_get_network_infos), 
 *                                    the function returns all stream infos of the given network.
 *                                    If NULL is passed, the function returns all the stream infos of 
 *                                    all the networks of the first network group.
 * @param[out] stream_infos           A pointer to a buffer of ::hailo_stream_info_t that receives the informations.
 * @param[in] stream_infos_length     The number of ::hailo_stream_info_t elements in the buffer pointed by
 *                                    @a stream_infos
 * @param[out] number_of_streams      This variable will be filled with the number of stream_infos if the function returns with
 *                                    HAILO_SUCCESS or HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_all_stream_infos(hailo_hef hef, const char *name,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams);

/**
 * Gets stream info by name.
 *
 * @param[in]  hef                 A ::hailo_hef object that contains the information.
 * @param[in]  network_group_name  The name of the network_group which contains the stream_infos. If NULL is passed,
 *                                 the first network_group in the HEF will be addressed.
 * @param[in]  stream_name         The name of the stream as presented in the hef.
 * @param[in]  stream_direction    Indicates the stream direction.
 * @param[out] stream_info         A pointer to a ::hailo_stream_info_t that receives the stream information.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_stream_info_by_name(hailo_hef hef, const char *network_group_name,
    const char *stream_name, hailo_stream_direction_t stream_direction, hailo_stream_info_t *stream_info);

/**
 * Gets all virtual stream infos.
 *
 * @param[in] hef                        A ::hailo_hef object that contains the information.
 * @param[in] name                       The name of the network or network_group which contains the virtual stream infos.
 *                                       In case network group name is given, the function returns all virtual stream infos 
 *                                       of all the networks of the given network group.
 *                                       In case network name is given (provided by @a hailo_hef_get_network_infos), 
 *                                       the function returns all stream infos of the given network.
 *                                       If NULL is passed, the function returns all the stream infos of 
 *                                       all the networks of the first network group.
 * @param[out] vstream_infos             A pointer to a buffer of ::hailo_stream_info_t that receives the informations.
 * @param[inout] vstream_infos_count     As input - the maximum amount of entries in @a vstream_infos array.
 *                                       As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                       or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_all_vstream_infos(hailo_hef hef, const char *name,
    hailo_vstream_info_t *vstream_infos, size_t *vstream_infos_count);

/**
 * Gets vstream name from original layer name.
 *
 * @param[in]  hef                 A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name  The name of the network_group which contains the stream_infos. If NULL is passed,
 *                                 the first network_group in the HEF will be addressed.
 * @param[in]  original_name       The original layer name as presented in the hef.
 * @param[out] vstream_name        The name of the vstream for the provided original name, ends with NULL terminator.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_vstream_name_from_original_name(hailo_hef hef, const char *network_group_name,
    const char *original_name, hailo_layer_name_t *vstream_name);

/**
 * Gets all original layer names from vstream name.
 *
 * @param[in]  hef                      A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name       The name of the network_group which contains the stream_infos. If NULL is passed,
 *                                      the first network_group in the HEF will be addressed.
 * @param[in]  vstream_name             The name of the stream as presented in the hef.
 * @param[out] original_names           Array of ::hailo_layer_name_t, all original names linked to the provided stream
 *                                      (each name ends with NULL terminator).
 * @param[inout] original_names_length  As input - the size of original_names array. As output - the number of original_layers names.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_original_names_from_vstream_name(hailo_hef hef, const char *network_group_name,
    const char *vstream_name, hailo_layer_name_t *original_names, size_t *original_names_length);

/**
 * Gets all vstream names from stream name.
 *
 * @param[in]  hef                      A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name       The name of the network_group which contains the stream_infos. If NULL is passed,
 *                                      the first network_group in the HEF will be addressed.
 * @param[in]  stream_name              The name of the stream as presented in the hef.
 * @param[out] vstream_names            Array of ::hailo_layer_name_t, all vstream names linked to the provided stream
 *                                      (each name ends with NULL terminator).
 * @param[inout] vstream_names_length   As input - the size of vstream_names array. As output - the number of vstream names.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_vstream_names_from_stream_name(hailo_hef hef, const char *network_group_name,
    const char *stream_name, hailo_layer_name_t *vstream_names, size_t *vstream_names_length);

/**
 * Gets all stream names from vstream name.
 *
 * @param[in]  hef                      A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name       The name of the network_group which contains the stream_infos. If NULL is passed,
 *                                      the first network_group in the HEF will be addressed.
 * @param[in]  vstream_name             The name of the vstream as presented in the hef.
 * @param[out] stream_names             Array of ::hailo_layer_name_t, all stream names linked to the provided vstream
 *                                      (each name ends with NULL terminator).
 * @param[inout] stream_names_length    As input - the size of stream_names array. As output - the number of stream names.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_stream_names_from_vstream_name(hailo_hef hef, const char *network_group_name,
    const char *vstream_name, hailo_layer_name_t *stream_names, size_t *stream_names_length);

 /**
 * Gets sorted output names from network group name.
 *
 * @param[in]  hef                          A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name           The name of the network_group. If NULL is passed, the first network_group
 *                                          in the HEF will be addressed.
 * @param[out] sorted_output_names          List of sorted outputs names.
 * @param[inout] sorted_output_names_count  As input - the expected size of sorted_output_names list. As output - the number of sorted_output_names.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_sorted_output_names(hailo_hef hef, const char *network_group_name,
    hailo_layer_name_t *sorted_output_names, size_t *sorted_output_names_count);

 /**
 * Gets bottleneck fps from network group name.
 *
 * @param[in]  hef                  A @a hailo_hef object that contains the information.
 * @param[in]  network_group_name   The name of the network_group. If NULL is passed, the first network_group
 *                                  in the HEF will be addressed.
 * @param[out] bottleneck_fps       Bottleneck FPS. Note: This is not relevant in the case of multi context. 
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_bottleneck_fps(hailo_hef hef, const char *network_group_name,
    float64_t *bottleneck_fps);

/** @} */ // end of group_hef_functions

/** @defgroup group_network_group_functions Network group configuration/activation functions
 *  @{
 */

/**
 * Init configure params with default values for a given hef.
 *
 * @param[in]  hef                      A  ::hailo_hef object to configure the @a device by.
 * @param[in]  stream_interface         A @a hailo_stream_interface_t indicating which @a hailo_stream_parameters_t to create.
 * @param[out] params                   A @a hailo_configure_params_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_init_configure_params(hailo_hef hef, hailo_stream_interface_t stream_interface,
    hailo_configure_params_t *params);

/**
 * Init configure params with default values for a given hef by virtual device.
 *
 * @param[in]  hef                      A  ::hailo_hef object to configure the @a device by.
 * @param[in]  vdevice                  A @a hailo_vdevice for which we init the params for.
 * @param[out] params                   A @a hailo_configure_params_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_init_configure_params_by_vdevice(hailo_hef hef, hailo_vdevice vdevice,
    hailo_configure_params_t *params);

/**
 * Init configure params with default values for a given hef by device.
 *
 * @param[in]  hef                      A  ::hailo_hef object to configure the @a device by.
 * @param[in]  device                   A @a hailo_device for which we init the params for.
 * @param[out] params                   A @a hailo_configure_params_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_init_configure_params_by_device(hailo_hef hef, hailo_device device,
    hailo_configure_params_t *params);

/**
 * Init configure params with default values for a given hef.
 *
 * @param[in]  hef                      A  ::hailo_hef object to configure the @a device by.
 * @param[in]  stream_interface         A @a hailo_stream_interface_t indicating which @a hailo_stream_parameters_t to create.
 * @param[in]  network_group_name       The name of the network_group to make configure params for. If NULL is passed,
 *                                      the first network_group in the HEF will be addressed.
 * @param[out] params                   A @a hailo_configure_params_t to be filled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_init_configure_network_group_params(hailo_hef hef, hailo_stream_interface_t stream_interface,
    const char *network_group_name, hailo_configure_network_group_params_t *params);

/**
 * Configure the device from an hef.
 *
 * @param[in]  device                      A ::hailo_device object to be configured.
 * @param[in]  hef                         A ::hailo_hef object to configure the @a device by.
 * @param[in]  params                      A @a hailo_configure_params_t (may be NULL). Can be initialzed to default values using ::hailo_init_configure_params_by_device.
 * @param[out] network_groups              Array of network_groups that were loaded from the HEF file.
 * @param[inout] number_of_network_groups  As input - the size of network_groups array. As output - the number of network_groups loaded.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_configure_device(hailo_device device, hailo_hef hef,
    hailo_configure_params_t *params, hailo_configured_network_group *network_groups, size_t *number_of_network_groups);

/**
 * Block until network_group is activated, or until timeout_ms is passed.
 *
 * @param[in] network_group              A ::hailo_configured_network_group to wait for activation.
 * @param[in] timeout_ms                 The timeout in milliseconds. If @a timeout_ms is zero, the function returns immediately.
 *                                       If @a timeout_ms is @a HAILO_INFINITE, the function returns only when the event is
 *                                       signaled.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_wait_for_network_group_activation(hailo_configured_network_group network_group,
    uint32_t timeout_ms);


/**
 * Get network group infos from an hef.
 *
 * @param[in]    hef                       A ::hailo_hef object.
 * @param[out]   infos                     Array of @a hailo_network_group_info_t to be filled.
 * @param[inout] number_of_infos           As input - the size of infos array. As output - the number of infos loaded.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_network_groups_infos(hailo_hef hef, hailo_network_group_info_t *infos,
    size_t *number_of_infos);

/**
 * Gets all stream infos from a configured network group
 *
 * @param[in]  network_group          A ::hailo_configured_network_group object.
 * @param[out] stream_infos           A pointer to a buffer of ::hailo_stream_info_t that receives the informations.
 * @param[in]  stream_infos_length    The number of ::hailo_stream_info_t elements in the buffer pointed by
 *                                    @a stream_infos
 * @param[out] number_of_streams      This variable will be filled with the number of stream_infos if the function returns with
 *                                    HAILO_SUCCESS or HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_network_group_get_all_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams);

/**
 * Gets all input stream infos from a configured network group
 *
 * @param[in]  network_group          A ::hailo_configured_network_group object.
 * @param[out] stream_infos           A pointer to a buffer of ::hailo_stream_info_t that receives the informations.
 * @param[in]  stream_infos_length    The number of ::hailo_stream_info_t elements in the buffer pointed by
 *                                    @a stream_infos
 * @param[out] number_of_streams      This variable will be filled with the number of stream_infos if the function returns with
 *                                    HAILO_SUCCESS or HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_network_group_get_input_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams);

/**
 * Gets all output stream infos from a configured network group
 *
 * @param[in]  network_group          A ::hailo_configured_network_group object.
 * @param[out] stream_infos           A pointer to a buffer of ::hailo_stream_info_t that receives the informations.
 * @param[in]  stream_infos_length    The number of ::hailo_stream_info_t elements in the buffer pointed by
 *                                    @a stream_infos
 * @param[out] number_of_streams      This variable will be filled with the number of stream_infos if the function returns with
 *                                    HAILO_SUCCESS or HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_network_group_get_output_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams);

/**
 * Shutdown a given network group. Makes sure all ongoing async operations are canceled. All async callbacks
 * of transfers that have not been completed will be called with status ::HAILO_STREAM_ABORT.
 * Any resources attached to the network group may be released after function returns.
 *
 * @param[in]  network_group                NetworkGroup to be shutdown.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note Calling this function is optional, and it is used to shutdown network group while there is still ongoing
 *       inference.
 */
HAILORTAPI hailo_status hailo_shutdown_network_group(hailo_configured_network_group network_group);

/**
 * Activates hailo_device inner-resources for inference.
 *
 * @param[in]  network_group                NetworkGroup to be activated.
 * @param[in]  activation_params            Optional parameters for the activation (may be NULL).
 * @param[out] activated_network_group_out  Handle for the activated network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_activate_network_group(hailo_configured_network_group network_group,
    hailo_activate_network_group_params_t *activation_params,
    hailo_activated_network_group *activated_network_group_out);

/**
 * De-activates hailo_device inner-resources for inference.
 *
 * @param[in]  activated_network_group        NetworkGroup to deactivate.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_deactivate_network_group(hailo_activated_network_group activated_network_group);

/**
 * Return input stream from configured network_group by stream name.
 *
 * @param[in]  configured_network_group      NetworkGroup to get stream from.
 * @param[in]  stream_name                  The name of the input stream to retrieve.
 * @param[out] stream                       The returned input stream.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_input_stream(
    hailo_configured_network_group configured_network_group, const char *stream_name, hailo_input_stream *stream);

/**
 * Return output stream from configured network_group by stream name.
 *
 * @param[in]  configured_network_group      NetworkGroup to get stream from.
 * @param[in]  stream_name                  The name of the output stream to retrieve.
 * @param[out] stream                       The returned output stream.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_output_stream(
    hailo_configured_network_group configured_network_group, const char *stream_name, hailo_output_stream *stream);

/**
 * Returns the network latency (only available if latency measurement was enabled).
 *
 * @param[in]  configured_network_group     NetworkGroup to get the latency measurement from.
 * @param[in]  network_name                 Network name of the requested latency measurement.
 *                                          If NULL is passed, all the networks in the network group will be addressed,
 *                                          and the resulted measurement is avarage latency of all networks.
 * @param[out] result                       Output latency result.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_latency_measurement(hailo_configured_network_group configured_network_group,
    const char *network_name, hailo_latency_measurement_result_t *result);

/**
 * Sets the maximum time period that may pass before receiving run time from the scheduler.
 * This will occur providing at least one send request has been sent, there is no minimum requirement for send
 *  requests, (e.g. threshold - see set_scheduler_threshold()).
 *
 * @param[in]  configured_network_group     NetworkGroup for which to set the scheduler timeout.
 * @param[in]  timeout_ms                   Timeout in milliseconds.
 * @param[in]  network_name                 Network name for which to set the timeout.
 *                                          If NULL is passed, the timeout will be set for all the networks in the network group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note The time period is measured from the time the first frame enters the queue. It resets when a switch occurs and there are still frames in the queue.

 * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
 * @note The default timeout is 0ms.
 * @note Currently, setting the timeout for a specific network is not supported.
 * @note The timeout may be ignored to prevent idle time from the device.
 */
HAILORTAPI hailo_status hailo_set_scheduler_timeout(hailo_configured_network_group configured_network_group,
    uint32_t timeout_ms, const char *network_name);

/**
 * Sets the scheduler threshold.
 * This threshold sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.
 * If at least one send request has been sent, but the threshold is not reached within a set time period (e.g. timeout - see hailo_set_scheduler_timeout()),
 *  the scheduler will consider the network ready regardless.
 *
 * @param[in]  configured_network_group     NetworkGroup for which to set the scheduler threshold.
 * @param[in]  threshold                    Threshold in number of frames.
 * @param[in]  network_name                 Network name for which to set the threshold.
 *                                          If NULL is passed, the threshold will be set for all the networks in the network group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
 * @note The default threshold is 1, which means HailoRT will apply an automatic heuristic to choose the threshold.
 * @note Currently, setting the threshold for a specific network is not supported.
 * @note The threshold may be ignored to prevent idle time from the device.
 */
HAILORTAPI hailo_status hailo_set_scheduler_threshold(hailo_configured_network_group configured_network_group,
    uint32_t threshold, const char *network_name);

/**
 * Sets the priority of the network.
 * When the network group scheduler will choose the next network, networks with higher priority will be prioritized in the selection.
 * Larger number represents higher priority
 *
 * @param[in]  configured_network_group     NetworkGroup for which to set the scheduler priority.
 * @param[in]  priority                     Priority as a number between HAILO_SCHEDULER_PRIORITY_MIN - HAILO_SCHEDULER_PRIORITY_MAX.
 * @param[in]  network_name                 Network name for which to set the priority.
 *                                          If NULL is passed, the priority will be set for all the networks in the network group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
 * @note The default priority is HAILO_SCHEDULER_PRIORITY_NORMAL.
 * @note Currently, setting the priority for a specific network is not supported.
 */
HAILORTAPI hailo_status hailo_set_scheduler_priority(hailo_configured_network_group configured_network_group,
    uint8_t priority, const char *network_name);

/** @} */ // end of group_network_group_functions

/** @defgroup group_buffer_functions Buffer functions
 *  @{
 */
// ************************************* NOTE - START ************************************* //
// Dma buffer allocation isn't currently supported and is for internal use only             //
// **************************************************************************************** //
// Free returned buffer via hailo_free_buffer
HAILORTAPI hailo_status hailo_allocate_buffer(size_t size, const hailo_buffer_parameters_t *allocation_params, void **buffer_out);
HAILORTAPI hailo_status hailo_free_buffer(void *buffer);
// ************************************** NOTE - END ************************************** //
// Dma buffer allocation isn't currently supported and is for internal use only             //
// **************************************************************************************** //

/**
 * Maps the buffer pointed to by @a address for DMA transfers to/from the given @a device, in the specified
 * @a data_direction.
 * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
 * apparent when the buffer is reused multiple times across different async operations.
 * For low level API (aka ::hailo_input_stream or ::hailo_output_stream), buffers passed to
 * ::hailo_stream_write_raw_buffer_async and ::hailo_stream_read_raw_buffer_async can be mapped.
 *
 * @param[in] device        A ::hailo_device object.
 * @param[in] address       The address of the buffer to be mapped
 * @param[in] size          The buffer's size in bytes
 * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
 *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note The DMA mapping will be released upon calling ::hailo_device_dma_unmap_buffer with @a address, @a size and
 *       @a data_direction, or when the @a device object is destroyed.
 * @note The buffer pointed to by @a address cannot be released until it is unmapped (via
 *       ::hailo_device_dma_unmap_buffer or ::hailo_release_device).
 */
HAILORTAPI hailo_status hailo_device_dma_map_buffer(hailo_device device, void *address, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Un-maps a buffer buffer pointed to by @a address for DMA transfers to/from the given @a device, in the direction
 * @a direction.
 *
 * @param[in] device        A ::hailo_device object.
 * @param[in] address       The address of the buffer to be un-mapped.
 * @param[in] size          The buffer's size in bytes.
 * @param[in] direction     The direction of the mapping.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_device_dma_unmap_buffer(hailo_device device, void *address, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Maps the buffer pointed to by @a address for DMA transfers to/from the given @a vdevice, in the specified
 * @a data_direction.
 * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
 * apparent when the buffer is reused multiple times across different async operations.
 * For low level API (aka ::hailo_input_stream or ::hailo_output_stream), buffers passed to
 * ::hailo_stream_write_raw_buffer_async and ::hailo_stream_read_raw_buffer_async can be mapped.
 *
 * @param[in] vdevice       A ::hailo_vdevice object.
 * @param[in] address       The address of the buffer to be mapped
 * @param[in] size          The buffer's size in bytes
 * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
 *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note The DMA mapping will be released upon calling ::hailo_vdevice_dma_unmap_buffer with @a address, @a size and
 *       @a data_direction, or when the @a vdevice object is destroyed.
 * @note The buffer pointed to by @a address cannot be released until it is unmapped (via
 *       ::hailo_vdevice_dma_unmap_buffer or ::hailo_release_vdevice).
 */
HAILORTAPI hailo_status hailo_vdevice_dma_map_buffer(hailo_vdevice vdevice, void *address, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Un-maps a buffer buffer pointed to by @a address for DMA transfers to/from the given @a vdevice, in the direction
 * @a direction.
 *
 * @param[in] vdevice       A ::hailo_vdevice object.
 * @param[in] address       The address of the buffer to be un-mapped.
 * @param[in] size          The buffer's size in bytes.
 * @param[in] direction     The direction of the mapping.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_vdevice_dma_unmap_buffer(hailo_vdevice vdevice, void *address, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Maps the dmabuf represented by file descriptor @a dmabuf_fd for DMA transfers to/from the given @a device, in the specified
 * @a data_direction.
 * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
 * apparent when the buffer is reused multiple times across different async operations.
 * For low level API (aka ::hailo_input_stream or ::hailo_output_stream), buffers passed to
 * ::hailo_stream_write_raw_buffer_async and ::hailo_stream_read_raw_buffer_async can be mapped.
 *
 * @param[in] device        A ::hailo_device object.
 * @param[in] dmabuf_fd     The file decsriptor of the dmabuf to be mapped
 * @param[in] size          The buffer's size in bytes
 * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
 *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note The DMA mapping will be released upon calling ::hailo_device_dma_unmap_dmabuf with @a dmabuf_fd, @a size and
 *       @a data_direction, or when the @a device object is destroyed.
 * @note The dmabuf pointed to by @a dmabuf_fd cannot be released until it is unmapped (via
 *       ::hailo_device_dma_map_dmabuf or ::hailo_release_device).
 */
HAILORTAPI hailo_status hailo_device_dma_map_dmabuf(hailo_device device, int dmabuf_fd, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Un-maps a dmabuf represented by file descriptor @a dmabuf_fd for DMA transfers to/from the given @a device, in the direction
 * @a direction.
 *
 * @param[in] device        A ::hailo_device object.
 * @param[in] dmabuf_fd     The file descriptor of the dmabuf to be un-mapped.
 * @param[in] size          The buffer's size in bytes.
 * @param[in] direction     The direction of the mapping.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_device_dma_unmap_dmabuf(hailo_device device, int dmabuf_fd, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Maps the dmabuf represented by the file descriptor @a dmabuf_fd for DMA transfers to/from the given @a vdevice, in the specified
 * @a data_direction.
 * DMA mapping of buffers in advance may improve the performance of async API. This improvement will become
 * apparent when the buffer is reused multiple times across different async operations.
 * For low level API (aka ::hailo_input_stream or ::hailo_output_stream), buffers passed to
 * ::hailo_stream_write_raw_buffer_async and ::hailo_stream_read_raw_buffer_async can be mapped.
 *
 * @param[in] vdevice       A ::hailo_vdevice object.
 * @param[in] dmabuf_fd     The file descriptor of the dmabuf to be mapped
 * @param[in] size          The buffer's size in bytes
 * @param[in] direction     The direction of the mapping. For input streams, use `HAILO_DMA_BUFFER_DIRECTION_H2D`
 *                          and for output streams, use `HAILO_DMA_BUFFER_DIRECTION_D2H`.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 *
 * @note The DMA mapping will be released upon calling ::hailo_vdevice_dma_unmap_dmabuf with @a dmabuf_fd, @a size and
 *       @a data_direction, or when the @a vdevice object is destroyed.
 * @note The dmabuf pointed to by @a dmabuf_fd cannot be released until it is unmapped (via
 *       ::hailo_vdevice_dma_unmap_dmabuf or ::hailo_release_vdevice).
 */
HAILORTAPI hailo_status hailo_vdevice_dma_map_dmabuf(hailo_vdevice vdevice, int dmabuf_fd, size_t size,
    hailo_dma_buffer_direction_t direction);

/**
 * Un-maps a dmabuf pointed to by @a dmabuf_fd for DMA transfers to/from the given @a vdevice, in the direction
 * @a direction.
 *
 * @param[in] vdevice       A ::hailo_vdevice object.
 * @param[in] dmabuf_fd     The file descriptor of the dmabuf to be un-mapped.
 * @param[in] size          The buffer's size in bytes.
 * @param[in] direction     The direction of the mapping.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_vdevice_dma_unmap_dmabuf(hailo_vdevice vdevice, int dmabuf_fd, size_t size,
    hailo_dma_buffer_direction_t direction);

/** @} */ // end of group_buffer_functions

/** @defgroup group_stream_functions Stream functions
 *  @{
 */

/**
 * Set new timeout value to an input stream
 *
 * @param[in] stream          A ::hailo_input_stream object to get the new timeout value.
 * @param[in] timeout_ms      the new timeout value to be set.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_input_stream_timeout(hailo_input_stream stream, uint32_t timeout_ms);

/**
 * Set new timeout value to an output stream
 *
 * @param[in] stream          A ::hailo_output_stream object to get the new timeout value.
 * @param[in] timeout_ms      the new timeout value to be set.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_set_output_stream_timeout(hailo_output_stream stream, uint32_t timeout_ms);

/**
 * Gets the size of a stream's frame on the host side in bytes
 *
 * @param[in] stream   A ::hailo_input_stream object.
 * @return The size of the frame on the host side in bytes
 */
HAILORTAPI size_t hailo_get_input_stream_frame_size(hailo_input_stream stream);

/**
 * Gets the size of a stream's frame on the host side in bytes
 *
 * @param[in] stream   A ::hailo_output_stream object.
 * @return The size of the frame on the host side in bytes
 */
HAILORTAPI size_t hailo_get_output_stream_frame_size(hailo_output_stream stream);

/**
 * Gets stream info from the given input stream
 *
 * @param[in] stream        A ::hailo_input_stream object.
 * @param[out] stream_info  An output ::hailo_stream_info_t.
 * @return The size of the frame on the host side in bytes
 */
HAILORTAPI hailo_status hailo_get_input_stream_info(hailo_input_stream stream, hailo_stream_info_t *stream_info);

/**
 * Gets stream info from the given output stream
 *
 * @param[in] stream        A ::hailo_input_stream object.
 * @param[out] stream_info  An output ::hailo_stream_info_t.
 * @return The size of the frame on the host side in bytes
 */
HAILORTAPI hailo_status hailo_get_output_stream_info(hailo_output_stream stream, hailo_stream_info_t *stream_info);

/**
 * Gets quant infos for a given input stream.
 * 
 * @param[in]     stream        A ::hailo_input_stream object.
 * @param[out]    quant_infos   A pointer to a buffer of @a hailo_quant_info_t that will be filled with quant infos.
 * @param[inout]  quant_infos_count   As input - the maximum amount of entries in @a quant_infos array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case a single qp is present - the returned list will be of size 1.
 *       Otherwise - the returned list will be of the same length as the number of the frame's features.
 */
HAILORTAPI hailo_status hailo_get_input_stream_quant_infos(hailo_input_stream stream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count);

/**
 * Gets quant infos for a given output stream.
 * 
 * @param[in]     stream       A ::hailo_output_stream object.
 * @param[out]    quant_infos  A pointer to a buffer of @a hailo_quant_info_t that will be filled with quant infos.
 * @param[inout]  quant_infos_count   As input - the maximum amount of entries in @a quant_infos array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case a single qp is present - the returned list will be of size 1.
 *       Otherwise - the returned list will be of the same length as the number of the frame's features.
 */
HAILORTAPI hailo_status hailo_get_output_stream_quant_infos(hailo_output_stream stream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count);

/**
 * Synchronously reads data from a stream.
 *
 * @param[in] stream            A ::hailo_output_stream object.
 * @param[in] buffer            A pointer to a buffer that receives the data read from @a stream.
 * @param[in] size              The amount of bytes to read, should be the frame size.
 *
 * @note The output buffer format comes from the \e format field inside ::hailo_stream_info_t and the shape comes from
 *            the \e hw_shape field inside ::hailo_stream_info_t.
 * @note @a size is expected to be stream_info.hw_frame_size.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_stream_read_raw_buffer(hailo_output_stream stream, void *buffer, size_t size);

/**
 * Synchronously writes all data to a stream.
 *
 * @param[in] stream   A ::hailo_input_stream object.
 * @param[in] buffer   A pointer to a buffer that contains the data to be written to @a stream.
 * @param[in] size     The amount of bytes to write.
 *
 * @note The input buffer format comes from the \e format field inside ::hailo_stream_info_t and the shape comes from
 *            the \e hw_shape field inside ::hailo_stream_info_t.
 * @note @a size is expected to be stream_info.hw_frame_size.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_stream_write_raw_buffer(hailo_input_stream stream, const void *buffer, size_t size);

/**
 * Waits until the stream is ready to launch a new ::hailo_stream_read_raw_buffer_async operation. Each stream has a
 * limited-size queue for ongoing transfers. You can retrieve the queue size for the given stream by calling
 * ::hailo_output_stream_get_async_max_queue_size.
 *
 * @param[in] stream            A ::hailo_output_stream object.
 * @param[in] transfer_size     Must be the result of ::hailo_get_output_stream_frame_size for the given stream.
 * @param[in] timeout_ms        Amount of time to wait until the stream is ready in milliseconds.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
 *           - If @a timeout_ms has passed and the stream is not ready, returns ::HAILO_TIMEOUT.
 *           - In any other error case, returns ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_stream_wait_for_async_output_ready(hailo_output_stream stream, size_t transfer_size,
    uint32_t timeout_ms);

/**
 * Waits until the stream is ready to launch a new ::hailo_stream_write_raw_buffer_async operation. Each stream has a
 * limited-size queue for ongoing transfers. You can retrieve the queue size for the given stream by calling
 * ::hailo_input_stream_get_async_max_queue_size.
 *
 * @param[in] stream            A ::hailo_input_stream object.
 * @param[in] transfer_size     Must be the result of ::hailo_get_input_stream_frame_size for the given stream.
 * @param[in] timeout_ms        Amount of time to wait until the stream is ready in milliseconds.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
 *           - If @a timeout_ms has passed and the stream is not ready, returns ::HAILO_TIMEOUT.
 *           - In any other error case, returns ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_stream_wait_for_async_input_ready(hailo_input_stream stream, size_t transfer_size,
    uint32_t timeout_ms);

/**
 * Returns the maximum amount of frames that can be simultaneously read from the stream (by
 * ::hailo_stream_read_raw_buffer_async calls) before any one of the read operations is complete, as signified by
 * @a user_callback being called.
 *
 * @param[in] stream        A ::hailo_output_stream object.
 * @param[out] queue_size   Returns value of the queue
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_output_stream_get_async_max_queue_size(hailo_output_stream stream, size_t *queue_size);

/**
 * Returns the maximum amount of frames that can be simultaneously written to the stream (by
 * ::hailo_stream_write_raw_buffer_async calls) before any one of the write operations is complete, as signified by
 *  @a user_callback being called.
 *
 * @param[in] stream        A ::hailo_input_stream object.
 * @param[out] queue_size   Returns value of the queue
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_input_stream_get_async_max_queue_size(hailo_input_stream stream, size_t *queue_size);

/**
 * Reads into @a buffer from the stream asynchronously, initiating a deferred operation that will be completed
 * later.
 * - If the function call succeeds (i.e., ::hailo_stream_read_raw_buffer_async returns ::HAILO_SUCCESS), the deferred
 *   operation has been initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
 * - If the function call fails (i.e., ::hailo_stream_read_raw_buffer_async returns a status other than
 *   ::HAILO_SUCCESS), the deferred operation will not be initiated and @a user_callback will not be invoked. The user
 *   is free to change or delete @a buffer.
 * - @a user_callback is triggered upon successful completion or failure of the deferred operation.
 *   The callback receives a ::hailo_stream_read_async_completion_info_t object containing a pointer to the transferred
 *   buffer (@a buffer_addr) and the transfer status (@a status). If the operation has completed successfully, the
 *   contents of @a buffer will have been updated by the read operation.
 *
 * @param[in] stream        A ::hailo_output_stream object.
 * @param[in] buffer        The buffer to be read into.
 *                          The buffer must be aligned to the system page size.
 * @param[in] size          The size of the given buffer, expected to be the result of
 *                          ::hailo_get_output_stream_frame_size.
 * @param[in] user_callback The callback that will be called when the transfer is complete or has failed.
 * @param[in] opaque        Optional pointer to user-defined context (may be NULL if not desired).
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
 *         - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL.
 *           In this case, please wait until @a user_callback is called on previous
 *           reads, or call ::hailo_stream_wait_for_async_output_ready. The size of the queue can be
 *           determined by calling ::hailo_output_stream_get_async_max_queue_size.
 *         - In any other error case, returns a ::hailo_status error.
 *
 * @note @a user_callback should execute as quickly as possible.
 * @note The output buffer format comes from the \e format field inside ::hailo_stream_info_t and the shape comes from
 *       the \e hw_shape field inside ::hailo_stream_info_t.
 * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
 *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
 *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
 */
HAILORTAPI hailo_status hailo_stream_read_raw_buffer_async(hailo_output_stream stream, void *buffer, size_t size,
    hailo_stream_read_async_callback_t user_callback, void *opaque);

/**
 * Writes the contents of @a buffer to the stream asynchronously, initiating a deferred operation that will be
 * completed later.
 * - If the function call succeeds (i.e., ::hailo_stream_write_raw_buffer_async returns ::HAILO_SUCCESS), the deferred
 *   operation has been initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
 * - If the function call fails (i.e., ::hailo_stream_write_raw_buffer_async returns a status other than
 *   ::HAILO_SUCCESS), the deferred operation will not be initiated and @a user_callback will not be invoked. The user
 *   is free to change or delete @a buffer.
 * - @a user_callback is triggered upon successful completion or failure of the deferred operation. The callback
 *   receives a ::hailo_stream_write_async_completion_info_t object containing a pointer to the transferred buffer
 *   (@a buffer_addr) and the transfer status (@a status).
 *
 * @param[in] stream         A ::hailo_input_stream object.
 * @param[in] buffer            The buffer to be written.
 *                              The buffer must be aligned to the system page size.
 * @param[in] size              The size of the given buffer, expected to be the result of
 *                              ::hailo_get_input_stream_frame_size.
 * @param[in] user_callback     The callback that will be called when the transfer is complete
 *                              or has failed.
 * @param[in] opaque         Optional pointer to user-defined context (may be NULL if not desired).
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
 *           - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL. In this case please wait
 *             until @a user_callback is called on previous writes, or call ::hailo_stream_wait_for_async_input_ready.
 *             The size of the queue can be determined by calling ::hailo_input_stream_get_async_max_queue_size.
 *           - In any other error case, returns a ::hailo_status error.
 *
 * @note @a user_callback should run as quickly as possible.
 * @note The input buffer format comes from the \e format field inside ::hailo_stream_info_t and the shape comes from
 *       the \e hw_shape field inside ::hailo_stream_info_t.
 * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
 *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
 *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
 */
HAILORTAPI hailo_status hailo_stream_write_raw_buffer_async(hailo_input_stream stream, const void *buffer, size_t size,
    hailo_stream_write_async_callback_t user_callback, void *opaque);

/**
 * Gets the size of a stream's frame on the host side in bytes
 * (the size could be affected by the format type - for example using UINT16, or by the data having not yet been quantized)
 *
 * @param[in] stream_info             The stream's info represented by ::hailo_stream_info_t
 * @param[in] transform_params        Host side transform parameters
 * @return The size of the frame on the host side in bytes
 */
HAILORTAPI size_t hailo_get_host_frame_size(const hailo_stream_info_t *stream_info, const hailo_transform_params_t *transform_params);

/** @} */ // end of group_stream_functions

/** @defgroup group_transform_functions Data transformation functions
 *  @{
 */

/**
 * Creates an input transform_context object. Allocates all necessary buffers used for the transformation (pre-process).
 * 
 * @param[in]     stream_info - A ::hailo_stream_info_t object
 * @param[in]     transform_params - A ::hailo_transform_params_t user transformation parameters.
 * @param[out]    transform_context - A ::hailo_input_transform_context
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note To release the transform_context, call the ::hailo_release_input_transform_context function
 *      with the returned ::hailo_input_transform_context.
 * 
 */
HAILORTAPI hailo_status hailo_create_input_transform_context(const hailo_stream_info_t *stream_info,
    const hailo_transform_params_t *transform_params, hailo_input_transform_context *transform_context);

/**
 * Creates an input transform_context object. Allocates all necessary buffers used for the transformation (pre-process).
 *
 * @param[in]     stream              A ::hailo_input_stream object
 * @param[in]     transform_params    A ::hailo_transform_params_t user transformation parameters.
 * @param[out]    transform_context   A ::hailo_input_transform_context
 *
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note To release the transform_context, call the ::hailo_release_input_transform_context function
 *      with the returned ::hailo_input_transform_context.
 *
 */
HAILORTAPI hailo_status hailo_create_input_transform_context_by_stream(hailo_input_stream stream,
    const hailo_transform_params_t *transform_params, hailo_input_transform_context *transform_context);

/**
 * Releases a transform_context object including all allocated buffers.
 * 
 * @param[in]    transform_context - A ::hailo_input_transform_context object.
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_input_transform_context(hailo_input_transform_context transform_context);

/**
 * Check whether or not a transformation is needed - for quant_info per feature case.
 *
 * @param[in]  src_image_shape         The shape of the src buffer (host shape).
 * @param[in]  src_format              The format of the src buffer (host format).
 * @param[in]  dst_image_shape         The shape of the dst buffer (hw shape).
 * @param[in]  dst_format              The format of the dst buffer (hw format).
 * @param[in]  quant_infos             A pointer to an array of ::hailo_quant_info_t object containing quantization information.
 * @param[in]  quant_infos_count       The number of ::hailo_quant_info_t elements pointed by quant_infos.
 *                                     quant_infos_count should be equals to either 1, or src_image_shape.features
 * @param[out] transformation_required Indicates whether or not a transformation is needed.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case @a transformation_required is false, the src frame is ready to be sent to HW without any transformation.
 */
HAILORTAPI hailo_status hailo_is_input_transformation_required2(
    const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format,
    const hailo_quant_info_t *quant_infos, size_t quant_infos_count, bool *transformation_required);

/**
 * Transforms an input frame pointed to by @a src directly to the buffer pointed to by @a dst.
 * 
 * @param[in]  transform_context    A ::hailo_input_transform_context.
 * @param[in]  src                  A pointer to a buffer to be transformed.
 * @param[in]  src_size             The number of bytes to transform. This number must be equal to the input host_frame_size,
 *                                  and less than or equal to the size of @a src buffer.
 * @param[out] dst                  A pointer to a buffer that receives the transformed data.
 * @param[in]  dst_size             The number of bytes in @a dst buffer. This number must be equal to the input hw_frame_size,
 *                                  and less than or equal to the size of @a dst buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @warning The buffers must not overlap.
 */
HAILORTAPI hailo_status hailo_transform_frame_by_input_transform_context(hailo_input_transform_context transform_context,
    const void *src, size_t src_size, void *dst, size_t dst_size);

/**
 * Check whether or not a transformation is needed - for quant_info per feature case.
 *
 * @param[in]  src_image_shape         The shape of the src buffer (hw shape).
 * @param[in]  src_format              The format of the src buffer (hw format).
 * @param[in]  dst_image_shape         The shape of the dst buffer (host shape).
 * @param[in]  dst_format              The format of the dst buffer (host format).
 * @param[in]  quant_infos             A pointer to an array of ::hailo_quant_info_t object containing quantization information.
 * @param[in]  quant_infos_count       The number of ::hailo_quant_info_t elements pointed by quant_infos.
 *                                     quant_infos_count should be equals to either 1, or dst_image_shape.features.
 * @param[out] transformation_required Indicates whether or not a transformation is needed.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case @a transformation_required is false, the src frame is already in the required format without any transformation.
 */
HAILORTAPI hailo_status hailo_is_output_transformation_required2(
    const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format,
    const hailo_quant_info_t *quant_infos, size_t quant_infos_count, bool *transformation_required);

/**
 * Creates an output transform_context object. Allocates all necessary buffers used for the transformation (post-process).
 * 
 * @param[in]     stream_info - A ::hailo_stream_info_t object
 * @param[in]     transform_params - A ::hailo_transform_params_t user transformation parameters.
 * @param[out]    transform_context - A ::hailo_output_transform_context
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note To release the transform_context, call the ::hailo_release_output_transform_context function
 *      with the returned ::hailo_output_transform_context.
 */
HAILORTAPI hailo_status hailo_create_output_transform_context(const hailo_stream_info_t *stream_info,
    const hailo_transform_params_t *transform_params, hailo_output_transform_context *transform_context);

/**
 * Creates an output transform_context object. Allocates all necessary buffers used for the transformation (post-process).
 * 
 * @param[in]     stream              A ::hailo_output_stream object
 * @param[in]     transform_params    A ::hailo_transform_params_t user transformation parameters.
 * @param[out]    transform_context   A ::hailo_output_transform_context
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note To release the transform_context, call the ::hailo_release_output_transform_context function
 *      with the returned ::hailo_output_transform_context.
 */
HAILORTAPI hailo_status hailo_create_output_transform_context_by_stream(hailo_output_stream stream,
     const hailo_transform_params_t *transform_params, hailo_output_transform_context *transform_context);

/**
 * Releases a transform_context object including all allocated buffers.
 * 
 * @param[in]    transform_context - A ::hailo_output_transform_context object.
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_output_transform_context(hailo_output_transform_context transform_context);

/**
 * Transforms an output frame pointed to by @a src directly to the buffer pointed to by @a dst.
 * 
 * @param[in]  transform_context    A ::hailo_output_transform_context.
 * @param[in]  src                  A pointer to a buffer to be transformed.
 * @param[in]  src_size             The number of bytes to transform. This number must be equal to the output hw_frame_size,
 *                                  and less than or equal to the size of @a src buffer.
 * @param[out] dst                  A pointer to a buffer that receives the transformed data.
 * @param[in]  dst_size             The number of bytes in @a dst buffer. This number must be equal to the output host_frame_size,
 *                                  and less than or equal to the size of @a dst buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @warning The buffers must not overlap.
 */
HAILORTAPI hailo_status hailo_transform_frame_by_output_transform_context(hailo_output_transform_context transform_context,
    const void *src, size_t src_size, void *dst, size_t dst_size);

/**
 * Returns whether or not qp is valid
 *
 * @param[in]     quant_info      A ::hailo_quant_info_t object.
 * @param[out]    is_qp_valid     Indicates whether or not qp is valid.
 *
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note QP will be invalid in case HEF file was compiled with multiple QPs, and then the user will try working with API for single QP.
 *       For example - if HEF was compiled with multiple QPs and then the user calls hailo_get_input_stream_info,
 *       The ::hailo_quant_info_t object of the ::hailo_stream_info_t object will be invalid.
 */
HAILORTAPI hailo_status hailo_is_qp_valid(const hailo_quant_info_t quant_info, bool *is_qp_valid);

/**
 * Creates an demuxer for the given mux stream. Allocates all necessary buffers
 * used for the demux process.
 * 
 * @param[in]     stream - A ::hailo_output_stream object
 * @param[in]     demux_params - A ::hailo_demux_params_t user demux parameters.
 * @param[out]    demuxer - A ::hailo_output_demuxer, used to transform output frames
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * 
 * @note To release the demuxer, call the ::hailo_release_output_demuxer function
 *      with the returned ::hailo_output_demuxer.
 * 
 */
HAILORTAPI hailo_status hailo_create_demuxer_by_stream(hailo_output_stream stream,
    const hailo_demux_params_t *demux_params, hailo_output_demuxer *demuxer);

/**
 * Releases a demuxer object.
 * 
 * @param[in]    demuxer - A ::hailo_output_demuxer object.
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_output_demuxer(hailo_output_demuxer demuxer);

/**
 * Demultiplexing an output frame pointed to by @a src directly to the buffers pointed to by @a raw_buffers.
 * 
 * @param[in]     demuxer            A ::hailo_output_demuxer object used for the demuxing.
 * @param[in]     src                A pointer to a buffer to be demultiplexed.
 * @param[in]     src_size           The number of bytes to demultiplexed. This number must be equal to the
 *                                   hw_frame_size, and less than or equal to the size of @a src buffer.
 * @param[in,out] raw_buffers        A pointer to an array of ::hailo_stream_raw_buffer_t that receives the
 *                                   demultiplexed data read from the @a stream.
 * @param[in]     raw_buffers_count  The number of ::hailo_stream_raw_buffer_t elements in the array pointed to by
 *                                   @a raw_buffers.
 * @note The order of @a raw_buffers should be the same as returned from the function 'hailo_get_mux_infos_by_output_demuxer()'.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_demux_raw_frame_by_output_demuxer(hailo_output_demuxer demuxer, const void *src,
    size_t src_size, hailo_stream_raw_buffer_t *raw_buffers, size_t raw_buffers_count);

/**
 * Demultiplexing an output frame pointed to by @a src directly to the buffers pointed to by @a raw_buffers_by_name.
 *
 * @param[in]     demuxer              A ::hailo_output_demuxer object used for the demuxing.
 * @param[in]     src                  A pointer to a buffer to be demultiplexed.
 * @param[in]     src_size             The number of bytes to demultiplexed. This number must be equal to the
 *                                     hw_frame_size, and less than or equal to the size of @a src buffer.
 * @param[in,out] raw_buffers_by_name  A pointer to an array of ::hailo_stream_raw_buffer_by_name_t that receives the
 *                                     demultiplexed data read from the @a stream. hailo_stream_raw_buffer_by_name_t::name should
 *                                     be filled with the demuxes names.
 * @param[in]     raw_buffers_count    The number of ::hailo_stream_raw_buffer_by_name_t elements in the array pointed to by
 *                                     @a raw_buffers_by_name.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_demux_by_name_raw_frame_by_output_demuxer(hailo_output_demuxer demuxer, const void *src,
    size_t src_size, hailo_stream_raw_buffer_by_name_t *raw_buffers_by_name, size_t raw_buffers_count);

/**
 * Gets all multiplexed stream infos.
 * 
 * @param[in]  demuxer                A ::hailo_output_demuxer object.
 * @param[out] stream_infos           A pointer to a buffer of ::hailo_stream_info_t that receives the information.
 * @param[inout] number_of_streams    The maximum amount of streams_info to fill. This variable will be filled with
 *                                    the actual number of multiplexed stream_infos. If the buffer is insufficient
 *                                    to hold the information this variable will be set to the requested value,
 *                                    ::HAILO_INSUFFICIENT_BUFFER would be returned.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_mux_infos_by_output_demuxer(hailo_output_demuxer demuxer, hailo_stream_info_t *stream_infos,
    size_t *number_of_streams);

/**
 * Fuse multiple defused NMS buffers pointed by @a nms_fuse_inputs to the buffer pointed to by @a fused_buffer.
 * This function should be called on @a nms_fuse_inputs after receiving them from HW, and before transformation.
 * This function expects @a nms_fuse_inputs to be ordered by their @a class_group_index (lowest to highest).
 * 
 * @param[in]     nms_fuse_inputs    Array of @a hailo_nms_fuse_input_t structs which contain the buffers to be fused.
 * @param[in]     inputs_count       How many members in @a nms_fuse_inputs.
 * @param[out]    fused_buffer       A pointer to a buffer which will contain the fused buffer.
 * @param[in]     fused_buffer_size  The fused buffer size.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_fuse_nms_frames(const hailo_nms_fuse_input_t *nms_fuse_inputs,
    uint32_t inputs_count, uint8_t *fused_buffer, size_t fused_buffer_size);

/** @} */ // end of group_transform_functions

/** @defgroup group_vstream_functions Virtual Stream functions
 *  @{
 */

/**
 * Creates input virtual stream params linked to a network or a network group.
 *
 * @param[in] hef                       A @a hailo_hef object that contains the information.
 * @param[in] name                      The name of the network group or network which contains the input virtual streams. 
 *                                      In case network group name is given, the function returns input virtual stream params 
 *                                      of all the networks of the given network group.
 *                                      In case network name is given (provided by @a hailo_hef_get_network_infos), 
 *                                      the function returns input virtual stream params of the given network.
 *                                      If NULL is passed, the function returns the input virtual stream params of 
 *                                      all the networks of the first network group.
 * @param[in] unused                    Unused.
 * @param[in] format_type               The default format type for all input virtual streams.
 * @param[out] input_params             List of params for input virtual streams.
 * @param[inout] input_params_count     On input: Amount of @a input_params array.
 *                                      On output: Will be filled with the detected amount of input vstreams on the @a network or @a network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_make_input_vstream_params(hailo_hef hef, const char *name, 
    bool unused, hailo_format_type_t format_type, 
    hailo_input_vstream_params_by_name_t *input_params, size_t *input_params_count);

/**
 * Creates output virtual stream params linked to a network or a network group.
 *
 * @param[in] hef                       A @a hailo_hef object that contains the information.
 * @param[in] name                      The name of the network group or network which contains the output virtual streams. 
 *                                      In case network group name is given, the function returns output virtual stream params 
 *                                      of all the networks of the given network group.
 *                                      In case network name is given (provided by @a hailo_hef_get_network_infos), 
 *                                      the function returns output virtual stream params of the given network.
 *                                      If NULL is passed, the function returns the output virtual stream params of 
 *                                      all the networks of the first network group.
 * @param[in] unused                    Unused.
 * @param[in] format_type               The default format type for all output virtual streams.
 * @param[out] output_params            List of params for output virtual streams.
 * @param[inout] output_params_count    On input: Amount of @a output_params array.
 *                                      On output: Will be filled with the detected amount of output vstreams on the @a network or @a network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_make_output_vstream_params(hailo_hef hef, const char *name, 
    bool unused, hailo_format_type_t format_type, 
    hailo_output_vstream_params_by_name_t *output_params, size_t *output_params_count);

/**
 * Creates input virtual stream params for a given network_group.
 *
 * @param[in]  network_group            Network group that owns the streams.
 * @param[in]  unused                   Unused.
 * @param[in]  format_type              The default format type for all input virtual streams.
 * @param[out] input_params             List of params for input virtual streams.
 * @param[inout] input_params_count     On input: Amount of @a input_params array.
 *                                      On output: Will be filled with the detected amount of input vstreams on the @a network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_make_input_vstream_params(hailo_configured_network_group network_group, bool unused,
    hailo_format_type_t format_type, hailo_input_vstream_params_by_name_t *input_params, size_t *input_params_count);

/**
 * Creates output virtual stream params for given network_group.
 *
 * @param[in]  network_group            Network group that owns the streams.
 * @param[in]  unused                   Unused.
 * @param[in]  format_type              The default format type for all output virtual streams.
 * @param[out] output_params            List of params for output virtual streams.
 * @param[inout] output_params_count    On input: Amount of @a output_params array.
 *                                      On output: Will be filled with the detected amount of output vstreams on the @a network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_make_output_vstream_params(hailo_configured_network_group network_group, bool unused,
    hailo_format_type_t format_type, hailo_output_vstream_params_by_name_t *output_params,
    size_t *output_params_count);

/**
 * Gets output virtual stream groups for given network_group. The groups are split with respect to their low-level streams.
 *
 * @param[in]  network_group                   Network group that owns the streams.
 * @param[out] output_name_by_group            List of params for output virtual streams.
 * @param[inout] output_name_by_group_count    On input: Amount of @a output_name_by_group array.
 *                                             On output: Will be filled with the detected amount of output vstreams on the @a network_group.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_output_vstream_groups(hailo_configured_network_group network_group,
    hailo_output_vstream_name_by_group_t *output_name_by_group, size_t *output_name_by_group_count);

/**
 * Creates input virtual streams.
 *
 * @param[in]  configured_network_group  Network group that owns the streams.
 * @param[in]  inputs_params             List of input virtual stream params to create input virtual streams from.
 * @param[in]  inputs_count              How many members in @a input_params.
 * @param[out] input_vstreams            List of input virtual streams. 
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note To release input virtual streams, call the ::hailo_release_input_vstreams function with the returned @a input_vstreams and @a inputs_count.
 */
HAILORTAPI hailo_status hailo_create_input_vstreams(hailo_configured_network_group configured_network_group,
    const hailo_input_vstream_params_by_name_t *inputs_params, size_t inputs_count, hailo_input_vstream *input_vstreams);

/**
 * Creates output virtual streams.
 *
 * @param[in]  configured_network_group   Network group that owns the streams.
 * @param[in]  outputs_params             List of output virtual stream params to create output virtual streams from.
 * @param[in]  outputs_count              How many members in @a outputs_params.
 * @param[out] output_vstreams            List of output virtual streams. 
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note If not creating all output vstreams together, one should make sure all vstreams from the same group are created together. See ::hailo_get_output_vstream_groups
 * @note To release output virtual streams, call the ::hailo_release_output_vstreams function with the returned @a output_vstreams and @a outputs_count.
 */
HAILORTAPI hailo_status hailo_create_output_vstreams(hailo_configured_network_group configured_network_group,
    const hailo_output_vstream_params_by_name_t *outputs_params, size_t outputs_count, hailo_output_vstream *output_vstreams);

/**
 * Gets the size of a virtual stream's frame on the host side in bytes
 * (the size could be affected by the format type - for example using UINT16, or by the data having not yet been quantized)
 *
 * @param[in]  input_vstream    A ::hailo_input_vstream object.
 * @param[out] frame_size       The size of the frame on the host side in bytes.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_input_vstream_frame_size(hailo_input_vstream input_vstream, size_t *frame_size);

/**
 * Gets the ::hailo_vstream_info_t struct for the given vstream.
 *
 * @param[in]  input_vstream    A ::hailo_input_vstream object.
 * @param[out] vstream_info     Will be filled with ::hailo_vstream_info_t.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_input_vstream_info(hailo_input_vstream input_vstream, hailo_vstream_info_t *vstream_info);

/**
 * Gets the user buffer format struct for the given vstream.
 *
 * @param[in]  input_vstream        A ::hailo_input_vstream object.
 * @param[out] user_buffer_format   Will be filled with ::hailo_format_t.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_input_vstream_user_format(hailo_input_vstream input_vstream, hailo_format_t *user_buffer_format);

/**
 * Gets quant infos for a given input vstream.
 *
 * @param[in]     vstream      A ::hailo_input_vstream object.
 * @param[out]    quant_infos  A pointer to a buffer of @a hailo_quant_info_t that will be filled with quant infos.
 * @param[inout]  quant_infos_count   As input - the maximum amount of entries in @a quant_infos array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case a single qp is present - the returned list will be of size 1.
 *       Otherwise - the returned list will be of the same length as the number of the frame's features.
 */
HAILORTAPI hailo_status hailo_get_input_vstream_quant_infos(hailo_input_vstream vstream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count);

/**
 * Gets quant infos for a given output vstream.
 *
 * @param[in]     vstream      A ::hailo_output_vstream object.
 * @param[out]    quant_infos  A pointer to a buffer of @a hailo_quant_info_t that will be filled with quant infos.
 * @param[inout]  quant_infos_count   As input - the maximum amount of entries in @a quant_infos array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case a single qp is present - the returned list will be of size 1.
 *       Otherwise - the returned list will be of the same length as the number of the frame's features.
 */
HAILORTAPI hailo_status hailo_get_output_vstream_quant_infos(hailo_output_vstream vstream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count);

/**
 * Gets the size of a virtual stream's frame on the host side in bytes
 * (the size could be affected by the format type - for example using UINT16, or by the data having not yet been quantized)
 *
 * @param[in]  output_vstream   A ::hailo_output_vstream object.
 * @param[out] frame_size       The size of the frame on the host side in bytes.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_output_vstream_frame_size(hailo_output_vstream output_vstream, size_t *frame_size);

/**
 * Gets the ::hailo_vstream_info_t struct for the given vstream.
 *
 * @param[in]  output_vstream    A ::hailo_output_vstream object.
 * @param[out] vstream_info     Will be filled with ::hailo_vstream_info_t.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_output_vstream_info(hailo_output_vstream output_vstream, hailo_vstream_info_t *vstream_info);

/**
 * Gets the user buffer format struct for the given vstream.
 *
 * @param[in]  output_vstream       A ::hailo_output_vstream object.
 * @param[out] user_buffer_format   Will be filled with ::hailo_format_t.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_output_vstream_user_format(hailo_output_vstream output_vstream, hailo_format_t *user_buffer_format);

/**
 * Gets the size of a virtual stream's frame in bytes
 * (the size could be affected by the format type - for example using UINT16, or by the data having not yet been quantized)
 *
 * @param[in]  vstream_info          A ::hailo_vstream_info_t object.
 * @param[in]  user_buffer_format    A ::hailo_format_t object.
 * @param[out] frame_size            The size of the frame on the host side in bytes.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_vstream_frame_size(hailo_vstream_info_t *vstream_info, hailo_format_t *user_buffer_format, size_t *frame_size);

/**
 * Writes buffer to hailo device via input virtual stream @a input_vstream.
 *
 * @param[in] input_vstream    A ::hailo_input_vstream object.
 * @param[in] buffer           A pointer to a buffer to be sent. The buffer format comes from the vstream's \e format
 *                             (Can be obtained using ::hailo_get_input_vstream_user_format) and the shape comes from
 *                             \e shape inside ::hailo_vstream_info_t (Can be obtained using ::hailo_get_input_vstream_info).
 * @param[in] buffer_size      @a buffer buffer size in bytes. The size is expected to be the size returned from
 *                             ::hailo_get_input_vstream_frame_size.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_vstream_write_raw_buffer(hailo_input_vstream input_vstream, const void *buffer, size_t buffer_size);

/**
 * Writes thte buffer to hailo device via input virtual stream @a input_vstream.
 *
 * @param[in] input_vstream    A ::hailo_input_vstream object.
 * @param[in] buffer           A pointer to the buffer containing
 *                             pointers to the planes to where the data to
 *                             be sent to the device is stored.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note Currently only support memory_type field of buffer to be HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR.
 */
HAILORTAPI hailo_status hailo_vstream_write_pix_buffer(hailo_input_vstream input_vstream, const hailo_pix_buffer_t *buffer);

/**
 * Blocks until the pipeline buffers of @a input_vstream are flushed.
 * 
 * @param[in] input_vstream                 The input vstream to flush.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_flush_input_vstream(hailo_input_vstream input_vstream);

/**
 * Reads data from hailo device via @a output_vstream into @a dst.
 *
 * @param[in] output_vstream   A ::hailo_output_vstream object.
 * @param[in] buffer           A pointer to the received buffer. The buffer format comes from the vstream's \e format
 *                             (Can be obtained using ::hailo_get_output_vstream_user_format) and the shape comes from
 *                             \e shape or \e nms_shape inside ::hailo_vstream_info_t (Can be obtained
 *                             using ::hailo_get_output_vstream_info).
 * @param[in] buffer_size      @a dst buffer size in bytes.The size is expected to be the size returned from
 *                             ::hailo_get_output_vstream_frame_size.
 * 
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_vstream_read_raw_buffer(hailo_output_vstream output_vstream, void *buffer, size_t buffer_size);

/**
 * Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.
 *
 * @param[in] output_vstream   A ::hailo_output_vstream object.
 * @param[in] threshold        NMS score threshold to set.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note This function will fail in cases where the output vstream has no NMS operations on the CPU.
 */
HAILORTAPI hailo_status hailo_vstream_set_nms_score_threshold(hailo_output_vstream output_vstream, float32_t threshold);

/**
 * Set NMS intersection over union overlap Threshold,
 * used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.
 *
 * @param[in] output_vstream   A ::hailo_output_vstream object.
 * @param[in] threshold        NMS IoU threshold to set.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note This function will fail in cases where the output vstream has no NMS operations on the CPU.
 */
HAILORTAPI hailo_status hailo_vstream_set_nms_iou_threshold(hailo_output_vstream output_vstream, float32_t threshold);

/**
 * Set a limit for the maximum number of boxes per class.
 *
 * @param[in] output_vstream             A ::hailo_output_vstream object.
 * @param[in] max_proposals_per_class    NMS max proposals per class to set.
 *
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note This function will fail in cases where the output vstream has no NMS operations on the CPU.
 */
HAILORTAPI hailo_status hailo_vstream_set_nms_max_proposals_per_class(hailo_output_vstream output_vstream, uint32_t max_proposals_per_class);

/**
 * Release input virtual streams.
 * 
 * @param[in] input_vstreams   List of input virtual streams to be released.
 * @param[in] inputs_count     The amount of elements in @a input_params.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_input_vstreams(const hailo_input_vstream *input_vstreams, size_t inputs_count);

/**
 * Release output virtual streams.
 * 
 * @param[in] output_vstreams  List of output virtual streams to be released.
 * @param[in] outputs_count    The amount of elements in @a output_vstreams.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_release_output_vstreams(const hailo_output_vstream *output_vstreams, size_t outputs_count);

/**
 * Clears the pipeline buffers of each vstream in @a input_vstreams.
 * 
 * @param[in] input_vstreams                List of input virtual streams to be cleared.
 * @param[in] inputs_count                  The amount of elements in @a input_params.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_clear_input_vstreams(const hailo_input_vstream *input_vstreams, size_t inputs_count);

/**
 * Clears the pipeline buffers of each vstream in @a output_vstreams.
 * 
 * @param[in] output_vstreams               List of output virtual streams to be cleared.
 * @param[in] outputs_count                 The amount of elements in @a output_vstreams.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note If not all output vstreams from the same group are passed together, it will cause an <b> undefined behavior </b>.
 *       See ::hailo_get_output_vstream_groups, to get the output vstreams' groups.
 */
HAILORTAPI hailo_status hailo_clear_output_vstreams(const hailo_output_vstream *output_vstreams, size_t outputs_count);

/**
 * Run simple inference using vstreams pipelines.
 *
 * @param[in] configured_network_group      A ::hailo_configured_network_group to run the inference on.
 * @param[in] inputs_params                 Array of input virtual stream params, indicates @a input_buffers format.
 * @param[in] input_buffers                 Array of ::hailo_stream_raw_buffer_by_name_t. Ths input dataset of the inference.
 * @param[in] inputs_count                  The amount of elements in @a inputs_params and @a input_buffers.
 * @param[in] outputs_params                Array of output virtual stream params, indicates @a output_buffers format.
 * @param[out] output_buffers               Array of ::hailo_stream_raw_buffer_by_name_t. Ths results of the inference.
 * @param[in] outputs_count                 The amount of elements in @a outputs_params and @a output_buffers.
 * @param[in] frames_count                  The amount of inferred frames.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note @a configured_network_group should be activated before calling this function.
 * @note the size of each element in @a input_buffers and @a output_buffers should match the product of @a frames_count
 *       and the frame size of the matching ::hailo_input_vstream / ::hailo_output_vstream.     
 */
HAILORTAPI hailo_status hailo_infer(hailo_configured_network_group configured_network_group,
    hailo_input_vstream_params_by_name_t *inputs_params, hailo_stream_raw_buffer_by_name_t *input_buffers, size_t inputs_count,
    hailo_output_vstream_params_by_name_t *outputs_params, hailo_stream_raw_buffer_by_name_t *output_buffers, size_t outputs_count,
    size_t frames_count);


/** @} */ // end of group_vstream_functions

/** @defgroup multi_network_functions multi network functions
 *  @{
 */

/**
 * Gets all network infos under a given network group
 *
 * @param[in]  hef                    A ::hailo_hef object that contains the information.
 * @param[in]  network_group_name     Name of the network_group to get the network infos by. If NULL is passed,
 *                                    the first network_group in the HEF will be addressed.
 * @param[out] networks_infos         A pointer to a buffer of ::hailo_network_info_t that receives the informations.
 * @param[inout] number_of_networks   As input - the maximum amount of entries in @a hailo_network_info_t array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_hef_get_network_infos(hailo_hef hef, const char *network_group_name,
    hailo_network_info_t *networks_infos, size_t *number_of_networks);

/**
 * Gets all network infos under a given network group
 *
 * @param[in]  network_group          A ::hailo_configured_network_group object to get the network infos from.
 * @param[out] networks_infos         A pointer to a buffer of ::hailo_network_info_t that receives the informations.
 * @param[inout] number_of_networks   As input - the maximum amount of entries in @a hailo_network_info_t array.
 *                                    As output - the actual amount of entries written if the function returns with ::HAILO_SUCCESS
 *                                    or the amount of entries needed if the function returns ::HAILO_INSUFFICIENT_BUFFER.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, if the buffer is
 *                                    insufficient to hold the information a ::HAILO_INSUFFICIENT_BUFFER would be
 *                                    returned. In any other case, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status hailo_get_network_infos(hailo_configured_network_group network_group,
    hailo_network_info_t *networks_infos, size_t *number_of_networks);

/** @} */ // end of multi_network_functions

/** @defgroup group_advanced_API_functions hailo advence API functions
 *  @{
 */

typedef enum hailo_sleep_state_e {
    HAILO_SLEEP_STATE_SLEEPING   = 0,
    HAILO_SLEEP_STATE_AWAKE = 1,

    /** Max enum value to maintain ABI Integrity */
    HAILO_SLEEP_STATE_MAX_ENUM = HAILO_MAX_ENUM
} hailo_sleep_state_t;

/**
 *  Set chip sleep state.
 * @note This is an advanced API. Please be advised not to use this API, unless supported by Hailo.
 * 
 * @param[in]     device         A ::hailo_device object.
 * @param[in]     sleep_state    The requested sleep state of the chip
 * 
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 */

HAILORTAPI hailo_status hailo_set_sleep_state(hailo_device device, hailo_sleep_state_t sleep_state);

/** @} */ // end of group_advanced_API_functions

/** @defgroup group_deprecated_functions_and_defines Deprecated functions and defines
 *  @{
 */

/**
 * Check whether or not a transformation is needed.
 *
 * @param[in]  src_image_shape         The shape of the src buffer (host shape).
 * @param[in]  src_format              The format of the src buffer (host format).
 * @param[in]  dst_image_shape         The shape of the dst buffer (hw shape).
 * @param[in]  dst_format              The format of the dst buffer (hw format).
 * @param[in]  quant_info              A ::hailo_quant_info_t object containing quantization information.
 * @param[out] transformation_required Indicates whether or not a transformation is needed.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case @a transformation_required is false, the src frame is ready to be sent to HW without any transformation.
 */
HAILORTAPI hailo_status hailo_is_input_transformation_required(
    const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format,
    const hailo_quant_info_t *quant_info, bool *transformation_required)
    DEPRECATED("hailo_is_input_transformation_required is deprecated. Please use hailo_is_input_transformation_required2 instead.");

/**
 * Check whether or not a transformation is needed.
 *
 * @param[in]  src_image_shape         The shape of the src buffer (hw shape).
 * @param[in]  src_format              The format of the src buffer (hw format).
 * @param[in]  dst_image_shape         The shape of the dst buffer (host shape).
 * @param[in]  dst_format              The format of the dst buffer (host format).
 * @param[in]  quant_info              A ::hailo_quant_info_t object containing quantization information.
 * @param[out] transformation_required Indicates whether or not a transformation is needed.
 * @return Upon success, returns @a HAILO_SUCCESS. Otherwise, returns an @a hailo_status error.
 * @note In case @a transformation_required is false, the src frame is already in the required format without any transformation.
 */
HAILORTAPI hailo_status hailo_is_output_transformation_required(
    const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format,
    const hailo_quant_info_t *quant_info, bool *transformation_required)
    DEPRECATED("hailo_is_output_transformation_required is deprecated. Please use hailo_is_output_transformation_required2 instead.");

/** @} */ // end of group_deprecated_functions_and_defines


#ifdef __cplusplus
}
#endif

#endif /* _HAILORT_H_ */
