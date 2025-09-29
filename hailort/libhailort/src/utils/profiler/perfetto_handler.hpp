/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file perfetto_handler.hpp
 * @brief Perfetto handler for HailoRT
 **/

#ifndef _HAILO_PERFETTO_HANDLER_HPP_
#define _HAILO_PERFETTO_HANDLER_HPP_

#include "handler.hpp"

namespace hailort
{


class PerfettoHandler : public Handler
{
public:
    PerfettoHandler(PerfettoHandler const&) = delete;
    void operator=(PerfettoHandler const&) = delete;

    PerfettoHandler() = default;
    ~PerfettoHandler() = default;

    virtual void handle_trace(const RunPushAsyncStartTrace&) override;
    virtual void handle_trace(const RunPushAsyncEndTrace&) override;
    virtual void handle_trace(const AsyncInferStartTrace&) override;
    virtual void handle_trace(const AsyncInferEndTrace&) override;
    virtual void handle_trace(const SwitchCoreOpStartTrace&) override;
    virtual void handle_trace(const SwitchCoreOpEndTrace&) override;
    virtual void handle_trace(const SetCoreOpPriorityTrace&) override;
    virtual void handle_trace(const SetCoreOpThresholdTrace&) override;
    virtual void handle_trace(const SetCoreOpTimeoutTrace&) override;
    virtual void handle_trace(const OracleDecisionTrace&) override;
    virtual void handle_trace(const PrepareCoreOpStartTrace&) override;
    virtual void handle_trace(const PrepareCoreOpEndTrace&) override;
    virtual void handle_trace(const SchedulerInferAsyncStartTrace&) override;
    virtual void handle_trace(const SchedulerInferAsyncEndTrace&) override;
    virtual void handle_trace(const SchedulerEnqueueInferRequestStartTrace&) override;
    virtual void handle_trace(const SchedulerEnqueueInferRequestEndTrace&) override;
};
}

#endif /* _PERFETTO_HANDLER_HPP_ */
