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
#ifndef _GST_HAILONET_HPP_
#define _GST_HAILONET_HPP_

#include "include/hailo_gst.h"

#include <gst/base/gstqueuearray.h>
#include <gst/video/gstvideofilter.h>

#include "hailo/infer_model.hpp"
#include "common.hpp"
#include "gsthailo_allocator.hpp"
#include "dma_buf_allocator_wrapper.hpp"

#if defined(__linux__)
  #include "gsthailo_dmabuf_allocator.hpp"
#endif /* __linux__ */

#include <queue>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace hailort;

G_BEGIN_DECLS

#define MIN_OUTPUTS_POOL_SIZE (MAX_GSTREAMER_BATCH_SIZE)
#define MAX_OUTPUTS_POOL_SIZE (MAX_GSTREAMER_BATCH_SIZE * 4)

struct HailoNetProperties final
{
public:
    HailoNetProperties() : m_hef_path(""), m_batch_size(HAILO_DEFAULT_BATCH_SIZE),
        m_device_id(""), m_device_count(0), m_vdevice_group_id(""), m_is_active(false), m_pass_through(false),
        m_outputs_min_pool_size(MIN_OUTPUTS_POOL_SIZE), m_outputs_max_pool_size(MAX_OUTPUTS_POOL_SIZE),
        m_scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN), m_scheduler_timeout_ms(HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS),
        m_scheduler_threshold(HAILO_DEFAULT_SCHEDULER_THRESHOLD), m_scheduler_priority(HAILO_SCHEDULER_PRIORITY_NORMAL),
        m_input_format_type(HAILO_FORMAT_TYPE_AUTO), m_output_format_type(HAILO_FORMAT_TYPE_AUTO),
        m_nms_score_threshold(0), m_nms_iou_threshold(0), m_nms_max_proposals_per_class(0), m_nms_max_proposals_total(0),
        m_input_from_meta(false), m_no_transform(false), m_multi_process_service(HAILO_DEFAULT_MULTI_PROCESS_SERVICE),
        m_should_force_writable(false)
    {}

    HailoElemStringProperty m_hef_path;
    HailoElemProperty<guint16> m_batch_size;
    HailoElemStringProperty m_device_id;
    HailoElemProperty<guint16> m_device_count;
    HailoElemStringProperty m_vdevice_group_id;
    HailoElemProperty<gboolean> m_is_active;
    HailoElemProperty<gboolean> m_pass_through;
    HailoElemProperty<guint> m_outputs_min_pool_size;
    HailoElemProperty<guint> m_outputs_max_pool_size;
    HailoElemProperty<hailo_scheduling_algorithm_t> m_scheduling_algorithm;
    HailoElemProperty<guint32> m_scheduler_timeout_ms;
    HailoElemProperty<guint32> m_scheduler_threshold;
    HailoElemProperty<guint8> m_scheduler_priority;
    HailoElemProperty<hailo_format_type_t> m_input_format_type;
    HailoElemProperty<hailo_format_type_t> m_output_format_type;
    HailoElemProperty<gfloat> m_nms_score_threshold;
    HailoElemProperty<gfloat> m_nms_iou_threshold;
    HailoElemProperty<guint32> m_nms_max_proposals_per_class;
    HailoElemProperty<guint32> m_nms_max_proposals_total;
    HailoElemProperty<gboolean> m_input_from_meta;
    HailoElemProperty<gboolean> m_no_transform;
    HailoElemProperty<gboolean> m_multi_process_service;
    HailoElemProperty<gboolean> m_should_force_writable;
};

class HailoNetImpl;
typedef struct _GstHailoNet {
    GstElement element;
    GstPad *sinkpad;
    GstPad *srcpad;

    HailoNetImpl *impl;
} GstHailoNet;

class HailoNetImpl final
{
public:
    static Expected<std::unique_ptr<HailoNetImpl>> create();
    HailoNetImpl();

    std::unordered_map<GstBuffer*, std::queue<GstEvent*>> events_queue_per_buffer;
    std::queue<GstEvent*> curr_event_queue;
    GstQueueArray *input_queue;

    GstQueueArray *thread_queue;
    std::atomic_uint32_t buffers_in_thread_queue;
    std::thread thread;
    HailoNetProperties props;
    GstCaps *input_caps;
    std::atomic_bool is_thread_running;
    std::atomic_bool has_pending_eos;
    std::atomic_bool has_sent_eos;
    std::mutex sink_probe_change_state_mutex;
    bool did_critical_failure_happen;

    std::unique_ptr<VDevice> vdevice;
    std::shared_ptr<InferModel> infer_model;
    std::shared_ptr<ConfiguredInferModel> configured_infer_model;
    ConfiguredInferModel::Bindings infer_bindings;
    bool is_configured;
    std::mutex infer_mutex;

    bool has_called_activate;
    std::atomic_uint32_t ongoing_frames;
    std::condition_variable flush_cv;
    std::mutex flush_mutex;
    std::mutex input_caps_mutex;

    GstVideoInfo input_frame_info;

    GstHailoAllocator *allocator;
    std::shared_ptr<HailoDmaBuffAllocator> dmabuf_allocator;
    std::unordered_map<std::string, GstBufferPool*> output_buffer_pools;
    std::unordered_map<std::string, hailo_vstream_info_t> output_vstream_infos;

    std::mutex input_queue_mutex;
    std::mutex thread_queue_mutex;
    std::condition_variable thread_cv;
};

typedef struct _GstHailoNetClass {
  GstElementClass parent_class;
} GstHailoNetClass;

struct TensorInfo {
    GstBuffer *buffer;
    GstMapInfo buffer_info;
};

#define GST_TYPE_HAILONET (gst_hailonet_get_type())
#define GST_HAILONET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILONET,GstHailoNet))
#define GST_HAILONET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILONET,GstHailoNetClass))
#define GST_IS_HAILONET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILONET))
#define GST_IS_HAILONET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILONET))

GType gst_hailonet_get_type (void);

G_END_DECLS

#endif /* _GST_HAILONET_HPP_ */