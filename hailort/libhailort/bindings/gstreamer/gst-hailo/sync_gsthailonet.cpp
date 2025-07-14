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
#include "sync_gsthailonet.hpp"
#include "sync_gst_hailosend.hpp"
#include "sync_gst_hailorecv.hpp"
#include "hailo_events/hailo_events.hpp"
#include "metadata/hailo_buffer_flag_meta.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"

#include <sstream>
#include <algorithm>

GST_DEBUG_CATEGORY_STATIC(gst_sync_hailonet_debug_category);
#define GST_CAT_DEFAULT gst_sync_hailonet_debug_category

constexpr std::chrono::milliseconds WAIT_FOR_FLUSH_TIMEOUT_MS(1000);

static void gst_sync_hailonet_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_sync_hailonet_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static gboolean gst_hailorecv_src_pad_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstPadProbeReturn gst_sync_hailonet_sink_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
static GstStateChangeReturn gst_sync_hailonet_change_state(GstElement *element, GstStateChange transition);
static void gst_sync_hailonet_flush_callback(GstSyncHailoNet *hailonet, gpointer data);
static void gst_sync_hailonet_inner_queue_overrun_callback(GstElement *queue, gpointer udata);
static void gst_sync_hailonet_inner_queue_underrun_callback(GstElement *queue, gpointer udata);

enum
{
    PROP_0,
    PROP_DEBUG,
    PROP_DEVICE_ID,
    PROP_HEF_PATH,
    PROP_NETWORK_NAME,
    PROP_BATCH_SIZE,
    PROP_OUTPUTS_MIN_POOL_SIZE,
    PROP_OUTPUTS_MAX_POOL_SIZE,
    PROP_IS_ACTIVE,
    PROP_DEVICE_COUNT,
    PROP_VDEVICE_KEY,
    PROP_SCHEDULING_ALGORITHM,
    PROP_SCHEDULER_TIMEOUT_MS,
    PROP_SCHEDULER_THRESHOLD,
    PROP_SCHEDULER_PRIORITY,
    PROP_MULTI_PROCESS_SERVICE,
    PROP_INPUT_FORMAT_TYPE,
    PROP_OUTPUT_FORMAT_TYPE,
    PROP_NMS_SCORE_THRESHOLD,
    PROP_NMS_IOU_THRESHOLD,
    PROP_NMS_MAX_PROPOSALS_PER_CLASS,
    PROP_NMS_MAX_PROPOSALS_TOTAL
};

G_DEFINE_TYPE(GstSyncHailoNet, gst_sync_hailonet, GST_TYPE_BIN);

static void gst_sync_hailonet_dispose(GObject *object) {
    GstSyncHailoNet *self = GST_SYNC_HAILONET(object);

    assert(nullptr != self->impl);
    delete self->impl;
    self->impl = nullptr;

    G_OBJECT_CLASS(gst_sync_hailonet_parent_class)->dispose(object);
}

static void gst_sync_hailonet_class_init(GstSyncHailoNetClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->dispose = gst_sync_hailonet_dispose;

    GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

    GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));

    gst_element_class_set_static_metadata(element_class,
        "sync hailonet element", "Hailo/Network",
        "Configure and Activate Hailo Network. "
            "Supports the \"flush\" signal which blocks until there are no buffers currently processesd in the element. "
            "When deactivating a sync hailonet during runtime (via set_property of \"is-active\" to False), make sure that no frames are being pushed into the "
            "hailonet, since this operation waits until there are no frames coming in.",
        PLUGIN_AUTHOR);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_sync_hailonet_change_state);
    
    gobject_class->set_property = gst_sync_hailonet_set_property;
    gobject_class->get_property = gst_sync_hailonet_get_property;
    g_object_class_install_property(gobject_class, PROP_DEBUG,
        g_param_spec_boolean("debug", "Debug flag", "Should print debug information", false,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
        g_param_spec_string("device-id", "Device ID", "Device ID ([<domain>]:<bus>:<device>.<func>, same as in lspci command). Excludes device-count.", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_DEVICE_COUNT,
        g_param_spec_uint("device-count", "Number of devices to use", "Number of physical devices to use. Excludes device-id.", HAILO_DEFAULT_DEVICE_COUNT,
            std::numeric_limits<uint16_t>::max(), HAILO_DEFAULT_DEVICE_COUNT, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_VDEVICE_KEY,
        g_param_spec_uint("vdevice-key",
            "Indicate whether to re-use or re-create vdevice",
            "Relevant only when 'device-count' is passed. If not passed, the created vdevice will be unique to this hailonet." \
            "if multiple hailonets share 'vdevice-key' and 'device-count', the created vdevice will be shared between those hailonets",
            MIN_VALID_VDEVICE_KEY, std::numeric_limits<uint32_t>::max(), MIN_VALID_VDEVICE_KEY, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_HEF_PATH,
        g_param_spec_string("hef-path", "HEF Path Location", "Location of the HEF file to read", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NETWORK_NAME,
        g_param_spec_string("net-name", "Network Name",
            "Configure and run this specific network. "
            "If not passed, configure and run the default network - ONLY if there is one network in the HEF!", NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_BATCH_SIZE,
        g_param_spec_uint("batch-size", "Inference Batch", "How many frame to send in one batch", MIN_GSTREAMER_BATCH_SIZE, MAX_GSTREAMER_BATCH_SIZE, HAILO_DEFAULT_BATCH_SIZE,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MIN_POOL_SIZE,
        g_param_spec_uint("outputs-min-pool-size", "Outputs Minimun Pool Size", "The minimum amount of buffers to allocate for each output layer",
            0, std::numeric_limits<uint32_t>::max(), DEFAULT_OUTPUTS_MIN_POOL_SIZE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUTS_MAX_POOL_SIZE,
        g_param_spec_uint("outputs-max-pool-size", "Outputs Maximum Pool Size",
            "The maximum amount of buffers to allocate for each output layer or 0 for unlimited", 0, std::numeric_limits<uint32_t>::max(),
            DEFAULT_OUTPUTS_MAX_POOL_SIZE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_IS_ACTIVE,
        g_param_spec_boolean("is-active", "Is Network Activated", "Controls whether this element should be active. "
            "By default, the hailonet element will not be active unless it is the only one. "
            "Setting this property in combination with 'scheduling-algorithm' different than HAILO_SCHEDULING_ALGORITHM_NONE is not supported.", false,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SCHEDULING_ALGORITHM,
        g_param_spec_enum("scheduling-algorithm", "Scheduling policy for automatic network group switching", "Controls the Model Scheduler algorithm of HailoRT. "
            "Gets values from the enum GstHailoSchedulingAlgorithms. "
            "Using Model Scheduler algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE, excludes the property 'is-active'. "
            "When using the same VDevice across multiple hailonets, all should have the same 'scheduling-algorithm'. ",
            GST_TYPE_SCHEDULING_ALGORITHM, HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_TIMEOUT_MS,
        g_param_spec_uint("scheduler-timeout-ms", "Timeout for for scheduler in ms", "The maximum time period that may pass before getting run time from the scheduler,"
            " as long as at least one send request has been sent.",
            HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS, std::numeric_limits<uint32_t>::max(), HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_THRESHOLD,
        g_param_spec_uint("scheduler-threshold", "Frames threshold for scheduler", "The minimum number of send requests required before the hailonet is considered ready to get run time from the scheduler.",
            HAILO_DEFAULT_SCHEDULER_THRESHOLD, std::numeric_limits<uint32_t>::max(), HAILO_DEFAULT_SCHEDULER_THRESHOLD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_SCHEDULER_PRIORITY,
        g_param_spec_uint("scheduler-priority", "Priority index for scheduler", "When the scheduler will choose the next hailonet to run, higher priority will be prioritized in the selection. "
            "Larger number represents higher priority",
            HAILO_SCHEDULER_PRIORITY_MIN, HAILO_SCHEDULER_PRIORITY_MAX, HAILO_SCHEDULER_PRIORITY_NORMAL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_MULTI_PROCESS_SERVICE,
        g_param_spec_boolean("multi-process-service", "Should run over HailoRT service", "Controls wether to run HailoRT over its service. "
            "To use this property, the service should be active and scheduling-algorithm should be set. Defaults to false.",
            HAILO_DEFAULT_MULTI_PROCESS_SERVICE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_INPUT_FORMAT_TYPE,
        g_param_spec_enum("input-format-type", "Input format type", "Input format type(auto, float32, uint16, uint8). Default value is auto."
            "Gets values from the enum GstHailoFormatType. ",
            GST_TYPE_HAILO_FORMAT_TYPE, HAILO_FORMAT_TYPE_AUTO,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_OUTPUT_FORMAT_TYPE,
        g_param_spec_enum("output-format-type", "Output format type", "Output format type(auto, float32, uint16, uint8). Default value is auto."
            "Gets values from the enum GstHailoFormatType. ",
            GST_TYPE_HAILO_FORMAT_TYPE, HAILO_FORMAT_TYPE_AUTO,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_SCORE_THRESHOLD,
        g_param_spec_float("nms-score-threshold", "NMS score threshold", "Threshold used for filtering out candidates. Any box with score<TH is suppressed.",
            0, 1, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_IOU_THRESHOLD,
        g_param_spec_float("nms-iou-threshold", "NMS IoU threshold", "Intersection over union overlap Threshold, used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.",
            0, 1, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(gobject_class, PROP_NMS_MAX_PROPOSALS_PER_CLASS,
        g_param_spec_uint("nms-max-proposals-per-class", "NMS max proposals per class", "Set a limit for the maximum number of boxes per class.",
            0, std::numeric_limits<uint32_t>::max(), 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    // TODO: HRT-15612 - Add support for BY_SCORE in sync hailonet
    g_object_class_install_property(gobject_class, PROP_NMS_MAX_PROPOSALS_TOTAL,
        g_param_spec_uint("nms-max-proposals-total", "NMS max proposals total", "Set a limit for the maximum number of boxes total.",
            0, std::numeric_limits<uint32_t>::max(), 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


    // See information about the "flush" signal in the element description
    g_signal_new(
        "flush",
        GST_TYPE_SYNC_HAILONET,
        G_SIGNAL_ACTION,
        0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0
    );
}

std::string create_name(std::string prefix, uint32_t id)
{
    return prefix + std::to_string(id);
}

Expected<std::unique_ptr<HailoSyncNetImpl>> HailoSyncNetImpl::create(GstSyncHailoNet *element)
{
    if (nullptr == element) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto hailosend_name = create_name("hailosend", HailoSyncNetImpl::m_sync_hailonet_count);
    GstElement *hailosend = gst_element_factory_make("hailosend", hailosend_name.c_str());
    if (nullptr == hailosend) {
        GST_ELEMENT_ERROR(element, RESOURCE, FAILED, ("Failed creating hailosend element in bin!"), (NULL));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    g_object_set(hailosend, "qos", FALSE, NULL);

    auto hailoqueue_name = create_name("hailoqueue", HailoSyncNetImpl::m_sync_hailonet_count);
    GstElement *queue = gst_element_factory_make("queue", hailoqueue_name.c_str());
    if (nullptr == queue) {
        GST_ELEMENT_ERROR(element, RESOURCE, FAILED, ("Failed creating queue element in bin!"), (NULL));
        gst_object_unref(hailosend);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    // Passing 0 disables the features here
    g_object_set(queue, "max-size-time", (guint64)0, NULL);
    g_object_set(queue, "max-size-bytes", (guint)0, NULL);
    g_signal_connect(queue, "overrun", G_CALLBACK(gst_sync_hailonet_inner_queue_overrun_callback), nullptr);
    g_signal_connect(queue, "underrun", G_CALLBACK(gst_sync_hailonet_inner_queue_underrun_callback), nullptr);

    auto hailorecv_name = create_name("hailorecv", HailoSyncNetImpl::m_sync_hailonet_count);
    GstElement *hailorecv = gst_element_factory_make("hailorecv", hailorecv_name.c_str());
    if (nullptr == hailorecv) {
        GST_ELEMENT_ERROR(element, RESOURCE, FAILED, ("Failed creating hailorecv element in bin!"), (NULL));
        gst_object_unref(hailosend);
        gst_object_unref(queue);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    g_object_set(hailorecv, "qos", FALSE, NULL);

    g_signal_connect(element, "flush", G_CALLBACK(gst_sync_hailonet_flush_callback), nullptr);

    auto was_flushed_event = Event::create_shared(Event::State::not_signalled);
    GST_CHECK_EXPECTED(was_flushed_event, element, RESOURCE, "Failed allocating memory for event!");

    auto ptr = make_unique_nothrow<HailoSyncNetImpl>(element, hailosend, queue, hailorecv, was_flushed_event.release());
    if (nullptr == ptr) {
        return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
    }

    return ptr;
}

std::atomic_uint32_t HailoSyncNetImpl::m_sync_hailonet_count(0);
std::mutex HailoSyncNetImpl::m_mutex;
HailoSyncNetImpl::HailoSyncNetImpl(GstSyncHailoNet *element, GstElement *hailosend, GstElement *queue, GstElement *hailorecv, EventPtr was_flushed_event) :
    m_element(element), m_props(), m_output_formats(), m_hailosend(hailosend), m_queue(queue), m_hailorecv(hailorecv),
    m_net_group_handle(nullptr), m_was_configured(false), m_has_called_activate(false),
    m_was_flushed_event(was_flushed_event), m_pool(nullptr)
{
    GST_DEBUG_CATEGORY_INIT(gst_sync_hailonet_debug_category, "sync hailonet", 0, "debug category for sync hailonet element");

    /* gst_bin_add_many cannot fail. I use this function because the elements are created here and does not come from the outside so,
     * gst_bin_add will not fail */
    gst_bin_add_many(GST_BIN(m_element), m_hailosend, m_queue, m_hailorecv, NULL);
    init_ghost_sink();
    init_ghost_src();

    ++m_sync_hailonet_count;
}

HailoSyncNetImpl::~HailoSyncNetImpl()
{
    if (nullptr != m_pool) {
        (void)gst_buffer_pool_set_active(m_pool, FALSE);
    }
}

void HailoSyncNetImpl::init_ghost_sink()
{
    GstPad *pad = gst_element_get_static_pad(m_hailosend, "sink");

    GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sink_template);

    GstPad *ghost_pad = gst_ghost_pad_new_from_template("sink", pad, pad_tmpl);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(GST_ELEMENT(m_element), ghost_pad);

    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, static_cast<GstPadProbeCallback>(gst_sync_hailonet_sink_probe), nullptr, nullptr);

    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

void HailoSyncNetImpl::init_ghost_src()
{
    GstPad *pad = gst_element_get_static_pad(m_hailorecv, "src");

    GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&src_template);

    GstPad *ghost_pad = gst_ghost_pad_new_from_template("src", pad, pad_tmpl);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(GST_ELEMENT(m_element), ghost_pad);

    gst_pad_set_event_function(pad, gst_hailorecv_src_pad_event);

    gst_object_unref(pad_tmpl);
    gst_object_unref(pad);
}

void HailoSyncNetImpl::set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "set_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("set_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_DEBUG:
    {
        gboolean debug = g_value_get_boolean(value);
        g_object_set(m_hailosend, "debug", debug, NULL);
        g_object_set(m_hailorecv, "debug", debug, NULL);
        break;
    }
    case PROP_DEVICE_ID:
        if (0 != m_props.m_device_count.get()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                g_value_get_string(value), m_props.m_device_count.get());
            break;
        }
        if (m_was_configured) {
            g_warning("The network was already configured so changing the device ID will not take place!");
            break;
        }
        if (nullptr != m_props.m_device_id.get()) {
            g_free(m_props.m_device_id.get());
        }
        m_props.m_device_id = g_strdup(g_value_get_string(value));
        break;
    case PROP_DEVICE_COUNT:
        if (nullptr != m_props.m_device_id.get()) {
            g_error("device-id and device-count excludes eachother. received device-id=%s, device-count=%d",
                m_props.m_device_id.get(), g_value_get_uint(value));
            break;
        }
        if (m_was_configured) {
            g_warning("The network was already configured so changing the device count will not take place!");
            break;
        }
        m_props.m_device_count = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_VDEVICE_KEY:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the vdevice key will not take place!");
            break;
        }
        m_props.m_vdevice_key = static_cast<guint32>(g_value_get_uint(value));
        break;
    case PROP_HEF_PATH:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the HEF path will not take place!");
            break;
        }
        if (nullptr != m_props.m_hef_path.get()) {
            g_free(m_props.m_hef_path.get());
        }
        m_props.m_hef_path = g_strdup(g_value_get_string(value));
        break;
    case PROP_NETWORK_NAME:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the network name will not take place!");
            break;
        }
        if (nullptr != m_props.m_network_name.get()) {
            g_free(m_props.m_network_name.get());
        }
        m_props.m_network_name = g_strdup(g_value_get_string(value));
        break;
    case PROP_BATCH_SIZE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the batch size will not take place!");
            break;
        }
        m_props.m_batch_size = static_cast<guint16>(g_value_get_uint(value));
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the outputs minimum pool size will not take place!");
            break;
        }
        g_object_set(m_hailorecv, "outputs-min-pool-size", g_value_get_uint(value), NULL);
        break;
    case PROP_OUTPUTS_MAX_POOL_SIZE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the outputs maximum pool size will not take place!");
            break;
        }
        g_object_set(m_hailorecv, "outputs-max-pool-size", g_value_get_uint(value), NULL);
        break;
    case PROP_IS_ACTIVE:
    {
        gboolean new_is_active = g_value_get_boolean(value);

        if (m_props.m_scheduling_algorithm.was_changed() && (HAILO_SCHEDULING_ALGORITHM_NONE != m_props.m_scheduling_algorithm.get())) {
            g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
            break;
        }

        if (m_has_called_activate) {
            if (m_props.m_is_active.get() && !new_is_active) {
                // Setting this to false before deactivating to signal hailosend and hailorecv to stop inferring
                m_props.m_is_active = false;
                hailo_status status = deactivate_network_group();
                if (HAILO_SUCCESS != status) {
                    g_error("Deactivating network group failed, status = %d", status);
                    return;
                }
            } else if (!m_props.m_is_active.get() && new_is_active) {
                hailo_status status = m_net_group_handle->activate_network_group();
                if (HAILO_SUCCESS != status) {
                    g_error("Failed activating network group, status = %d", status);
                    break;
                }
                m_props.m_is_active = true;
            } else {
                g_warning("Trying to change is-active property state from %d to %d", m_props.m_is_active.get(), new_is_active);
                break;
            }
        } else {
            m_props.m_is_active = new_is_active;
        }
        break;
    }
    case PROP_SCHEDULING_ALGORITHM:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the scheduling algorithm will not take place!");
            break;
        }
        if (m_props.m_is_active.was_changed() && (g_value_get_enum(value) != HAILO_SCHEDULING_ALGORITHM_NONE)) {
            g_error("scheduling-algorithm different than HAILO_SCHEDULING_ALGORITHM_NONE in combination with 'is-active' is not supported.");
            break;
        }
        m_props.m_scheduling_algorithm = static_cast<hailo_scheduling_algorithm_t>(g_value_get_enum(value));
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the scheduling timeout will not take place!");
            break;
        }
        if (m_props.m_is_active.was_changed()) {
            g_error("scheduler usage (scheduler-timeout-ms) in combination with 'is-active' is not supported.");
            break;
        }
        m_props.m_scheduler_timeout_ms = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_THRESHOLD:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the scheduling threshold will not take place!");
            break;
        }
        if (m_props.m_is_active.was_changed()) {
            g_error("scheduler usage (scheduler-threshold) in combination with 'is-active' is not supported.");
            break;
        }
        m_props.m_scheduler_threshold = g_value_get_uint(value);
        break;
    case PROP_SCHEDULER_PRIORITY:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the scheduling priority will not take place!");
            break;
        }
        if (m_props.m_is_active.was_changed()) {
            g_error("scheduler usage (scheduler-priority) in combination with 'is-active' is not supported.");
            break;
        }
        m_props.m_scheduler_priority = static_cast<guint8>(g_value_get_uint(value));
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the multi-process-service property will not take place!");
            break;
        }
        m_props.m_multi_process_service = g_value_get_boolean(value);
        break;
    case PROP_INPUT_FORMAT_TYPE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        m_props.m_input_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the format type will not take place!");
            break;
        }
        m_props.m_output_format_type = static_cast<hailo_format_type_t>(g_value_get_enum(value));
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the score threshold will not take place!");
            break;
        }
        m_props.m_nms_score_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_IOU_THRESHOLD:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the IoU threshold will not take place!");
            break;
        }
        m_props.m_nms_iou_threshold = static_cast<gfloat>(g_value_get_float(value));
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        if (m_was_configured) {
            g_warning("The network was already configured so changing the max proposals per class will not take place!");
            break;
        }
        m_props.m_nms_max_proposals_per_class = static_cast<guint32>(g_value_get_uint(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void HailoSyncNetImpl::get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_DEBUG_OBJECT(m_element, "get_property");

    if ((object == nullptr) || (value == nullptr) || (pspec == nullptr)) {
        g_error("get_property got null parameter!");
        return;
    }

    switch (property_id) {
    case PROP_DEBUG:
    {
        gboolean debug;
        g_object_get(m_hailosend, "debug", &debug, nullptr);
        g_value_set_boolean(value, debug);
        break;
    }
    case PROP_DEVICE_ID:
        g_value_set_string(value, m_props.m_device_id.get());
        break;
    case PROP_DEVICE_COUNT:
        g_value_set_uint(value, m_props.m_device_count.get());
        break;
    case PROP_VDEVICE_KEY:
        g_value_set_uint(value, m_props.m_vdevice_key.get());
        break;
    case PROP_HEF_PATH:
        g_value_set_string(value, m_props.m_hef_path.get());
        break;
    case PROP_NETWORK_NAME:
        g_value_set_string(value, m_props.m_network_name.get());
        break;
    case PROP_BATCH_SIZE:
        g_value_set_uint(value, m_props.m_batch_size.get());
        break;
    case PROP_OUTPUTS_MIN_POOL_SIZE:
    {
        guint outputs_min_pool_size;
        g_object_get(m_hailorecv, "outputs-min-pool-size", &outputs_min_pool_size, nullptr);
        g_value_set_uint(value, outputs_min_pool_size);
        break;
    }
    case PROP_OUTPUTS_MAX_POOL_SIZE:
    {
        guint outputs_max_pool_size;
        g_object_get(m_hailorecv, "outputs-max-pool-size", &outputs_max_pool_size, nullptr);
        g_value_set_uint(value, outputs_max_pool_size);
        break;
    }
    case PROP_IS_ACTIVE:
        g_value_set_boolean(value, m_props.m_is_active.get());
        break;
    case PROP_SCHEDULING_ALGORITHM:
        g_value_set_enum(value, m_props.m_scheduling_algorithm.get());
        break;
    case PROP_SCHEDULER_TIMEOUT_MS:
        g_value_set_uint(value, m_props.m_scheduler_timeout_ms.get());
        break;
    case PROP_SCHEDULER_THRESHOLD:
        g_value_set_uint(value, m_props.m_scheduler_threshold.get());
        break;
    case PROP_SCHEDULER_PRIORITY:
        g_value_set_uint(value, m_props.m_scheduler_priority.get());
        break;
    case PROP_MULTI_PROCESS_SERVICE:
        g_value_set_boolean(value, m_props.m_multi_process_service.get());
        break;
    case PROP_INPUT_FORMAT_TYPE:
        g_value_set_enum(value, m_props.m_input_format_type.get());
        break;
    case PROP_OUTPUT_FORMAT_TYPE:
        g_value_set_enum(value, m_props.m_output_format_type.get());
        break;
    case PROP_NMS_SCORE_THRESHOLD:
        g_value_set_float(value, m_props.m_nms_score_threshold.get());
        break;
    case PROP_NMS_IOU_THRESHOLD:
        g_value_set_float(value, m_props.m_nms_iou_threshold.get());
        break;
    case PROP_NMS_MAX_PROPOSALS_PER_CLASS:
        g_value_set_uint(value, m_props.m_nms_max_proposals_per_class.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

hailo_status HailoSyncNetImpl::set_hef()
{
    m_net_group_handle = make_unique_nothrow<NetworkGroupHandle>(GST_ELEMENT(m_element));
    GST_CHECK(nullptr != m_net_group_handle, HAILO_OUT_OF_HOST_MEMORY, m_element, RESOURCE, "Failed allocating memory for network handle!");

    hailo_status status = m_net_group_handle->set_hef(m_props.m_device_id.get(), m_props.m_device_count.get(),
        m_props.m_vdevice_key.get(), m_props.m_scheduling_algorithm.get(), static_cast<bool>(m_props.m_multi_process_service.get()),
        m_props.m_hef_path.get());
    if (HAILO_SUCCESS != status) {
        return status;
    }

    if (m_props.m_multi_process_service.get()) {
        GST_CHECK(m_props.m_scheduling_algorithm.get() != HAILO_SCHEDULING_ALGORITHM_NONE,
            HAILO_INVALID_OPERATION, m_element, RESOURCE, "To use multi-process-service please set scheduling-algorithm.");
    }

    if (nullptr == m_props.m_network_name.get()) {
        // TODO: HRT-4957
        GST_CHECK(m_net_group_handle->hef()->get_network_groups_names().size() == 1, HAILO_INVALID_ARGUMENT, m_element, RESOURCE,
            "Network group has to be specified when there are more than one network groups in the HEF!");
        auto network_group_name = m_net_group_handle->hef()->get_network_groups_names()[0];

        auto networks_infos = m_net_group_handle->hef()->get_network_infos(network_group_name.c_str());
        GST_CHECK_EXPECTED_AS_STATUS(networks_infos, m_element, RESOURCE, "Getting network infos from network group name was failed, status %d", networks_infos.status());
        GST_CHECK(networks_infos.value().size() == 1, HAILO_INVALID_ARGUMENT, m_element, RESOURCE,
            "Network has to be specified when there are more than one network in the network group!");

        std::string default_ng_name = HailoRTDefaults::get_network_name(network_group_name);
        m_props.m_network_name = g_strdup(default_ng_name.c_str());
    }

    auto input_vstream_infos = m_net_group_handle->hef()->get_input_vstream_infos(m_props.m_network_name.get());
    GST_CHECK_EXPECTED_AS_STATUS(input_vstream_infos, m_element, RESOURCE, "Getting input vstream infos from HEF has failed, status = %d",
        input_vstream_infos.status());

    // TODO: HRT-4095
    GST_CHECK(1 == input_vstream_infos->size(), HAILO_INVALID_OPERATION, m_element, RESOURCE, "sync hailonet element supports only HEFs with one input for now!");

    auto input_vstream_info = input_vstream_infos.value()[0];
    GST_HAILOSEND(m_hailosend)->impl->set_input_vstream_infos(input_vstream_infos.release());
    GST_HAILOSEND(m_hailosend)->impl->set_batch_size(m_props.m_batch_size.get());

    GstBufferPool *pool = gst_buffer_pool_new();
    GstStructure *config = gst_buffer_pool_get_config(pool);

    auto frame_size = HailoRTCommon::get_frame_size(input_vstream_info, input_vstream_info.format);
    gst_buffer_pool_config_set_params(config, nullptr, frame_size, 1, 1);

    gboolean result = gst_buffer_pool_set_config(pool, config);
    GST_CHECK(result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Could not set config buffer pool");

    result = gst_buffer_pool_set_active(pool, TRUE);
    GST_CHECK(result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Could not set buffer pool active");

    m_pool = pool;

    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::configure_network_group()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    g_object_set(m_queue, "max-size-buffers", MAX_BUFFER_COUNT(m_props.m_batch_size.get()), NULL);

    auto network_group_name = get_network_group_name(m_props.m_network_name.get());
    GST_CHECK_EXPECTED_AS_STATUS(network_group_name, m_element, RESOURCE, "Could not get network group name from name %s, status = %d",
        m_props.m_network_name.get(), network_group_name.status());

    hailo_status status = m_net_group_handle->configure_network_group(network_group_name->c_str(), m_props.m_scheduling_algorithm.get(), m_props.m_batch_size.get());
    if (HAILO_SUCCESS != status) {
        return status;
    }
    m_was_configured = true;

    if (m_props.m_scheduler_timeout_ms.was_changed()) {
        status = m_net_group_handle->set_scheduler_timeout(m_props.m_network_name.get(), m_props.m_scheduler_timeout_ms.get());
        GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting scheduler timeout failed, status = %d", status);
    }
    if (m_props.m_scheduler_threshold.was_changed()) {
        status = m_net_group_handle->set_scheduler_threshold(m_props.m_network_name.get(), m_props.m_scheduler_threshold.get());
        GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting scheduler threshold failed, status = %d", status);
    }
    if (m_props.m_scheduler_priority.was_changed()) {
        status = m_net_group_handle->set_scheduler_priority(m_props.m_network_name.get(), m_props.m_scheduler_priority.get());
        GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting scheduler priority failed, status = %d", status);
    }

    auto vstreams = m_net_group_handle->create_vstreams(m_props.m_network_name.get(), m_props.m_scheduling_algorithm.get(), m_output_formats,
        m_props.m_input_format_type.get(), m_props.m_output_format_type.get());
    GST_CHECK_EXPECTED_AS_STATUS(vstreams, m_element, RESOURCE, "Creating vstreams failed, status = %d", status);

    GST_HAILOSEND(m_hailosend)->impl->set_input_vstreams(std::move(vstreams->first));

    // Check that if one of the NMS params are changed, we have NMS outputs in the model
    auto has_nms_output = std::any_of(vstreams->second.begin(), vstreams->second.end(), [](const auto &vs)
    {
        return HailoRTCommon::is_nms(vs.get_info());
    });

    for (auto &out_vs : vstreams->second) {
        if (m_props.m_nms_score_threshold.was_changed()) {
            GST_CHECK(has_nms_output, HAILO_INVALID_OPERATION, m_element, RESOURCE, "NMS score threshold is set, but there is no NMS output in this model.");
            if (HailoRTCommon::is_nms(out_vs.get_info())) {
                status = out_vs.set_nms_score_threshold(m_props.m_nms_score_threshold.get());
                GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting NMS score threshold failed, status = %d", status);
            }
        }
        if (m_props.m_nms_iou_threshold.was_changed()) {
            GST_CHECK(has_nms_output, HAILO_INVALID_OPERATION, m_element, RESOURCE, "NMS IoU threshold is set, but there is no NMS output in this model.");
            if (HailoRTCommon::is_nms(out_vs.get_info())) {
                status = out_vs.set_nms_iou_threshold(m_props.m_nms_iou_threshold.get());
                GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting NMS IoU threshold failed, status = %d", status);
            }
        }
        if (m_props.m_nms_max_proposals_per_class.was_changed()) {
            GST_CHECK(has_nms_output, HAILO_INVALID_OPERATION, m_element, RESOURCE, "NMS max proposals per class is set, but there is no NMS output in this model.");
            if (HailoRTCommon::is_nms(out_vs.get_info())) {
                status = out_vs.set_nms_max_proposals_per_class(m_props.m_nms_max_proposals_per_class.get());
                GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting NMS max proposals per class failed, status = %d", status);
            }
        }
    }

    status = GST_HAILORECV(m_hailorecv)->impl->set_output_vstreams(std::move(vstreams->second), m_props.m_batch_size.get());
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Setting output vstreams failed, status = %d", status);

    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::activate_hailonet()
{
    if (HAILO_SCHEDULING_ALGORITHM_NONE != m_props.m_scheduling_algorithm.get()) {
        m_props.m_is_active = true;
        return HAILO_SUCCESS;
    }

    if ((1 == m_sync_hailonet_count) && (!m_props.m_is_active.was_changed())) {
        m_props.m_is_active = true;
    }

    if (m_props.m_is_active.get()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        hailo_status status = m_net_group_handle->activate_network_group();
        if (HAILO_SUCCESS != status) {
            return status;
        }
    }

    m_has_called_activate = true;

    return HAILO_SUCCESS;
}

Expected<std::string> HailoSyncNetImpl::get_network_group_name(const std::string &network_name)
{
    for (const auto &network_group_name : m_net_group_handle->hef()->get_network_groups_names()) {
        // Look for network_group with the given name
        if ((network_name == network_group_name) || (network_name == HailoRTDefaults::get_network_name(network_group_name))) {
            return std::string(network_group_name);
        }

        auto network_infos = m_net_group_handle->hef()->get_network_infos(network_group_name);
        GST_CHECK_EXPECTED(network_infos, m_element, RESOURCE, "Could not get network infos of group %s, status = %d", network_group_name.c_str(),
            network_infos.status());

        // Look for network with the given name
        for (const auto &network_info : network_infos.value()) {
            if (network_name == network_info.name) {
                return std::string(network_group_name);
            }
        }
    }

    GST_ELEMENT_ERROR(m_element, RESOURCE, FAILED, ("Failed to get network group name from the name %s!", network_name.c_str()), (NULL));
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status HailoSyncNetImpl::link_elements()
{
    /* Link elements here because only here we have the HEF and the Caps format */
    if (!gst_element_link_many(m_hailosend, m_queue, m_hailorecv, NULL)) {
        GST_ELEMENT_ERROR(m_element, RESOURCE, FAILED, ("Could not add link elements in bin!"), (NULL));
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::abort_streams()
{
    if (!m_props.m_is_active.get()) {
        return HAILO_SUCCESS;
    }

    auto status = GST_HAILOSEND(m_hailosend)->impl->abort_vstreams();
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed aborting input VStreams of hailosend, status = %d", status);
    status = GST_HAILORECV(m_hailorecv)->impl->abort_vstreams();
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed aborting output VStreams of hailorecv, status = %d", status);
    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::deactivate_network_group()
{
    auto was_deactivated = m_net_group_handle->remove_network_group();
    GST_CHECK_EXPECTED_AS_STATUS(was_deactivated, m_element, RESOURCE, "Failed removing network, status = %d", was_deactivated.status());

    if (was_deactivated.value()) {
        return clear_vstreams();
    }
    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::clear_vstreams()
{
    if (nullptr != GST_HAILOSEND(m_hailosend)->impl) {
        hailo_status status = GST_HAILOSEND(m_hailosend)->impl->clear_vstreams();
        GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed clearing input VStreams of hailosend, status = %d", status);
    }

    if (nullptr != GST_HAILORECV(m_hailorecv)->impl) {
        hailo_status status = GST_HAILORECV(m_hailorecv)->impl->clear_vstreams();
        GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed clearing output VStreams of hailorecv, status = %d", status);
    }

    return HAILO_SUCCESS;
}

gboolean HailoSyncNetImpl::src_pad_event(GstEvent *event)
{
    assert(nullptr != event);

    auto parsed_event = HailoSetOutputFormatEvent::parse(event);
    if (HAILO_SUCCESS != parsed_event.status()) {
        return FALSE;
    }

    m_output_formats = std::move(parsed_event->formats);
    return TRUE;
}

GstPadProbeReturn HailoSyncNetImpl::sink_probe()
{
    hailo_status status = activate_hailonet();
    GST_CHECK(HAILO_SUCCESS == status, GST_PAD_PROBE_REMOVE, m_element, RESOURCE, "Failed activating network, status = %d", status);
    return GST_PAD_PROBE_REMOVE;
}

gboolean HailoSyncNetImpl::is_active()
{
    return m_props.m_is_active.get();
}

hailo_status HailoSyncNetImpl::flush()
{
    GstBuffer *buffer = nullptr;
    GstFlowReturn flow_result = gst_buffer_pool_acquire_buffer(m_pool, &buffer, nullptr);
    GST_CHECK(GST_FLOW_OK == flow_result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Acquire buffer failed!");

    GstHailoBufferFlagMeta *buffer_meta = GST_HAILO_BUFFER_FLAG_META_ADD(buffer);
    buffer_meta->flag = BUFFER_FLAG_FLUSH;
    GST_BUFFER_TIMESTAMP(buffer) = GST_HAILOSEND(m_hailosend)->impl->last_frame_pts();

    GstPad *pad = gst_element_get_static_pad(m_hailosend, "src");
    flow_result = gst_pad_push(pad, buffer);
    GST_CHECK(GST_FLOW_OK == flow_result, HAILO_INTERNAL_FAILURE, m_element, RESOURCE, "Pushing buffer to queue has failed!");

    hailo_status status = m_was_flushed_event->wait(WAIT_FOR_FLUSH_TIMEOUT_MS);
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed waiting for flushed event, status = %d", status);

    status = m_was_flushed_event->reset();
    GST_CHECK_SUCCESS(status, m_element, RESOURCE, "Failed resetting flushed event, status = %d", status);

    return HAILO_SUCCESS;
}

hailo_status HailoSyncNetImpl::signal_was_flushed_event()
{
    return m_was_flushed_event->signal();
}

static void gst_sync_hailonet_init(GstSyncHailoNet *self)
{
    if (!do_versions_match(GST_ELEMENT(self))) {
        return;
    }

    auto sync_hailonet_impl = HailoSyncNetImpl::create(self);
    if (!sync_hailonet_impl) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Creating sync hailonet implementation has failed! status = %d", sync_hailonet_impl.status()), (NULL));
        return;
    }

    self->impl = sync_hailonet_impl->release();
}

static void gst_sync_hailonet_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GST_SYNC_HAILONET(object)->impl->set_property(object, property_id, value, pspec);
}

static void gst_sync_hailonet_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GST_SYNC_HAILONET(object)->impl->get_property(object, property_id, value, pspec);
}

static gboolean gst_hailorecv_src_pad_event(GstPad */*pad*/, GstObject *parent, GstEvent *event)
{
    gboolean result = GST_SYNC_HAILONET(GST_ELEMENT_PARENT(parent))->impl->src_pad_event(event);
    if (result) {
        return TRUE;
    }

    GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST(parent);
    return GST_BASE_TRANSFORM_GET_CLASS(trans)->src_event(trans, event);
}

static GstPadProbeReturn gst_sync_hailonet_sink_probe(GstPad *pad, GstPadProbeInfo */*info*/, gpointer /*user_data*/)
{
    return GST_SYNC_HAILONET(GST_ELEMENT_PARENT(gst_pad_get_parent(pad)))->impl->sink_probe();
}

static GstStateChangeReturn gst_sync_hailonet_change_state(GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_sync_hailonet_parent_class)->change_state(element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret) {
        return ret;
    }

    auto &sync_hailonet = GST_SYNC_HAILONET(element)->impl;
    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        hailo_status status = sync_hailonet->link_elements();
        GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, RESOURCE, "Linking elements has failed, status = %d\n", status);
        break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        hailo_status status = sync_hailonet->configure_network_group();
        GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, RESOURCE, "Configuring network group failed, status = %d\n", status);
        break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
        hailo_status status = sync_hailonet->abort_streams();
        GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, RESOURCE, "Aborting streams has failed, status = %d\n", status);
        break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
        if (HAILO_SCHEDULING_ALGORITHM_NONE == sync_hailonet->get_props().m_scheduling_algorithm.get()) {
            auto status = sync_hailonet->deactivate_network_group();
            GST_CHECK(HAILO_SUCCESS == status, GST_STATE_CHANGE_FAILURE, element, RESOURCE, "Deactivating network group failed, status = %d\n", status);
        }

        break;
    }
    default:
        break;
    }

    return ret;
}

static void gst_sync_hailonet_flush_callback(GstSyncHailoNet *sync_hailonet, gpointer /*data*/)
{
    (void)sync_hailonet->impl->flush();
}

static void gst_sync_hailonet_inner_queue_overrun_callback(GstElement *queue, gpointer /*udata*/)
{
    if (GST_SYNC_HAILONET(GST_ELEMENT_PARENT(queue))->impl->is_active()) {
        GST_INFO("Inner queue of %s is overrun!", GST_ELEMENT_NAME(GST_ELEMENT_PARENT(queue)));
    }
}

static void gst_sync_hailonet_inner_queue_underrun_callback(GstElement *queue, gpointer /*udata*/)
{
    if (GST_SYNC_HAILONET(GST_ELEMENT_PARENT(queue))->impl->is_active()) {
        GST_INFO("Inner queue of %s is underrun!", GST_ELEMENT_NAME(GST_ELEMENT_PARENT(queue)));
    }
}