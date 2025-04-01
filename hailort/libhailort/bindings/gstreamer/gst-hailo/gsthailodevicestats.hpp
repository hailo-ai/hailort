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
#ifndef _GST_HAILODEVICESTATS_HPP_
#define _GST_HAILODEVICESTATS_HPP_

#include "common.hpp"
#include "hailo/expected.hpp"

#include <memory>
#include <mutex>
#include <thread>

G_BEGIN_DECLS

#define GST_TYPE_HAILODEVICESTATS (gst_hailodevicestats_get_type())
#define GST_HAILODEVICESTATS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILODEVICESTATS,GstHailoDeviceStats))
#define GST_HAILODEVICESTATS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILODEVICESTATS,GstHailoDeviceStatsClass))
#define GST_IS_HAILODEVICESTATS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILODEVICESTATS))
#define GST_IS_HAILODEVICESTATS_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILODEVICESTATS))

class HailoDeviceStatsImpl;
struct GstHailoDeviceStats
{
    GstElement parent;
    std::unique_ptr<HailoDeviceStatsImpl> impl;
};

struct GstHailoDeviceStatsClass
{
    GstElementClass parent;
};

// Message to send hailo device stats measurement data.
// This event should be sent from gsthailodevicestats element.
struct HailoDeviceStatsMessage {
    gchar *device_id;
    float32_t temperature;
    float32_t power;

    static constexpr const gchar *name = "HailoDeviceStatsMessage";
};

class HailoDeviceStatsImpl final
{
public:
    static Expected<std::unique_ptr<HailoDeviceStatsImpl>> create(GstHailoDeviceStats *element);
    HailoDeviceStatsImpl(GstHailoDeviceStats *element);
    ~HailoDeviceStatsImpl();

    void set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
    void get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
    hailo_status start_thread();
    void join_thread();

private:
    hailo_status run_measure_loop();

    Expected<std::unique_ptr<Device>> create_device(const char *device_id, hailo_pcie_device_info_t &device_info);

    GstHailoDeviceStats *m_element;
    guint32 m_sampling_interval;
    gchar *m_device_id;
    hailo_pcie_device_info_t m_device_info;
    bool m_is_silent;
    bool m_was_configured;
    float32_t m_power_measure;
    float32_t m_avg_temp;
    std::thread m_thread;
    std::atomic_bool m_is_thread_running;
    std::unique_ptr<Device> m_device;
    std::mutex m_mutex;
};

GType gst_hailodevicestats_get_type(void);

G_END_DECLS

#endif /* _GST_HAILODEVICESTATS_HPP_ */
