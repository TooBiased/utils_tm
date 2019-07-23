/*******************************************************************************
 * utils/test_coordination.h
 *
 * Offers low level functionality for thread synchronization
 * and parallel for loops (used to simplify writing tests/benchmarks)
 *
 * Part of Project growt - https://github.com/TooBiased/growt.git
 *
 * Copyright (C) 2015-2016 Tobias Maier <t.maier@kit.edu>
 *
 * All rights reserved. Published under the BSD-2 license in the LICENSE file.
 ******************************************************************************/

#ifndef TEST_COORDINATION_H
#define TEST_COORDINATION_H

#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include "utils/output.h"

static std::atomic_size_t        level;
static std::atomic_size_t        wait_end;
static std::atomic_size_t        wait_start;


// MAIN THREAD CLASS
template <bool timed>
struct MainThread
{
    MainThread(size_t p, size_t id) : p(p), id(id), _stage(0) { }

    template<typename Functor, typename ... Types>
    inline std::pair<typename std::result_of<Functor(Types&& ...)>::type, size_t>
        synchronized(Functor f, Types&& ... param)
    {
        start_stage(p-1, ++_stage);
        auto temp = std::forward<Functor>(f)(std::forward<Types>(param) ... );
        return std::make_pair(std::move(temp), end_stage(p-1));
    }

    inline void synchronize()
    {
        start_stage(p-1, ++_stage);
        end_stage(p-1);
    }

    size_t p;
    size_t id;
    out_tm::output_type out;
    static constexpr bool is_main = true;

private:
    size_t _stage;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

    inline void start_stage(size_t p, size_t lvl)
    {
        while(wait_start.load(std::memory_order_acquire) < p);
        wait_start.store(0, std::memory_order_release);

        if constexpr (timed)
        {
            start_time = std::chrono::high_resolution_clock::now();
        }

        level.store(lvl, std::memory_order_release);
    }

    inline size_t end_stage(size_t p)
    {
        while (wait_end.load(std::memory_order_acquire) < p);
        wait_end.store(0, std::memory_order_release);
        size_t result = 0;
        if constexpr (timed)
        {
            result = std::chrono::duration_cast<std::chrono::nanoseconds>
               (std::chrono::high_resolution_clock::now() - start_time).count();
        }
        return result;
    }
};

using   TimedMainThread = MainThread<true>;
using UnTimedMainThread = MainThread<false>;



// SUB THREAD CLASS
template <bool timed>
struct SubThread
{
    SubThread(size_t p, size_t id) : p(p), id(id), _stage(0)
    {
        out.disable();
    }

    template<typename Functor, typename ... Types>
    inline std::pair<typename std::result_of<Functor(Types&& ...)>::type, size_t>
    synchronized(Functor f, Types&& ... param)
    {
        start_stage(++_stage);//wait_for_stage(stage);
        auto temp = std::forward<Functor>(f)(std::forward<Types>(param)...);
        //finished_stage();
        return std::make_pair(temp, end_stage());
    }

    inline void synchronize()
    {
        start_stage(++_stage);
        end_stage();
    }

    size_t p;
    size_t id;
    out_tm::output_type out;
    static constexpr bool is_main = false;

private:
    size_t _stage;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

    inline void start_stage(size_t lvl)
    {
        wait_start.fetch_add(1, std::memory_order_acq_rel);
        while (level.load(std::memory_order_acquire) < lvl);
        if constexpr (timed)
        {
            start_time = std::chrono::high_resolution_clock::now();
        }
    }

    inline size_t end_stage()
    {
        wait_end.fetch_add(1, std::memory_order_acq_rel);

        size_t result = 0;
        if constexpr (timed)
        {
            result = std::chrono::duration_cast<std::chrono::nanoseconds>
                (std::chrono::high_resolution_clock::now() - start_time).count();
        }

        return result;
    }
};

//time is measured relative to the global start
using     TimedSubThread = SubThread<true>;
using   UnTimedSubThread = SubThread<false>;



// STARTS P-1 TFunctor THREADS, THEN EXECUTES MFunctor, THEN REJOINS GENERATED THREADS
template<template <class> class Functor, typename ... Types>
inline int start_threads(size_t p, Types&& ... param)
{
    std::thread* local_thread = new std::thread[p-1];

    for (size_t i = 0; i < p-1; ++i)
    {
        local_thread[i] = std::thread(Functor<UnTimedSubThread>::execute,
                                      UnTimedSubThread(p, i+1),
                                      std::ref(std::forward<Types>(param))...);
    }

    // int temp =0;
    int temp = Functor<TimedMainThread>::execute(TimedMainThread(p, 0),
                                                 std::forward<Types>(param)...);

    // CLEANUP THREADS
    for (size_t i = 0; i < p-1; ++i)
    {
        local_thread[i].join();
    }

    delete[] local_thread;

    return temp;
}



static const size_t block_size = 4096;



//BLOCKWISE EXECUTION IN PARALLEL
template<typename Functor, typename ... Types>
inline void execute_parallel(std::atomic_size_t& global_counter, size_t e,
                                       Functor f, Types&& ... param)
{
    auto c_s = global_counter.fetch_add(block_size);
    while (c_s < e)
    {
        auto c_e = std::min(c_s+block_size, e);
        for (size_t i = c_s; i < c_e; ++i)
            f(i, std::forward<Types>(param)...);
        c_s = global_counter.fetch_add(block_size);
    }
}

template<typename Functor, typename ... Types>
inline void execute_blockwise_parallel(std::atomic_size_t& global_counter, size_t e,
                                       Functor f, Types&& ... param)
{
    auto c_s = global_counter.fetch_add(block_size);
    while (c_s < e)
    {
        auto c_e = std::min(c_s+block_size, e);
        f(c_s, c_e, std::forward<Types>(param)...);
        c_s = global_counter.fetch_add(block_size);
    }
}

#endif // TEST_COORDINATION_H
