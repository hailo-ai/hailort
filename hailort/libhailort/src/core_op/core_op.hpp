/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file core_op.hpp
 * @brief Hence, the hierarchy is as follows:
 *  CoreOp
 *  | -- VdmaConfigCoreOp
 *  | -- VDeviceCoreOp (contains vector of VdmaConfigCoreOp, one per physical device)
 **/

#ifndef _HAILO_CORE_OP_HPP_
#define _HAILO_CORE_OP_HPP_

#include "hailo/network_group.hpp"
#include "hailo/device.hpp"

#include "common/latency_meter.hpp"

#include "hef/core_op_metadata.hpp"
#include "control_protocol.h"
#include "core_op/active_core_op_holder.hpp"
#include "stream_common/stream_internal.hpp"


namespace hailort {

namespace vdma {
    class BoundaryChannel;
    using BoundaryChannelPtr = std::shared_ptr<BoundaryChannel>;
} /* namespace vdma */


/** Represents a vector of InputStream ptrs */
using InputStreamPtrVector = std::vector<std::shared_ptr<InputStreamBase>>;

/** Represents a vector of OutputStream ptrs */
using OutputStreamPtrVector = std::vector<std::shared_ptr<OutputStreamBase>>;

class CoreOp
{
public:
    virtual ~CoreOp() = default;
    CoreOp(const CoreOp &other) = delete;
    CoreOp &operator=(const CoreOp &other) = delete;
    CoreOp &operator=(CoreOp &&other) = delete;
    CoreOp(CoreOp &&other) = default;

    std::shared_ptr<CoreOpMetadata> metadata() {
        return m_metadata;
    }

    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout);

    virtual const std::string& name() const;

    virtual bool is_scheduled() const = 0;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) = 0;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) = 0;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) = 0;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() = 0;

    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="");
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="");
    virtual InputStreamRefVector get_input_streams();
    virtual OutputStreamRefVector get_output_streams();
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface);
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface);
    virtual ExpectedRef<InputStreamBase> get_input_stream_by_name(const std::string& name);
    virtual ExpectedRef<OutputStreamBase> get_output_stream_by_name(const std::string& name);
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="");

    hailo_status activate(uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    hailo_status deactivate();

    // Shutdown the core-op, make sure all ongoing transfers are completed with status HAILO_STREAM_ABORT
    virtual hailo_status shutdown() = 0;

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE) = 0;
    virtual hailo_status deactivate_impl() = 0;

    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const;

    virtual AccumulatorPtr get_activation_time_accumulator() const;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const;
    hailo_status create_streams_from_config_params(Device &device);

    virtual bool is_multi_context() const;
    virtual const ConfigureNetworkParams get_config_params() const;
    virtual Expected<HwInferResults> run_hw_infer_estimator() = 0;

    const SupportedFeatures &get_supported_features();
    Expected<uint16_t> get_stream_batch_size(const std::string &stream_name);
    bool is_default_batch_size() const;

    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key);

    hailo_status wrap_streams_for_remote_process();

    void set_vdevice_core_op_handle(vdevice_core_op_handle_t handle) { m_vdevice_core_op_handle = handle;}
    vdevice_core_op_handle_t vdevice_core_op_handle() { return m_vdevice_core_op_handle;}

    // Amount of parallel ongoing infer requests for the core op.
    Expected<size_t> infer_queue_size() const;

    /**
     * The function returns `HAILO_SUCCESS` if at least one of the writes or reads happened.
     * This assures that all the callbacks will be called: The callbacks per transfer and the `infer_request` callback.
     *
     * If the function fails, then we can assume that no callback has being called.
     * Neither the transfers callbacks nor the `infer_request` callback.
     *
     */
    hailo_status infer_async(InferRequest &&request);

    virtual bool has_caches() const = 0;
    virtual Expected<uint32_t> get_cache_length() const = 0;
    virtual Expected<uint32_t> get_cache_read_length() const = 0;
    virtual Expected<uint32_t> get_cache_write_length() const = 0;
    virtual Expected<uint32_t> get_cache_entry_size(uint32_t cache_id) const = 0;
    virtual hailo_status init_cache(uint32_t read_offset) = 0;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) = 0;
    virtual Expected<std::vector<uint32_t>> get_cache_ids() const = 0;
    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) = 0;
    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) = 0;

    std::map<std::string, std::shared_ptr<InputStreamBase>> m_input_streams;
    std::map<std::string, std::shared_ptr<OutputStreamBase>> m_output_streams;

protected:
    CoreOp(const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata,
        ActiveCoreOpHolder &active_core_op_holder, hailo_status &status, bool is_scheduled = false);

    Expected<std::shared_ptr<OutputStreamBase>> create_output_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    Expected<std::shared_ptr<InputStreamBase>> create_input_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);

    hailo_status activate_low_level_streams();
    hailo_status deactivate_low_level_streams();
    hailo_status abort_low_level_streams();

    Expected<LayerInfo> get_layer_info(const std::string &stream_name);
    bool is_nms();

    hailo_status add_input_stream(std::shared_ptr<InputStreamBase> &&stream,
        const hailo_stream_parameters_t &stream_params);
    hailo_status add_output_stream(std::shared_ptr<OutputStreamBase> &&stream,
        const hailo_stream_parameters_t &stream_params);

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() = 0;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) = 0;
    static uint16_t get_smallest_configured_batch_size(const ConfigureNetworkParams &config_params);

private:
    struct OngoingInferState {
        std::atomic_size_t callbacks_left;
        hailo_status status;
    };

    // Launch write_async/read_async on all streams with wrapped callback.
    // We remove all transfer that was launched successfully from transfers in order to call those callback
    // with HAILO_STREAM_ABORT status on the case of a failure.
    hailo_status infer_async_impl(std::unordered_map<std::string, TransferRequest> &transfers,
        std::shared_ptr<OngoingInferState> state,
         TransferDoneCallback done_callback);
    TransferDoneCallback wrap_user_callback(TransferDoneCallback &&original_callback,
        std::shared_ptr<OngoingInferState> state,
        TransferDoneCallback infer_callback);

    const ConfigureNetworkParams m_config_params;
    ActiveCoreOpHolder &m_active_core_op_holder;
    const uint16_t m_min_configured_batch_size; // TODO: remove after HRT-6535
    EventPtr m_core_op_activated_event;
    AccumulatorPtr m_activation_time_accumulator;
    AccumulatorPtr m_deactivation_time_accumulator;
    std::shared_ptr<CoreOpMetadata> m_metadata;
    vdevice_core_op_handle_t m_vdevice_core_op_handle;

    Expected<std::shared_ptr<InputStreamBase>> create_vdma_input_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    Expected<std::shared_ptr<OutputStreamBase>> create_vdma_output_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    Expected<std::shared_ptr<InputStreamBase>> get_shared_input_stream_by_name(const std::string &stream_name);
    Expected<std::shared_ptr<OutputStreamBase>> get_shared_output_stream_by_name(const std::string &stream_name);

    friend class VDeviceCoreOp; // VDeviceCoreOp is using protected members and functions from other CoreOps objects
    friend class ConfiguredNetworkGroupBase;
};

} /* namespace hailort */

#endif /* _HAILO_CORE_OP_HPP_ */
