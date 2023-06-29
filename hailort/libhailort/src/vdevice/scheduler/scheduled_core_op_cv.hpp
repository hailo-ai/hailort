/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_core_op_cv.hpp
 * @brief Class declaration for scheduled core-ops conditional variables
 **/

#ifndef _HAILO_SCHEDULED_CORE_OP_CV_HPP_
#define _HAILO_SCHEDULED_CORE_OP_CV_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"

#include <condition_variable>


namespace hailort
{

class ScheduledCoreOpCV
{
public:
    static Expected<std::shared_ptr<ScheduledCoreOpCV>> create(std::shared_ptr<CoreOp> added_cng)
    {
        auto stream_infos = added_cng->get_all_stream_infos();
        CHECK_EXPECTED(stream_infos);

        std::unordered_map<stream_name_t, std::shared_ptr<std::condition_variable>> cv_per_stream;
        for (const auto &stream_info : stream_infos.value()) {
            auto cv = make_shared_nothrow<std::condition_variable>();
            CHECK_NOT_NULL_AS_EXPECTED(cv, HAILO_OUT_OF_HOST_MEMORY);
            cv_per_stream[stream_info.name] = std::move(cv);
        }

        auto scheduled_core_op_cv = make_shared_nothrow<ScheduledCoreOpCV>(cv_per_stream);
        CHECK_NOT_NULL_AS_EXPECTED(scheduled_core_op_cv, HAILO_OUT_OF_HOST_MEMORY);

        return scheduled_core_op_cv;
    }

    virtual ~ScheduledCoreOpCV()  = default;
    ScheduledCoreOpCV(const ScheduledCoreOpCV &other) = delete;
    ScheduledCoreOpCV &operator=(const ScheduledCoreOpCV &other) = delete;
    ScheduledCoreOpCV &operator=(ScheduledCoreOpCV &&other) = delete;
    ScheduledCoreOpCV(ScheduledCoreOpCV &&other) noexcept = delete;

    void notify_one(const stream_name_t &name)
    {
        assert(contains(m_map, name));
        m_map[name]->notify_one();
    }

    void notify_all()
    {
        for (auto &cv : m_map) {
            cv.second->notify_one();
        }
    }

    template<typename _Rep, typename _Period, typename _Predicate>
    bool wait_for(const stream_name_t &name, std::unique_lock<std::mutex>& __lock, const std::chrono::duration<_Rep, _Period>& __rtime, _Predicate __p)
    {
        assert(contains(m_map, name));
        return m_map[name]->wait_for(__lock, __rtime, __p);
    }

    ScheduledCoreOpCV(std::unordered_map<stream_name_t, std::shared_ptr<std::condition_variable>> cv_map) : m_map(std::move(cv_map))
    {}

private:
    std::unordered_map<stream_name_t, std::shared_ptr<std::condition_variable>> m_map;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULED_CORE_OP_CV_HPP_ */
