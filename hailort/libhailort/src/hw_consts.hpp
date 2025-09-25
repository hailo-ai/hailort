/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hw_consts.hpp
 * @brief Hardware constants
 **/

#ifndef _HAILO_HW_CONSTS_HPP_
#define _HAILO_HW_CONSTS_HPP_

/** stable constants **************************************************/

/** Package constants *********************************************************/
// TODO HRT-11452 - use hw consts here instead of these defines
#define HAILO8_INBOUND_DATA_STREAM_SIZE                                     (0x00010000L)
#define HAILO8_PERIPH_PAYLOAD_MAX_VALUE                                     (0x0000FFFFL)
// Max periph bytes per buffer for hailo1x because (we use its value shifted right by 3 - according to the spec) to
// configure shmifo credit size - which in hailo15 only has a width of 10 bits
#define HAILO1X_PERIPH_BYTES_PER_BUFFER_MAX_SIZE                            (0x00002000L)
#define HAILO1X_PERIPH_PAYLOAD_MAX_VALUE                                    (0x01FFFFFFL)

#endif /* _HAILO_HW_CONSTS_HPP_ */
