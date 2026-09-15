include_guard(GLOBAL)

set(HAILO_USB_TARGET_FW_VERSION "1.12.1")
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/../hailort/common/usb_loader_protocol.h.in
    ${CMAKE_BINARY_DIR}/generated/loader_protocol/usb_loader_protocol.h
    @ONLY
)
