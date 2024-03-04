/*
 * Copyright (c) 2021-2023 Hailo Technologies Ltd. All rights reserved.
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
#ifndef _GST_HAILONET2_HPP_
#define _GST_HAILONET2_HPP_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <gst/gst.h>
#pragma GCC diagnostic pop

#include <gst/base/gstqueuearray.h>
#include <gst/video/gstvideofilter.h>

#include "hailo/infer_model.hpp"
#include "common.hpp"

#include <queue>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace hailort;

G_BEGIN_DECLS

#define GST_TYPE_HAILO_ALLOCATOR (gst_hailo_allocator_get_type())
#define GST_HAILO_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HAILO_ALLOCATOR, GstHailoAllocator))
#define GST_HAILO_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HAILO_ALLOCATOR, GstHailoAllocatorClass))
#define GST_IS_HAILO_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HAILO_ALLOCATOR))
#define GST_IS_HAILO_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HAILO_ALLOCATOR))

#define MIN_OUTPUTS_POOL_SIZE (MAX_GSTREAMER_BATCH_SIZE)
#define MAX_OUTPUTS_POOL_SIZE (MAX_GSTREAMER_BATCH_SIZE * 4)

struct GstHailoAllocator
{
    GstAllocator parent;
    std::unordered_map<GstMemory*, Buffer> buffers;
};

struct GstHailoAllocatorClass
{
    GstAllocatorClass parent;
};

GType gst_hailo_allocator_get_type(void);

struct HailoNet2Properties final
{
public:
    HailoNet2Properties() : m_hef_path(nullptr), m_batch_size(HAILO_DEFAULT_BATCH_SIZE),
        m_device_id(nullptr), m_device_count(0), m_vdevice_group_id(nullptr), m_is_active(false), m_pass_through(false),
        m_outputs_min_pool_size(MIN_OUTPUTS_POOL_SIZE), m_outputs_max_pool_size(MAX_OUTPUTS_POOL_SIZE),
        m_scheduling_algorithm(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN), m_scheduler_timeout_ms(HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS),
        m_scheduler_threshold(HAILO_DEFAULT_SCHEDULER_THRESHOLD), m_scheduler_priority(HAILO_SCHEDULER_PRIORITY_NORMAL),
        m_input_format_type(HAILO_FORMAT_TYPE_AUTO), m_output_format_type(HAILO_FORMAT_TYPE_AUTO),
        m_nms_score_threshold(0), m_nms_iou_threshold(0), m_nms_max_proposals_per_class(0), m_input_from_meta(false),
        m_no_transform(false), m_multi_process_service(HAILO_DEFAULT_MULTI_PROCESS_SERVICE),
        m_vdevice_key(DEFAULT_VDEVICE_KEY)
    {}

    void free_strings()
    {
      if (m_hef_path.was_changed()) {
        g_free(m_hef_path.get());
      }
      if (m_device_id.was_changed()) {
        g_free(m_device_id.get());
      }
      if (m_vdevice_group_id.was_changed()) {
        g_free(m_vdevice_group_id.get());
      }
    }

    HailoElemProperty<gchar*> m_hef_path;
    HailoElemProperty<guint16> m_batch_size;
    HailoElemProperty<gchar*> m_device_id;
    HailoElemProperty<guint16> m_device_count;
    HailoElemProperty<gchar*> m_vdevice_group_id;
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
    HailoElemProperty<gboolean> m_input_from_meta;
    HailoElemProperty<gboolean> m_no_transform;
    HailoElemProperty<gboolean> m_multi_process_service;

    // Deprecated
    HailoElemProperty<guint32> m_vdevice_key;
};

typedef struct _GstHailoNet2 {
  GstElement element;
  GstPad *sinkpad;
  GstPad *srcpad;
  GstQueueArray *input_queue;
  GstQueueArray *thread_queue;
  std::atomic_uint32_t buffers_in_thread_queue;
  std::thread thread;
  HailoNet2Properties props;
  GstCaps *input_caps;
  std::atomic_bool is_thread_running;
  std::atomic_bool has_got_eos;

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

  GstVideoInfo input_frame_info;

  GstHailoAllocator *allocator;
  std::unordered_map<std::string, GstBufferPool*> output_buffer_pools;
  std::unordered_map<std::string, hailo_vstream_info_t> output_vstream_infos;

  std::mutex input_queue_mutex;
  std::mutex thread_queue_mutex;
  std::condition_variable thread_cv;
} GstHailoNet2;

typedef struct _GstHailoNet2Class {
  GstElementClass parent_class;
} GstHailoNet2Class;

#define GST_TYPE_HAILONET2 (gst_hailonet2_get_type())
#define GST_HAILONET2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HAILONET2,GstHailoNet2))
#define GST_HAILONET2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HAILONET2,GstHailoNet2Class))
#define GST_IS_HAILONET2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HAILONET2))
#define GST_IS_HAILONET2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HAILONET2))

GType gst_hailonet2_get_type (void);

G_END_DECLS

#endif /* _GST_HAILONET2_HPP_ */