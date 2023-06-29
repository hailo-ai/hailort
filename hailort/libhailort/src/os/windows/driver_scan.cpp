/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file driver_scan.cpp
 * @brief Get list and parse pcie driver info
 */

#include "os/windows/osdep.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/os/windows/string_conversion.hpp"
#include "os/driver_scan.hpp"
#include "../../../../drivers/win/include/Public.h"

namespace hailort
{

class CDeviceProperty {
public:
    CDeviceProperty() {}
    ~CDeviceProperty()
    {
        Drop();
    }
    bool IsValid() const
    {
        return m_Buffer != NULL;
    }
    void MoveTo(CDeviceProperty& other)
    {
        other.Drop();
        other.m_Size = m_Size;
        other.m_Buffer = m_Buffer;
        other.m_String = m_String;
        other.m_Type = m_Type;
        other.m_Value = m_Value;
        m_Buffer = NULL;
    }
    bool Number(uint32_t& Value) const {
        Value = m_Value;
        return m_Type == DEVPROP_TYPE_UINT32 && IsValid();
    }
protected:
    PBYTE m_Buffer = NULL;
    ULONG m_Size = 0;
    DEVPROPTYPE m_Type = DEVPROP_TYPE_EMPTY;
    std::wstring m_String;
    uint32_t m_Value = 0;
protected:
    void Drop()
    {
        if (m_Buffer) free(m_Buffer);
        m_Buffer = NULL;
        m_Size = 0;
        m_Type = DEVPROP_TYPE_EMPTY;
    }
    void PostProcess(CONFIGRET cr)
    {
        if (cr != CR_SUCCESS) {
            Drop();
        }
        if (m_Type == DEVPROP_TYPE_STRING) {
            m_String = (wchar_t *)m_Buffer;
        }
        if (m_Type == DEVPROP_TYPE_UINT32) {
            if (m_Size == sizeof(uint32_t)) {
                m_Value = *(uint32_t *)m_Buffer;
            } else {
                Drop();
            }
        }
    }
};

class CDeviceInterfaceProperty : public CDeviceProperty
{
public:
    CDeviceInterfaceProperty(LPCWSTR DevInterface, const DEVPROPKEY* Key, bool AllowRecursion = true);
};

class CDevInstProperty : public CDeviceProperty
{
public:
    CDevInstProperty(LPCWSTR DevInst, const DEVPROPKEY* Key)
    {
        DEVINST dn;
        CONFIGRET cr = CM_Locate_DevNodeW(&dn, (WCHAR *)DevInst, CM_LOCATE_DEVNODE_NORMAL);
        if (cr != CR_SUCCESS)
            return;
        // try to get the size of the property
        CM_Get_DevNode_PropertyW(dn, Key, &m_Type, NULL, &m_Size, 0);
        if (!m_Size)
            return;
        m_Buffer = (PBYTE)malloc(m_Size);
        if (!m_Buffer) {
            return;
        }
        cr = CM_Get_DevNode_PropertyW(dn, Key, &m_Type, m_Buffer, &m_Size, 0);
        PostProcess(cr);
    }
};

class CDeviceInstancePropertyOfInterface : public CDeviceInterfaceProperty
{
public:
    CDeviceInstancePropertyOfInterface(LPCWSTR DevInterface) :
        CDeviceInterfaceProperty(DevInterface, &DEVPKEY_Device_InstanceId, false)
    { }
    const std::wstring& DevInst() const { return m_String; }
};

CDeviceInterfaceProperty::CDeviceInterfaceProperty(
    LPCWSTR DevInterface,
    const DEVPROPKEY* Key,
    bool AllowRecursion)
{
    // try to get the property via device interface
    CM_Get_Device_Interface_PropertyW(DevInterface, Key, &m_Type, NULL, &m_Size, 0);
    if (!m_Size) {
        if (AllowRecursion) {
            // try to get the property via device instance
            CDeviceInstancePropertyOfInterface diProp(DevInterface);
            if (diProp.IsValid()) {
                const std::wstring& di = diProp.DevInst();
                CDevInstProperty dip(di.c_str(), Key);
                if (dip.IsValid()) {
                    dip.MoveTo(*this);
                }
            }
        }
        return;
    }
    m_Buffer = (PBYTE)malloc(m_Size);
    if (!m_Buffer)
        return;
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(
        DevInterface, Key, &m_Type, m_Buffer, &m_Size, 0);
    PostProcess(cr);
}

Expected<std::vector<std::string>> list_devices()
{
    GUID guid = GUID_DEVINTERFACE_HailoKM;

    ULONG len = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeA(
        &len,
        &guid,
        NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    CHECK_AS_EXPECTED((cr == CR_SUCCESS) && (len > 0), HAILO_PCIE_DRIVER_NOT_INSTALLED,
        "Driver interface not found error {}", cr);

    std::string names_str;
    names_str.resize(len);

    cr = CM_Get_Device_Interface_ListA(
        &guid,
        NULL,
        const_cast<char*>(names_str.c_str()),
        len,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    CHECK_AS_EXPECTED(cr == CR_SUCCESS, HAILO_PCIE_DRIVER_NOT_INSTALLED, "Can't retrieve driver interface error {}", cr);

    std::vector<std::string> names;
    for (const char *current_name = names_str.c_str(); *current_name; current_name += strlen(current_name) + 1) {
        names.emplace_back(current_name);
    }

    return names;
}

static Expected<uint32_t> parse_uint32_property(const std::wstring &dev_interface,
    const DEVPROPKEY* key)
{
    CDeviceInterfaceProperty prop(dev_interface.c_str(), key);
    uint32_t number = 0;
    if (!prop.Number(number)) {
        LOGGER__ERROR("Failed parsing prop");
        return make_unexpected(HAILO_DRIVER_FAIL);
    }
    return number;
}

#define DEVICE_ADDRESS_GET_FUNC(device_func) ((device_func) & 0xff)
#define DEVICE_ADDRESS_GET_DEV(device_func) ((device_func) >> 16)

Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name)
{
    const auto device_name_wstring = StringConverter::ansi_to_utf16(device_name);
    CHECK_EXPECTED(device_name_wstring);

    auto bus = parse_uint32_property(device_name_wstring.value(), &DEVPKEY_Device_BusNumber);
    CHECK_EXPECTED(bus);

    auto device_func = parse_uint32_property(device_name_wstring.value(), &DEVPKEY_Device_Address);
    CHECK_EXPECTED(device_func);

    HailoRTDriver::DeviceInfo device_info{};
    device_info.device_id = fmt::format("{:04X}:{:02X}:{:02X}.{}", 0, *bus, DEVICE_ADDRESS_GET_DEV(*device_func),
        DEVICE_ADDRESS_GET_FUNC(*device_func));
    device_info.dev_path = device_name;
    return device_info;
}

} /* namespace hailort */
