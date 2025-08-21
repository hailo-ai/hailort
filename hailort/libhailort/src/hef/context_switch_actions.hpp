/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file context_switch_actions.hpp
 * @brief Contains classes represents the context switch action (Actions found in the HEFs
 *        and action sent to the fw).
 **/

#ifndef _HAILO_CONTEXT_SWITCH_ACTIONS_HPP_
#define _HAILO_CONTEXT_SWITCH_ACTIONS_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"
#include "common/file_utils.hpp"

#include "vdma/channel/channel_id.hpp"
#include "hef/layer_info.hpp"

#include "device_common/control_protocol.hpp"
#include "context_switch_defs.h"

namespace hailort
{


class ResourcesManager;
class ContextResources;
struct EdgeLayer;
#pragma pack(push, 1)
typedef struct {
    uint64_t offset;
    uint32_t size;
} ccw_write_ptr_t;
#pragma pack(pop)

class ContextSwitchConfigAction;
using ContextSwitchConfigActionPtr = std::shared_ptr<ContextSwitchConfigAction>;

class ContextSwitchConfigAction
{
public:
    enum class Type
    {
        None,
        ActivateConfigChannel,
        DeactivateConfigChannel,
        WriteDataCcw,
        AddCcwBurst,
        FetchCfgChannelDescriptors,
        TriggerSequencer,
        WaitForSequencerDone,
        TriggerNewDataFromDataInput,
        TriggerNewDataFromDataInputDdr,
        EnableLcuNonDefault,
        EnableLcuDefault,
        DisableLcu,
        WaitForLcu,
        WaitForModuleConfigDone,
        DdrPairInfo,
        StartDdrBufferingTask,
        ResetDdrBufferingTask,
        AddRepeated,
        StartBurstCreditsTask,
        ResetBurstCreditsTask,
        WaitForNetworkGroupChange,
        ChangeVdmaToStreamMapping,
        WaitOutputTransferDone,
        OpenBoundaryInputChannel,
        OpenBoundaryOutputChannel,
        ActivateBoundaryInputChannel,
        ActivateBoundaryOutputChannel,
        ActivateInterContextInputChannel,
        ActivateInterContextOutputChannel,
        ActivateDdrInputChannel,
        ActivateDdrOutputChannel,
        ActivateCacheInputChannel,
        ActivateCacheOutputChannel,
        ValidateChannel,
        DeactivateChannel,
        WaitDmaIdle,
        WaitNmsIdle,
        EnableNms,
        WriteDataByType,
        SwitchLcuBatch,
        ChangeBoundaryInputBatchAction,
        PauseVdmaChannel,
        ResumeVdmaChannel,
        WaitForCacheUpdated,
        Sleep,
        Halt,
        ConfigChannelPreAllowInputDataflowAction,
        DisableDataChannelsAction,
    };

    ContextSwitchConfigAction(ContextSwitchConfigAction &&) = default;
    ContextSwitchConfigAction(const ContextSwitchConfigAction &) = delete;
    ContextSwitchConfigAction &operator=(ContextSwitchConfigAction &&) = delete;
    ContextSwitchConfigAction &operator=(const ContextSwitchConfigAction &) = delete;
    virtual ~ContextSwitchConfigAction() = default;

    // Serialize the action a vector of buffers - each buffer is a chunk that must be sent continuously to the firmware
    // (For example each chunk can be sub action of RepeatedAction).
    virtual Expected<std::vector<Buffer>> serialize(const ContextResources &context_resources) const;

    Expected<Buffer> serialize_header() const;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const = 0;

    virtual bool supports_repeated_block() const = 0;
    Type get_type() const;
    CONTEXT_SWITCH_DEFS__ACTION_TYPE_t get_action_list_type() const;

protected:
    ContextSwitchConfigAction(Type type);
    ContextSwitchConfigAction(Type type, CONTEXT_SWITCH_DEFS__ACTION_TYPE_t action_list_type);

    const Type m_type;
    const CONTEXT_SWITCH_DEFS__ACTION_TYPE_t m_action_list_type;
};

class NoneAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    NoneAction(NoneAction &&) = default;
    NoneAction(const NoneAction &) = delete;
    NoneAction &operator=(NoneAction &&) = delete;
    NoneAction &operator=(const NoneAction &) = delete;
    virtual ~NoneAction() = default;

    virtual Expected<std::vector<Buffer>> serialize(const ContextResources &context_resources) const override;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    NoneAction();
};

class ActivateConfigChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t config_stream_index, const vdma::ChannelId &channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateConfigChannelAction(uint8_t config_stream_index, const vdma::ChannelId &channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    const uint8_t m_config_stream_index;
    const vdma::ChannelId m_channel_id;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
};

class DeactivateConfigChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t config_stream_index, const vdma::ChannelId &channel_id);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    DeactivateConfigChannelAction(uint8_t config_stream_index, const vdma::ChannelId &channel_id);

    const uint8_t m_config_stream_index;
    const vdma::ChannelId m_channel_id;
};

class ConfigBuffer;
class CopiedConfigBuffer;

class WriteDataCcwAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(std::vector<ccw_write_ptr_t> &&ccw_write_ptrs, uint8_t config_stream_index,
        uint16_t total_ccw_burst, std::shared_ptr<SeekableBytesReader> hef_reader);
    WriteDataCcwAction(WriteDataCcwAction &&) = default;
    WriteDataCcwAction(const WriteDataCcwAction &) = delete;
    WriteDataCcwAction &operator=(WriteDataCcwAction &&) = delete;
    WriteDataCcwAction &operator=(const WriteDataCcwAction &) = delete;
    virtual ~WriteDataCcwAction() = default;

    virtual Expected<std::vector<Buffer>> serialize(const ContextResources &context_resources) const override;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

    virtual size_t size() const { return m_size; }
    virtual uint8_t config_stream_index() const { return m_config_stream_index; }
    virtual hailo_status write_to_config_buffer(CopiedConfigBuffer& config_buffer, bool should_support_pre_fetch);
    uint16_t total_ccw_burst() const { return m_total_ccw_burst; }

protected:
    WriteDataCcwAction(std::vector<ccw_write_ptr_t> &&ccw_write_ptrs, uint8_t config_stream_index,
        uint16_t total_ccw_burst, std::shared_ptr<SeekableBytesReader> hef_reader);

    const std::vector<ccw_write_ptr_t> m_ccw_write_ptrs;
    size_t m_size;
    const uint8_t m_config_stream_index;
    uint16_t m_total_ccw_burst;
    std::shared_ptr<SeekableBytesReader> m_hef_reader;
};

class WriteDataCcwActionByBuffer : public WriteDataCcwAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(Buffer &&data, uint8_t config_stream_index,
        size_t total_ccw_burst);
    WriteDataCcwActionByBuffer(WriteDataCcwActionByBuffer &&) = default;
    WriteDataCcwActionByBuffer(const WriteDataCcwActionByBuffer &) = delete;
    WriteDataCcwActionByBuffer &operator=(WriteDataCcwActionByBuffer &&) = delete;
    WriteDataCcwActionByBuffer &operator=(const WriteDataCcwActionByBuffer &) = delete;
    virtual ~WriteDataCcwActionByBuffer() = default;

    virtual size_t size() const override { return m_data.size(); }
    virtual hailo_status write_to_config_buffer(CopiedConfigBuffer& config_buffer, bool should_support_pre_fetch) override;

private:
    WriteDataCcwActionByBuffer(Buffer &&data, uint8_t config_stream_index,
        uint16_t total_ccw_burst);

    Buffer m_data;
};

class AddCcwBurstAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t config_stream_index, uint16_t ccw_bursts);
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    AddCcwBurstAction(uint8_t config_stream_index, uint16_t ccw_bursts);

    const uint8_t m_config_stream_index;
    const uint16_t m_ccw_bursts;
};

class FetchCfgChannelDescriptorsAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id, size_t desc_count);

    FetchCfgChannelDescriptorsAction(FetchCfgChannelDescriptorsAction &&) = default;
    FetchCfgChannelDescriptorsAction(const FetchCfgChannelDescriptorsAction &) = delete;
    FetchCfgChannelDescriptorsAction &operator=(FetchCfgChannelDescriptorsAction &&) = delete;
    FetchCfgChannelDescriptorsAction &operator=(const FetchCfgChannelDescriptorsAction &) = delete;
    virtual ~FetchCfgChannelDescriptorsAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    FetchCfgChannelDescriptorsAction(const vdma::ChannelId &channel_id, uint16_t desc_count);

    const vdma::ChannelId m_channel_id;
    const uint16_t m_desc_count;
};

class StartBurstCreditsTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();

    StartBurstCreditsTaskAction(StartBurstCreditsTaskAction &&) = default;
    StartBurstCreditsTaskAction(const StartBurstCreditsTaskAction &) = delete;
    StartBurstCreditsTaskAction &operator=(StartBurstCreditsTaskAction &&) = delete;
    StartBurstCreditsTaskAction &operator=(const StartBurstCreditsTaskAction &) = delete;
    virtual ~StartBurstCreditsTaskAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    StartBurstCreditsTaskAction();
};

class ResetBurstCreditsTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();

    ResetBurstCreditsTaskAction(ResetBurstCreditsTaskAction &&) = default;
    ResetBurstCreditsTaskAction(const ResetBurstCreditsTaskAction &) = delete;
    ResetBurstCreditsTaskAction &operator=(ResetBurstCreditsTaskAction &&) = delete;
    ResetBurstCreditsTaskAction &operator=(const ResetBurstCreditsTaskAction &) = delete;
    virtual ~ResetBurstCreditsTaskAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ResetBurstCreditsTaskAction();
};

class WaitForCacheUpdatedAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();

    WaitForCacheUpdatedAction(WaitForCacheUpdatedAction &&) = default;
    WaitForCacheUpdatedAction(const WaitForCacheUpdatedAction &) = delete;
    WaitForCacheUpdatedAction &operator=(WaitForCacheUpdatedAction &&) = delete;
    WaitForCacheUpdatedAction &operator=(const WaitForCacheUpdatedAction &) = delete;
    virtual ~WaitForCacheUpdatedAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitForCacheUpdatedAction();
};

class WaitForNetworkGroupChangeAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitForNetworkGroupChangeAction();
};

class RepeatedAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(std::vector<ContextSwitchConfigActionPtr> &&actions);
    RepeatedAction(RepeatedAction &&) = default;
    RepeatedAction(const RepeatedAction &) = delete;
    RepeatedAction &operator=(RepeatedAction &&) = delete;
    RepeatedAction &operator=(const RepeatedAction &) = delete;
    virtual ~RepeatedAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

    virtual Expected<std::vector<Buffer>> serialize(const ContextResources &context_resources) const override;

private:
    RepeatedAction(std::vector<ContextSwitchConfigActionPtr> &&actions);

    const std::vector<ContextSwitchConfigActionPtr> m_actions;
    const CONTEXT_SWITCH_DEFS__ACTION_TYPE_t m_sub_action_type;
};

class DisableLcuAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index, uint8_t lcu_index);
    DisableLcuAction(DisableLcuAction &&) = default;
    DisableLcuAction(const DisableLcuAction &) = delete;
    DisableLcuAction &operator=(DisableLcuAction &&) = delete;
    DisableLcuAction &operator=(const DisableLcuAction &) = delete;
    virtual ~DisableLcuAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    DisableLcuAction(uint8_t cluster_index, uint8_t lcu_index);

    const uint8_t m_cluster_index;
    const uint8_t m_lcu_index;
};


class WaitForLcuAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index, uint8_t lcu_index);
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitForLcuAction(uint8_t cluster_index, uint8_t lcu_index);

    uint8_t m_cluster_index;
    uint8_t m_lcu_index;
};

class EnableLcuAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index, uint8_t lcu_index,
        uint8_t network_index, uint16_t kernel_done_address, uint32_t kernel_done_count);
    EnableLcuAction(EnableLcuAction &&) = default;
    EnableLcuAction(const EnableLcuAction &) = delete;
    EnableLcuAction &operator=(EnableLcuAction &&) = delete;
    EnableLcuAction &operator=(const EnableLcuAction &) = delete;
    virtual ~EnableLcuAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    static CONTEXT_SWITCH_DEFS__ACTION_TYPE_t get_enable_lcu_action_type(bool is_default);
    static Type get_enable_lcu_type(bool is_default);

    EnableLcuAction(uint8_t cluster_index, uint8_t lcu_index,
        uint8_t network_index, uint16_t kernel_done_address, uint32_t kernel_done_count, bool is_default);

    const uint8_t m_cluster_index;
    const uint8_t m_lcu_index;
    const uint8_t m_network_index;
    const uint16_t m_kernel_done_address;
    const uint32_t m_kernel_done_count;
    const bool m_is_default;
};

class EnableSequencerAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index, uint8_t initial_l3_cut,
        uint16_t initial_l3_offset, uint32_t active_apu, uint32_t active_ia, uint64_t active_sc, uint64_t active_l2,
        uint64_t l2_offset_0, uint64_t l2_offset_1);
    EnableSequencerAction(EnableSequencerAction &&) = default;
    EnableSequencerAction(const EnableSequencerAction &) = delete;
    EnableSequencerAction &operator=(EnableSequencerAction &&) = delete;
    EnableSequencerAction &operator=(const EnableSequencerAction &) = delete;
    virtual ~EnableSequencerAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    EnableSequencerAction(uint8_t cluster_index, uint8_t initial_l3_cut, uint16_t initial_l3_offset,
        uint32_t active_apu, uint32_t active_ia, uint64_t active_sc, uint64_t active_l2, uint64_t l2_offset_0,
        uint64_t l2_offset_1);

    const uint8_t m_cluster_index;
    const uint8_t m_initial_l3_cut;
    const uint16_t m_initial_l3_offset;
    const uint32_t m_active_apu;
    const uint32_t m_active_ia;
    const uint64_t m_active_sc;
    const uint64_t m_active_l2;
    const uint64_t m_l2_offset_0;
    const uint64_t m_l2_offset_1;
};

class WaitForSequencerAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index);
    WaitForSequencerAction(WaitForSequencerAction &&) = default;
    WaitForSequencerAction(const WaitForSequencerAction &) = delete;
    WaitForSequencerAction &operator=(WaitForSequencerAction &&) = delete;
    WaitForSequencerAction &operator=(const WaitForSequencerAction &) = delete;
    virtual ~WaitForSequencerAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitForSequencerAction(uint8_t cluster_index);

    const uint8_t m_cluster_index;
};

class AllowInputDataflowAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t stream_index);
    AllowInputDataflowAction(AllowInputDataflowAction &&) = default;
    AllowInputDataflowAction(const AllowInputDataflowAction &) = delete;
    AllowInputDataflowAction &operator=(AllowInputDataflowAction &&) = delete;
    AllowInputDataflowAction &operator=(const AllowInputDataflowAction &) = delete;
    virtual ~AllowInputDataflowAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit AllowInputDataflowAction(uint8_t stream_index);

    const uint8_t m_stream_index;
};

class ChangeBoundaryInputBatchAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId channel_id);
    ChangeBoundaryInputBatchAction(ChangeBoundaryInputBatchAction &&) = default;
    ChangeBoundaryInputBatchAction(const ChangeBoundaryInputBatchAction &) = delete;
    ChangeBoundaryInputBatchAction &operator=(ChangeBoundaryInputBatchAction &&) = delete;
    ChangeBoundaryInputBatchAction &operator=(const ChangeBoundaryInputBatchAction &) = delete;
    virtual ~ChangeBoundaryInputBatchAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit ChangeBoundaryInputBatchAction(const vdma::ChannelId channel_id);

    const vdma::ChannelId m_channel_id;
};

class WaitForModuleConfigDoneAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t module_index);
    WaitForModuleConfigDoneAction(WaitForModuleConfigDoneAction &&) = default;
    WaitForModuleConfigDoneAction(const WaitForModuleConfigDoneAction &) = delete;
    WaitForModuleConfigDoneAction &operator=(WaitForModuleConfigDoneAction &&) = delete;
    WaitForModuleConfigDoneAction &operator=(const WaitForModuleConfigDoneAction &) = delete;
    virtual ~WaitForModuleConfigDoneAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitForModuleConfigDoneAction(uint8_t module_index);

    const uint8_t m_module_index;
};

class DdrPairInfoAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &h2d_channel_id,
        const vdma::ChannelId &d2h_channel_id, uint8_t network_index, uint32_t descriptors_per_frame, uint16_t descs_count);
    DdrPairInfoAction(DdrPairInfoAction &&) = default;
    DdrPairInfoAction(const DdrPairInfoAction &) = delete;
    DdrPairInfoAction &operator=(DdrPairInfoAction &&) = delete;
    DdrPairInfoAction &operator=(const DdrPairInfoAction &) = delete;
    virtual ~DdrPairInfoAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    DdrPairInfoAction(const vdma::ChannelId &h2d_channel_id, const vdma::ChannelId &d2h_channel_id,
        uint8_t network_index, uint32_t descriptors_per_frame, uint16_t descs_count);

    const vdma::ChannelId m_h2d_channel_id;
    const vdma::ChannelId m_d2h_channel_id;
    const uint8_t m_network_index;
    const uint32_t m_descriptors_per_frame;
    const uint16_t m_descs_count;
};

class StartDdrBufferingTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    StartDdrBufferingTaskAction(StartDdrBufferingTaskAction &&) = default;
    StartDdrBufferingTaskAction(const StartDdrBufferingTaskAction &) = delete;
    StartDdrBufferingTaskAction &operator=(StartDdrBufferingTaskAction &&) = delete;
    StartDdrBufferingTaskAction &operator=(const StartDdrBufferingTaskAction &) = delete;
    virtual ~StartDdrBufferingTaskAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    StartDdrBufferingTaskAction();
};

class ResetDdrBufferingTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    ResetDdrBufferingTaskAction(ResetDdrBufferingTaskAction &&) = default;
    ResetDdrBufferingTaskAction(const ResetDdrBufferingTaskAction &) = delete;
    ResetDdrBufferingTaskAction &operator=(ResetDdrBufferingTaskAction &&) = delete;
    ResetDdrBufferingTaskAction &operator=(const ResetDdrBufferingTaskAction &) = delete;
    virtual ~ResetDdrBufferingTaskAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;
private:
    ResetDdrBufferingTaskAction();
};

class ChangeVdmaToStreamMapping : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id, uint8_t stream_index,
        bool is_dummy_stream);
    ChangeVdmaToStreamMapping(ChangeVdmaToStreamMapping &&) = default;
    ChangeVdmaToStreamMapping(const ChangeVdmaToStreamMapping &) = delete;
    ChangeVdmaToStreamMapping &operator=(ChangeVdmaToStreamMapping &&) = delete;
    ChangeVdmaToStreamMapping &operator=(const ChangeVdmaToStreamMapping &) = delete;
    virtual ~ChangeVdmaToStreamMapping() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ChangeVdmaToStreamMapping(const vdma::ChannelId &channel_id, uint8_t stream_index, bool is_dummy_stream);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const bool m_is_dummy_stream;
};

class WaitOutputTransferDoneAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t stream_index);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit WaitOutputTransferDoneAction(uint8_t stream_index);

    uint8_t m_stream_index;
};

class OpenBoundaryInputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    OpenBoundaryInputChannelAction(const vdma::ChannelId channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    const vdma::ChannelId m_channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
};

class OpenBoundaryOutputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    OpenBoundaryOutputChannelAction(const vdma::ChannelId &channel_id,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    const vdma::ChannelId m_channel_id;
    CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
};

class ActivateBoundaryInputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t initial_credit_size);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateBoundaryInputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info,
        uint32_t initial_credit_size);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    const uint32_t m_initial_credit_size;
};

class ActivateBoundaryOutputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateBoundaryOutputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const uint8_t m_network_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
};

class ActivateInterContextInputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t initial_credit_size);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateInterContextInputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info,
        uint32_t initial_credit_size);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    const uint32_t m_initial_credit_size;
};

class ActivateInterContextOutputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id, uint8_t stream_index,
        uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateInterContextOutputChannelAction(const vdma::ChannelId &channel_id, uint8_t stream_index,
        uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const uint8_t m_network_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
};

class ActivateDdrInputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t initial_credit_size,
        const vdma::ChannelId &connected_d2h_channel_id);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateDdrInputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t initial_credit_size,
        const vdma::ChannelId &connected_d2h_channel_id);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    const uint32_t m_initial_credit_size;
    const vdma::ChannelId m_connected_d2h_channel_id;
};

class ActivateDdrOutputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t buffered_rows_count);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateDdrOutputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t buffered_rows_count);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    const uint32_t m_buffered_rows_count;
};

class ActivateCacheInputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint32_t initial_credit_size);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateCacheInputChannelAction(const vdma::ChannelId &channel_id,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info,
        uint32_t initial_credit_size);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    const uint32_t m_initial_credit_size;
};

class ActivateCacheOutputChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &channel_id, uint8_t stream_index,
        uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint16_t batch_size);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ActivateCacheOutputChannelAction(const vdma::ChannelId &channel_id, uint8_t stream_index,
        uint8_t network_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const CONTROL_PROTOCOL__host_buffer_info_t &host_buffer_info, uint16_t batch_size);

    const vdma::ChannelId m_channel_id;
    const uint8_t m_stream_index;
    const uint8_t m_network_index;
    const CONTROL_PROTOCOL__nn_stream_config_t m_nn_stream_config;
    const CONTROL_PROTOCOL__host_buffer_info_t m_host_buffer_info;
    uint16_t m_batch_size;
};

class ValidateChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const EdgeLayer &edge_layer,
        const bool is_batch_switch_context);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ValidateChannelAction(const vdma::ChannelId &channel_id, hailo_stream_direction_t stream_direction,
        bool check_host_empty_num_available, CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t host_buffer_type, uint32_t initial_credit_size);

    const vdma::ChannelId m_channel_id;
    const hailo_stream_direction_t m_stream_direction;
    const bool m_check_host_empty_num_available;
    const CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t m_host_buffer_type;
    const uint32_t m_initial_credit_size;
};

class DeactivateChannelAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const EdgeLayer &edge_layer, const bool is_batch_switch_context);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    DeactivateChannelAction(const vdma::ChannelId &channel_id, hailo_stream_direction_t stream_direction,
        bool check_host_empty_num_available, CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t host_buffer_type, uint32_t initial_credit_size);

    const vdma::ChannelId m_channel_id;
    const hailo_stream_direction_t m_stream_direction;
    const bool m_check_host_empty_num_available;
    const CONTROL_PROTOCOL__HOST_BUFFER_TYPE_t m_host_buffer_type;
    const uint32_t m_initial_credit_size;
};

class PauseVdmaChannel : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const EdgeLayer &edge_layer);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    PauseVdmaChannel(const vdma::ChannelId &channel_id, hailo_stream_direction_t stream_direction);

    const vdma::ChannelId m_channel_id;
    const hailo_stream_direction_t m_stream_direction;
};

class ResumeVdmaChannel : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const EdgeLayer &edge_layer);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    ResumeVdmaChannel(const vdma::ChannelId &channel_id, hailo_stream_direction_t stream_direction);

    const vdma::ChannelId m_channel_id;
    const hailo_stream_direction_t m_stream_direction;
};

class WaitDmaIdleAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t stream_index);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit WaitDmaIdleAction(uint8_t stream_index);

    uint8_t m_stream_index;
};

class WaitNmsIdleAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t aggregator_index,
        uint8_t pred_cluster_ob_index, uint8_t pred_cluster_ob_cluster_index, uint8_t pred_cluster_ob_interface,
        uint8_t succ_prepost_ob_index, uint8_t succ_prepost_ob_interface);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WaitNmsIdleAction(uint8_t aggregator_index, uint8_t pred_cluster_ob_index, uint8_t pred_cluster_ob_cluster_index,
        uint8_t pred_cluster_ob_interface, uint8_t succ_prepost_ob_index, uint8_t succ_prepost_ob_interface);

    uint8_t m_aggregator_index;
    uint8_t m_pred_cluster_ob_index;
    uint8_t m_pred_cluster_ob_cluster_index;
    uint8_t m_pred_cluster_ob_interface;
    uint8_t m_succ_prepost_ob_index;
    uint8_t m_succ_prepost_ob_interface;
};

class EnableNmsAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t nms_unit_index, uint8_t network_index, uint16_t number_of_classes,
        uint16_t burst_size, uint8_t division_factor);
    EnableNmsAction(EnableNmsAction &&) = default;
    EnableNmsAction(const EnableNmsAction &) = delete;
    EnableNmsAction &operator=(EnableNmsAction &&) = delete;
    EnableNmsAction &operator=(const EnableNmsAction &) = delete;
    virtual ~EnableNmsAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    EnableNmsAction(uint8_t nms_unit_index, uint8_t network_index, uint16_t number_of_classes, uint16_t burst_size, uint8_t division_factor);

    const uint8_t m_nms_unit_index;
    const uint8_t m_network_index;
    const uint16_t m_number_of_classes;
    const uint16_t m_burst_size;
    const uint8_t m_division_factor;
};

class WriteDataByTypeAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint32_t address, uint8_t data_type, uint32_t data,
        uint8_t shift, uint32_t mask, uint8_t network_index);

    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    WriteDataByTypeAction(uint32_t address, uint8_t data_type, uint32_t data, uint8_t shift, uint32_t mask, uint8_t network_index);

    const uint32_t m_address;
    const uint8_t m_data_type;
    const uint32_t m_data;
    const uint8_t m_shift;
    const uint32_t m_mask;
    const uint8_t m_network_index;

};

class SwitchLcuBatchAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t cluster_index, uint8_t lcu_index, uint8_t network_index,
        uint32_t kernel_done_count);
    SwitchLcuBatchAction(SwitchLcuBatchAction &&) = default;
    SwitchLcuBatchAction(const SwitchLcuBatchAction &) = delete;
    SwitchLcuBatchAction &operator=(SwitchLcuBatchAction &&) = delete;
    SwitchLcuBatchAction &operator=(const SwitchLcuBatchAction &) = delete;
    virtual ~SwitchLcuBatchAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    SwitchLcuBatchAction(uint8_t cluster_index, uint8_t lcu_index, uint8_t network_index, uint32_t kernel_done_count);

    const uint8_t m_cluster_index;
    const uint8_t m_lcu_index;
    const uint8_t m_network_index;
    const uint32_t m_kernel_done_count;
};

class SleepAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint64_t sleep_time);
    SleepAction(SleepAction &&) = default;
    SleepAction(const SleepAction &) = delete;
    SleepAction &operator=(SleepAction &&) = delete;
    SleepAction &operator=(const SleepAction &) = delete;
    virtual ~SleepAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    SleepAction(uint32_t sleep_time);

    const uint32_t m_sleep_time = 0;
};

class HaltAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    HaltAction(HaltAction &&) = default;
    HaltAction(const HaltAction &) = delete;
    HaltAction &operator=(HaltAction &&) = delete;
    HaltAction &operator=(const HaltAction &) = delete;
    virtual ~HaltAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    HaltAction();
};

class ConfigChannelPreAllowInputDataflowAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    ConfigChannelPreAllowInputDataflowAction(ConfigChannelPreAllowInputDataflowAction &&) = default;
    ConfigChannelPreAllowInputDataflowAction(const ConfigChannelPreAllowInputDataflowAction &) = delete;
    ConfigChannelPreAllowInputDataflowAction &operator=(ConfigChannelPreAllowInputDataflowAction &&) = delete;
    ConfigChannelPreAllowInputDataflowAction &operator=(const ConfigChannelPreAllowInputDataflowAction &) = delete;
    virtual ~ConfigChannelPreAllowInputDataflowAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit ConfigChannelPreAllowInputDataflowAction();
};

class DisableDataChannelsAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    DisableDataChannelsAction(DisableDataChannelsAction &&) = default;
    DisableDataChannelsAction(const DisableDataChannelsAction &) = delete;
    DisableDataChannelsAction &operator=(DisableDataChannelsAction &&) = delete;
    DisableDataChannelsAction &operator=(const DisableDataChannelsAction &) = delete;
    virtual ~DisableDataChannelsAction() = default;
    virtual bool supports_repeated_block() const override;
    virtual Expected<Buffer> serialize_params(const ContextResources &context_resources) const override;

private:
    explicit DisableDataChannelsAction();
};

} /* namespace hailort */

#endif /* _HAILO_CONTEXT_SWITCH_ACTIONS_HPP_ */
