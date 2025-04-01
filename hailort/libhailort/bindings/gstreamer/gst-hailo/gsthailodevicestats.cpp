/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include "gsthailodevicestats.hpp"
#include "hailo_events/hailo_events.hpp"

#include <limits>

GST_DEBUG_CATEGORY_STATIC(gst_hailodevicestats_debug_category);
#define GST_CAT_DEFAULT gst_hailodevicestats_debug_category
#define DEFAULT_SAMPLING_INTERVAL_SECONDS (1)

/* Amount of time between each power measurement interval.
    The default values for provides by the sensor a new value every:
    2 * sampling_period (1.1) * averaging_factor (256) [ms].
    Therefore we want it to be the period of time that the core will sleep between samples,
    plus a factor of 20 percent */

static void gst_hailodevicestats_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailodevicestats_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_hailodevicestats_change_state(GstElement *element, GstStateChange transition);
static void gst_hailodevicestats_finalize(GObject *object);

enum
{
    PROP_0,
    PROP_SAMPLING_INTERVAL,
    PROP_DEVICE_ID,
    PROP_SILENT,
    PROP_POWER_MEASUREMENT,
    PROP_TEMPERATURE
};

G_DEFINE_TYPE(GstHailoDeviceStats, gst_hailodevicestats, GST_TYPE_ELEMENT);

static void gst_hailodevicestats_class_init(GstHailoDeviceStatsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_hailodevicestats_set_property;
    gobject_class->get_property = gst_hailodevicestats_get_property;
    g_object_class_install_property(gobject_class, PROP_SAMPLING_INTERVAL,
        g_param_spec_uint("interval", "Sampling Interval", "Time period between samples, in seconds",
            0, std::numeric_limits<uint32_t>::max(), DEFAULT_SAMPLING_INTERVAL_SECONDS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
        g_param_spec_string("device-id", "Device ID", "Device ID ([<domain>]:<bus>:<device>.<func>, same as in lspci command)", nullptr,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SILENT,
        g_param_spec_boolean("silent", "Silent flag", "Should print statistics", false,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_POWER_MEASUREMENT,
        g_param_spec_float("power-measurement", "Power Measurement", "Current power measurement of device",
            0.0f, std::numeric_limits<float>::max(), 0.0f, (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_TEMPERATURE,
        g_param_spec_float("temperature", "Temperature", "Current temperature of device",
            0.0f, std::numeric_limits<float>::max(), 0.0f, (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gobject_class->finalize = gst_hailodevicestats_finalize;

    gst_element_class_set_static_metadata(element_class, "hailodevicestats element", "Hailo/Device", "Log Hailo8 device statistics", PLUGIN_AUTHOR);
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_hailodevicestats_change_state);
}

Expected<std::unique_ptr<HailoDeviceStatsImpl>> HailoDeviceStatsImpl::create(GstHailoDeviceStats *element)
{
    if (nullptr == element) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto ptr = make_unique_nothrow<HailoDeviceStatsImpl>(element);
    GST_CHECK(nullptr != ptr, make_unexpected(HAILO_OUT_OF_HOST_MEMORY), element, RESOURCE, "Could not create HailoDeviceStats implementation!");

    return ptr;
}

HailoDeviceStatsImpl::HailoDeviceStatsImpl(GstHailoDeviceStats *element) : m_element(element), m_sampling_interval(DEFAULT_SAMPLING_INTERVAL_SECONDS),
    m_device_id(nullptr), m_device_info(), m_is_silent(false), m_was_configured(false), m_power_measure(0.0f), m_avg_temp(0.0f)
{
    GST_DEBUG_CATEGORY_INIT(gst_hailodevicestats_debug_category, "hailodevicestats", 0, "debug category for hailodevicestats element");
}

HailoDeviceStatsImpl::~HailoDeviceStatsImpl()
{
    if (nullptr != m_device_id) {
        g_free(m_device_id);
    }

    m_is_thread_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void HailoDeviceStatsImpl::set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "set_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("set_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_SAMPLING_INTERVAL:
        m_sampling_interval = g_value_get_uint(value);
        break;
    case PROP_DEVICE_ID:
        if (m_was_configured) {
            g_warning("The device was already configured so changing the device ID will not take place!");
            break;
        }
        if (nullptr != m_device_id) {
            g_free(m_device_id);
        }
        m_device_id = g_strdup(g_value_get_string(value));
        break;
    case PROP_SILENT:
        m_is_silent = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void HailoDeviceStatsImpl::get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "get_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("get_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_SAMPLING_INTERVAL:
        g_value_set_uint(value, m_sampling_interval);
        break;
    case PROP_DEVICE_ID:
        g_value_set_string(value, m_device_id);
        break;
    case PROP_SILENT:
        g_value_set_boolean(value, m_is_silent);
        break;
    case PROP_POWER_MEASUREMENT:
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            g_value_set_float(value, m_power_measure);
        }
        break;
    case PROP_TEMPERATURE:
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            g_value_set_float(value, m_avg_temp);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

Expected<std::unique_ptr<Device>> HailoDeviceStatsImpl::create_device(const char *device_id, hailo_pcie_device_info_t &device_info)
{
    if (nullptr == device_id) {
        auto scan_result = Device::scan_pcie();
        GST_CHECK_EXPECTED(scan_result, m_element, RESOURCE, "Failed scanning pcie devices, status = %d", scan_result.status());
        GST_CHECK(scan_result->size() == 1, make_unexpected(HAILO_INVALID_OPERATION), m_element, RESOURCE, "Expected only 1 PCIe device");
        device_info = scan_result->at(0);
    } else {
        std::string device_bdf = device_id;
        auto device_info_expected = Device::parse_pcie_device_info(device_bdf);
        GST_CHECK_EXPECTED(device_info_expected, m_element, RESOURCE, "Failed parsing pcie device info, status = %d", device_info_expected.status());
        device_info = device_info_expected.value();
    }

    auto device = Device::create_pcie(device_info);
    GST_CHECK_EXPECTED(device, m_element, RESOURCE, "Failed creating device, status = %d", device.status());

    return device.release();
}

hailo_status HailoDeviceStatsImpl::start_thread()
{
    auto device = create_device(m_device_id, m_device_info);
    GST_CHECK_EXPECTED_AS_STATUS(device, m_element, RESOURCE, "Creating device failed, status = %d", device.status());

    m_device = device.release();
    m_is_thread_running = true;
    m_thread = std::thread([this] () {
        (void)run_measure_loop();
    });

    return HAILO_SUCCESS;
}

void HailoDeviceStatsImpl::join_thread()
{
    m_is_thread_running = false;
    m_thread.join();
}

hailo_status HailoDeviceStatsImpl::run_measure_loop()
{
    // Checking temperature sensor before starting thread
    auto initial_temp_info = m_device->get_chip_temperature();
    GST_CHECK_EXPECTED_AS_STATUS(initial_temp_info, m_element, RESOURCE, "Getting chip temperature failed, status = %d", initial_temp_info.status());

    hailo_status status = m_device->stop_power_measurement();
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Stopping power measurement failed, status = %d", status);

    status = m_device->set_power_measurement(HAILO_MEASUREMENT_BUFFER_INDEX_0, HAILO_DVM_OPTIONS_AUTO, HAILO_POWER_MEASUREMENT_TYPES__AUTO);
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting power measurement parameters failed, status = %d", status);

    status = m_device->start_power_measurement(HAILO_DEFAULT_INIT_AVERAGING_FACTOR, HAILO_DEFAULT_INIT_SAMPLING_PERIOD_US);
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Starting power measurement failed, status = %d", status);

    auto device_string = Device::pcie_device_info_to_string(m_device_info);
    GST_CHECK_EXPECTED_AS_STATUS(device_string, m_element, RESOURCE, "Getting PCIe device ID string has failed, status = %d", device_string.status());
    const char *device_raw_string = device_string->c_str();

    while (m_is_thread_running.load()) {
        auto measurement = m_device->get_power_measurement(HAILO_MEASUREMENT_BUFFER_INDEX_0, true);
        GST_CHECK_EXPECTED_AS_STATUS(measurement, m_element, RESOURCE, "Getting power measurement failed, status = %d", measurement.status());

        if (!m_is_silent) {
            GST_DEBUG("[%s] Power measurement: %f", device_raw_string, measurement->average_value);
        }

        auto temp_info = m_device->get_chip_temperature();
        GST_CHECK_EXPECTED_AS_STATUS(temp_info, m_element, RESOURCE, "Temperature measurement failed, status = %d", temp_info.status());

        float32_t ts_avg = ((temp_info->ts0_temperature + temp_info->ts1_temperature) / 2);
        if (!m_is_silent) {
            GST_DEBUG("[%s] Temperature = %f", device_raw_string, ts_avg);
        }

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_power_measure = measurement->average_value;
            m_avg_temp = ts_avg;
        }

        GstStructure *str = gst_structure_new(HailoDeviceStatsMessage::name,
                                              "device_id", G_TYPE_STRING, device_raw_string,
                                              "temperature", G_TYPE_FLOAT, m_avg_temp,
                                              "power", G_TYPE_FLOAT, m_power_measure,
                                              NULL);
        GstMessage *msg = gst_message_new_custom(GST_MESSAGE_ELEMENT, GST_OBJECT(GST_ELEMENT_PARENT(m_element)), str);
        gst_element_post_message(GST_ELEMENT_PARENT(m_element), msg);

        std::this_thread::sleep_for(std::chrono::seconds(m_sampling_interval));
    }

    status = m_device->stop_power_measurement();
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Stopping power measurement failed, status = %d", status);

    return HAILO_SUCCESS;
}

static void gst_hailodevicestats_init(GstHailoDeviceStats *self)
{
    auto hailodevicestats_impl = HailoDeviceStatsImpl::create(self);
    if (!hailodevicestats_impl) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Creating hailodevicestats implementation has failed! status = %d", hailodevicestats_impl.status()), (NULL));
        return;
    }

    self->impl = hailodevicestats_impl.release();
}

static void gst_hailodevicestats_finalize(GObject *object)
{
    GST_HAILODEVICESTATS(object)->impl.reset();
    G_OBJECT_CLASS(gst_hailodevicestats_parent_class)->finalize(object);
}

void gst_hailodevicestats_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_HAILODEVICESTATS(object)->impl->set_property(object, property_id, value, pspec);
}

void gst_hailodevicestats_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_HAILODEVICESTATS(object)->impl->get_property(object, property_id, value, pspec);
}

static GstStateChangeReturn gst_hailodevicestats_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_hailodevicestats_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        hailo_status status = GST_HAILODEVICESTATS(element)->impl->start_thread();
        if (HAILO_SUCCESS != status) {
            g_critical("start hailodevicestats thread failed, status = %d", status);
        }
        break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        GST_HAILODEVICESTATS(element)->impl->join_thread();
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        // Cleanup all of hailodevicestats memory
        GST_HAILODEVICESTATS(element)->impl.reset();
        break;
    default:
        break;
    }

    return ret;
}