/*
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
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
#ifndef _GST_HAILONET_HPP_
#define _GST_HAILONET_HPP_

#include "common.hpp"
#include "network_group_handle.hpp"
#include "hailo/expected.hpp"
#include "hailo/event.hpp"

#include <atomic>
#include <condition_variable>

G_BEGIN_DECLS

#define GST_TYPE_HAILONET (gst_hailonet_get_type())
#define GST_HAILONET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILONET,GstHailoNet))
#define GST_HAILONET_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILONET,GstHailoNetClass))
#define GST_IS_HAILONET(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILONET))
#define GST_IS_HAILONET_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILONET))

class HailoNetImpl;
struct GstHailoNet
{
    GstBin parent;
    std::unique_ptr<HailoNetImpl> impl;
};

struct GstHailoNetClass
{
    GstBinClass parent;
};

struct HailoNetProperties final
{
public:
    HailoNetProperties() : m_device_id(nullptr), m_hef_path(nullptr), m_network_name(nullptr), m_batch_size(HAILO_DEFAULT_BATCH_SIZE),
        m_is_active(false), m_device_count(0), m_vdevice_key(DEFAULT_VDEVICE_KEY), m_scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN),
        m_scheduler_timeout_ms(HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS), m_scheduler_threshold(HAILO_DEFAULT_SCHEDULER_THRESHOLD),
        m_multi_process_service(HAILO_DEFAULT_MULTI_PROCESS_SERVICE)
    {}

    HailoElemProperty<gchar*> m_device_id;
    HailoElemProperty<gchar*> m_hef_path;
    HailoElemProperty<gchar*> m_network_name; // This property can be network group name or a network name
    HailoElemProperty<guint16> m_batch_size;
    HailoElemProperty<gboolean> m_is_active;
    HailoElemProperty<guint16> m_device_count;
    HailoElemProperty<guint32> m_vdevice_key;
    HailoElemProperty<hailo_scheduling_algorithm_t> m_scheduling_algorithm;
    HailoElemProperty<guint32> m_scheduler_timeout_ms;
    HailoElemProperty<guint32> m_scheduler_threshold;
    HailoElemProperty<gboolean> m_multi_process_service;
};

class HailoNetImpl final
{
public:
    static Expected<std::unique_ptr<HailoNetImpl>> create(GstHailoNet *element);
    HailoNetImpl(GstHailoNet *element, GstElement *hailosend, GstElement *queue, GstElement *hailorecv, EventPtr was_flushed_event);
    ~HailoNetImpl();

    void set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
    void get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
    hailo_status set_hef();
    hailo_status link_elements();
    hailo_status configure_network_group();
    hailo_status activate_hailonet();
    hailo_status abort_streams();

    gboolean src_pad_event(GstEvent *event);
    GstPadProbeReturn sink_probe();
    gboolean is_active();
    hailo_status flush();
    hailo_status signal_was_flushed_event();

    hailo_status deactivate_network_group();
    HailoNetProperties &get_props() {
        return m_props;
    }

private:
    void init_ghost_sink();
    void init_ghost_src();
    Expected<std::string> get_network_group_name(const std::string &network_name);

    hailo_status clear_vstreams();

    static std::atomic_uint32_t m_hailonet_count;
    static std::mutex m_mutex;
    GstHailoNet *m_element;
    HailoNetProperties m_props;
    std::vector<hailo_format_with_name_t> m_output_formats;
    GstElement *m_hailosend;
    GstElement *m_queue;
    GstElement *m_hailorecv;
    std::unique_ptr<NetworkGroupHandle> m_net_group_handle;
    bool m_was_configured;
    bool m_has_called_activate;
    EventPtr m_was_flushed_event;
    GstBufferPool *m_pool;
};

GType gst_hailonet_get_type(void);

G_END_DECLS

#endif /* _GST_HAILONET_HPP_ */
