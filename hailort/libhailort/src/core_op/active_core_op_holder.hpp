/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file active_core_op_holder.hpp
 * @brief place_holder stored in ConfigManager indicating which CoreOp is currently active
 *
 **/

#ifndef _HAILO_CONTEXT_SWITCH_ACTIVE_CORE_OP_HOLDER_HPP_
#define _HAILO_CONTEXT_SWITCH_ACTIVE_CORE_OP_HOLDER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"


namespace hailort
{

class CoreOp;

class ActiveCoreOpHolder final
{
  public:
    ActiveCoreOpHolder() : m_core_op(nullptr) {}

    ExpectedRef<CoreOp> get()
    {
        CHECK_NOT_NULL_AS_EXPECTED(m_core_op, HAILO_INVALID_OPERATION);
        return std::ref(*m_core_op);
    }
    void set(CoreOp &core_op)
    {
        assert(!is_any_active());
        m_core_op = &core_op;
    }

    bool is_any_active() { return nullptr != m_core_op; }

    void clear() { m_core_op = nullptr; }

    ActiveCoreOpHolder(ActiveCoreOpHolder&) = delete;
    ActiveCoreOpHolder& operator=(ActiveCoreOpHolder&) = delete;
    ActiveCoreOpHolder& operator=(ActiveCoreOpHolder&&) = delete;
    ActiveCoreOpHolder(ActiveCoreOpHolder&&) = default;
  private:
    CoreOp *m_core_op;
};

} /* namespace hailort */

#endif //_HAILO_CONTEXT_SWITCH_ACTIVE_CORE_OP_HOLDER_HPP_