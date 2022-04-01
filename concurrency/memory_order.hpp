#pragma once

#include <atomic>

namespace utils_tm
{
namespace concurrency_tm
{
#ifndef MAKE_SEQ_CST // THIS SWITCH CAN CHANGE ALL MEMORY ORDERS TO SEQUENTIAL
                     // CONSISTENCY
static constexpr std::memory_order mo_relaxed = std::memory_order_relaxed;
static constexpr std::memory_order mo_acquire = std::memory_order_acquire;
static constexpr std::memory_order mo_release = std::memory_order_release;
static constexpr std::memory_order mo_acq_rel = std::memory_order_acq_rel;
static constexpr std::memory_order mo_seq_cst = std::memory_order_seq_cst;
#else
static constexpr std::memory_order mo_relaxed = std::memory_order_seq_cst;
static constexpr std::memory_order mo_acquire = std::memory_order_seq_cst;
static constexpr std::memory_order mo_release = std::memory_order_seq_cst;
static constexpr std::memory_order mo_acq_rel = std::memory_order_seq_cst;
static constexpr std::memory_order mo_seq_cst = std::memory_order_seq_cst;
#endif
} // namespace concurrency_tm
} // namespace utils_tm
