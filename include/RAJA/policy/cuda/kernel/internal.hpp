/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   RAJA header file containing constructs used to run kernel
 *          traversals on GPU with CUDA.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-18, Lawrence Livermore National Security, LLC.
//
// Produced at the Lawrence Livermore National Laboratory
//
// LLNL-CODE-689114
//
// All rights reserved.
//
// This file is part of RAJA.
//
// For details about use and distribution, please read RAJA/LICENSE.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//


#ifndef RAJA_policy_cuda_kernel_internal_HPP
#define RAJA_policy_cuda_kernel_internal_HPP

#include "RAJA/config.hpp"

#if defined(RAJA_ENABLE_CUDA)

#include <cassert>
#include <climits>

#include "camp/camp.hpp"

#include "RAJA/pattern/kernel.hpp"

#include "RAJA/util/macros.hpp"
#include "RAJA/util/types.hpp"

#include "RAJA/policy/cuda/MemUtils_CUDA.hpp"
#include "RAJA/policy/cuda/policy.hpp"

#include "RAJA/internal/LegacyCompatibility.hpp"


namespace RAJA
{


/*!
 * Policy for For<>, executes loop iteration by distributing them over threads.
 * This does no (additional) work-sharing between thread blocks.
 */

struct cuda_thread_exec
    : public RAJA::
          make_policy_pattern_launch_platform_t<RAJA::Policy::cuda,
                                                RAJA::Pattern::forall,
                                                RAJA::Launch::undefined,
                                                RAJA::Platform::cuda> {
};


/*!
 * Policy for For<>, executes loop iteration by distributing iterations
 * exclusively over blocks.
 */

struct cuda_block_exec
    : public RAJA::
          make_policy_pattern_launch_platform_t<RAJA::Policy::cuda,
                                                RAJA::Pattern::forall,
                                                RAJA::Launch::undefined,
                                                RAJA::Platform::cuda> {
};


/*!
 * Policy for For<>, executes loop iteration by distributing work over
 * physical blocks and executing sequentially within blocks.
 */

template <size_t num_blocks>
struct cuda_block_seq_exec
    : public RAJA::
          make_policy_pattern_launch_platform_t<RAJA::Policy::cuda,
                                                RAJA::Pattern::forall,
                                                RAJA::Launch::undefined,
                                                RAJA::Platform::cuda> {
};


/*!
 * Policy for For<>, executes loop iteration by distributing them over threads
 * and blocks, but limiting the number of threads to num_threads.
 */
template <size_t num_threads>
struct cuda_threadblock_exec
    : public RAJA::
          make_policy_pattern_launch_platform_t<RAJA::Policy::cuda,
                                                RAJA::Pattern::forall,
                                                RAJA::Launch::undefined,
                                                RAJA::Platform::cuda> {
};


namespace internal
{


struct LaunchDim {

  int blocks;
  int threads;

  RAJA_INLINE
  RAJA_HOST_DEVICE
  constexpr LaunchDim() : blocks(0), threads(0) {}


  RAJA_INLINE
  RAJA_HOST_DEVICE
  constexpr LaunchDim(int b, int t) : blocks(b), threads(t) {}


  RAJA_INLINE
  RAJA_HOST_DEVICE
  constexpr LaunchDim(LaunchDim const &c) : blocks(c.blocks), threads(c.threads)
  {
  }


  RAJA_INLINE
  LaunchDim maximum(LaunchDim const &c) const
  {
    LaunchDim result(-1, -1);

    if(c.blocks > -1 && blocks > -1){
      result.blocks = std::max(c.blocks, blocks);
    }

    if(c.threads > -1 && threads > -1){
      result.threads = std::max(c.threads, threads);
    }

    return result;
  }

  RAJA_INLINE
  RAJA_HOST_DEVICE
  constexpr LaunchDim operator*(LaunchDim const &c) const
  {
    return LaunchDim{blocks * c.blocks, threads * c.threads};
  }


  RAJA_INLINE
  void addThreads(int t){
    // Create a trap for an argument with 0 threads
    if(t == 0 || threads == -1){
      threads = -1;
    }
    else{
      threads = (threads == 0) ? t : threads*t;
    }
  }

  RAJA_INLINE
  void addBlocks(int b){
    // Create a trap for an argument with 0 blocks
    if(b == 0 || blocks == -1){
      blocks = 1;
    }
    else{
      blocks = (blocks == 0) ? b : blocks*b;
    }
  }
};

struct CudaLaunchLimits {
  LaunchDim max_dims;
  LaunchDim physical_dims;
};


struct CudaCarryPair {
  int first;
  int second;
};


template <camp::idx_t ArgumentId, typename ExecPolicy>
struct CudaIndexCalc_Policy;


template <camp::idx_t ArgumentId>
struct CudaIndexCalc_Policy<ArgumentId, seq_exec> {


  template <typename Data>
  RAJA_INLINE
  RAJA_HOST_DEVICE
  CudaCarryPair
  initThread(Data &data, int carry_init, int carry_incr, bool final = false)
  {
    return CudaCarryPair{carry_init, carry_incr};
  }


  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE bool reset(Data &data)
  {
    data.template assign_offset<ArgumentId>(0);

    // return true if there are no iterations
    return segment_length<ArgumentId>(data) == 0;
  }



  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE int increment(Data &data, int carry)
  {
    int i = camp::get<ArgumentId>(data.offset_tuple);
    ++i;

    int len = segment_length<ArgumentId>(data);
    if (i >= len) {
      i = 0;
    } else {
      carry = 0;
    }

    data.template assign_offset<ArgumentId>(i);

    return carry;
  }
};


template <camp::idx_t ArgumentId>
struct CudaIndexCalc_Policy<ArgumentId, cuda_thread_exec> {

  int i0; // initial value upon reset
  int full_cycle; // minimum number of full trips per execution


  /*
   * Initializes the iteration pattern for a specific thread.
   *
   * This sets up the striding and initial values for a thread.  This only
   * needs to be called once.
   *
   * carry_thread = threadIdx.x  // what our starting offset is
   * carry_incr = blockDim.x     // what our block-stride increment is
   *
   * co = carry_thread / len     number initial iterations we skip
   *                           (also, how many initial iterations do we pass
   *                           on the next loop level up)
   *
   * i0 = carry_thread % len     our starting i
   *
   *
   *
   *
   */
  template <typename Data>
  RAJA_INLINE
  RAJA_HOST_DEVICE
  CudaCarryPair
  initThread(Data &data, int carry_thread, int carry_incr, bool final = false)
  {
    int len = segment_length<ArgumentId>(data);

    // set i0 to -1 if we have no iterations
    if(final && carry_thread >= len){
      i0 = -1;
      return CudaCarryPair{0, 0};
    }
    else{
			
      int carry_out = carry_thread / len;
      
      i0 = carry_thread - (carry_out * len); // equiv: carry_thread % len

      full_cycle = carry_incr/len;
   		
      return CudaCarryPair{carry_out, full_cycle};
    }
  }

  /*
   * Resets the iteration to the beginning.
   * This should be called each time a nested-loop structure is executed.
   */
  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE bool reset(Data &data)
  {
    data.template assign_offset<ArgumentId>(i0);

    // return TRUE if there are no iterations for us
    return i0 < 0;
  }



  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE int increment(Data &data, int carry_in)
  {
    int i = camp::get<ArgumentId>(data.offset_tuple);
    int len = segment_length<ArgumentId>(data);

    i += carry_in - len*full_cycle;

    int carry_out = full_cycle;
    while (i >= len) {
      i -= len;
      ++carry_out;
    }

    data.template assign_offset<ArgumentId>(i);

    return carry_out;
  }
};


template <camp::idx_t Idx>
struct IndexCalcHelper {

  template <typename Data, typename CalcList>
  static RAJA_INLINE RAJA_HOST_DEVICE CudaCarryPair initThread(Data &data,
                                                     CalcList &calc_list,
                                                     int carry_init,
                                                     int carry_incr)
  {
    auto carry_next = camp::get<Idx>(calc_list).initThread(data,
                                                           carry_init,
                                                           carry_incr,
                                                           Idx == 0);

    return IndexCalcHelper<Idx - 1>::initThread(data,
                                                calc_list,
                                                carry_next.first,
                                                carry_next.second);
  }


  template <typename Data, typename CalcList>
  static RAJA_INLINE RAJA_HOST_DEVICE bool reset(Data &data,
                                                     CalcList &calc_list)
  {

    bool done = camp::get<Idx>(calc_list).reset(data);

    return done || IndexCalcHelper<Idx - 1>::reset(data, calc_list);
  }


  template <typename Data, typename CalcList>
  static RAJA_INLINE RAJA_HOST_DEVICE int increment(Data &data,
                                                    CalcList &calc_list,
                                                    int carry_in)
  {
    int carry_next = camp::get<Idx>(calc_list).increment(data, carry_in);
    return carry_next == 0 ? 0
                           : IndexCalcHelper<Idx - 1>::increment(data,
                                                                 calc_list,
                                                                 carry_next);
  }
};

template <>
struct IndexCalcHelper<-1> {

  template <typename Data, typename CalcList>
  static RAJA_INLINE RAJA_HOST_DEVICE CudaCarryPair initThread(Data &,
                                                     CalcList &,
                                                     int carry_init,
                                                     int carry_incr)
  {
    return CudaCarryPair{carry_init, carry_incr};
  }

  template <typename Data, typename CalcList>
    static RAJA_INLINE RAJA_HOST_DEVICE bool reset(Data &,
                                                   CalcList &)
  {
    return false;
  }


  template <typename Data, typename CalcList>
  static RAJA_INLINE RAJA_HOST_DEVICE int increment(Data &,
                                                    CalcList &,
                                                    int carry_in)
  {
    return carry_in;
  }
};


template <typename SegmentTuple, typename ExecPolicies, typename RangeList>
struct CudaIndexCalc;


template <typename SegmentTuple,
          typename... IndexPolicies,
          camp::idx_t... RangeInts>
struct CudaIndexCalc<SegmentTuple,
                     camp::list<IndexPolicies...>,
                     camp::idx_seq<RangeInts...>> {

  using CalcList = camp::tuple<IndexPolicies...>;

  CalcList calc_list;


  /**
   * Computes indexing for each loop, must be called any time the loop
   * dimensions change.
   */

  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE void initThread(Data &data, int thread, int num_threads)
  {
    IndexCalcHelper<sizeof...(RangeInts) - 1>::initThread(data,
                                                          calc_list,
                                                          thread,
                                                          num_threads);
  }


  /**
   * Assigns beginning index for all arguments in the calc list
   */
  template <typename Data>
  RAJA_INLINE RAJA_DEVICE bool reset(Data &data)
  {
    return IndexCalcHelper<sizeof...(RangeInts) - 1>::reset(data, calc_list);
  }


  /**
   * Increment calc list by the increment amount.
   *
   * The loop is "done" when we get something carried out of the top-level loop
   */
  template <typename Data>
  RAJA_INLINE RAJA_DEVICE bool increment(Data &data)
  {
    int carry = blockDim.x;
    return IndexCalcHelper<sizeof...(RangeInts) - 1>::increment(data,
                                                                calc_list,
                                                                carry)
           > 0;
  }
};


/**
 * Empty calculator case.  Needed for SetShmemWindow when no For loops have
 * been issues (ie. just Tile loops)
 */
template <typename SegmentTuple>
struct CudaIndexCalc<SegmentTuple, camp::list<>, camp::idx_seq<>> {

  template <typename Data>
  RAJA_INLINE RAJA_HOST_DEVICE void initThread(Data &, int, int)
  {
    // each physical thread will execute
  }

  template <typename Data>
  RAJA_INLINE RAJA_DEVICE bool reset(Data &)
  {
    // each physical thread will execute
    return false;
  }

  template <typename Data>
  RAJA_INLINE RAJA_DEVICE bool increment(Data &)
  {
    // only one execution per physical thread
    return true;
  }
};

template <typename SegmentTuple>
using CudaIndexCalc_Terminator =
    CudaIndexCalc<SegmentTuple, camp::list<>, camp::idx_seq<>>;


template <typename IndexCalcBase, typename CalcN>
struct CudaIndexCalc_Extender;


template <typename SegmentTuple,
          typename... CalcPolicies,
          camp::idx_t... RangeInts,
          typename CalcN>
struct CudaIndexCalc_Extender<CudaIndexCalc<SegmentTuple,
                                            camp::list<CalcPolicies...>,
                                            camp::idx_seq<RangeInts...>>,
                              CalcN> {
  using type = CudaIndexCalc<SegmentTuple,
                             camp::list<CalcPolicies..., CalcN>,
                             camp::idx_seq<RangeInts..., sizeof...(RangeInts)>>;
};

template <typename IndexCalcBase, typename CalcN>
using ExtendCudaIndexCalc =
    typename CudaIndexCalc_Extender<IndexCalcBase, CalcN>::type;


template <camp::idx_t cur_stmt, camp::idx_t num_stmts>
struct CudaStatementListExecutorHelper {

  using next_helper_t =
      CudaStatementListExecutorHelper<cur_stmt + 1, num_stmts>;

  template <typename StmtTuple, typename Data>
  inline static __device__ void exec(StmtTuple &stmts,
                                     Data &data,
                                     int num_logical_blocks,
                                     int block_carry)
  {
    // Execute stmt
    camp::get<cur_stmt>(stmts).exec(data, num_logical_blocks, block_carry);

    // Execute next stmt
    next_helper_t::exec(stmts, data, num_logical_blocks, block_carry);
  }

  template <typename StmtTuple, typename Data>
  inline static RAJA_HOST_DEVICE void initBlocks(StmtTuple &stmts,
                                           Data &data,
                                           int num_logical_blocks,
                                           int block_stride)
  {
    // Execute stmt
    camp::get<cur_stmt>(stmts).initBlocks(data,
                                          num_logical_blocks,
                                          block_stride);

    // Execute next stmt
    next_helper_t::initBlocks(stmts, data, num_logical_blocks, block_stride);
  }

  template <typename StmtTuple, typename Data>
  inline static RAJA_DEVICE void initThread(StmtTuple &stmts,
                                           Data &data)
  {
    // Execute stmt
    camp::get<cur_stmt>(stmts).initThread(data);

    // Execute next stmt
    next_helper_t::initThread(stmts, data);
  }

  template <typename StmtTuple, typename Data>
  inline static LaunchDim calculateDimensions(StmtTuple &stmts,
                                              Data &data,
                                              LaunchDim const &max_physical)
  {
    // Compute this statements launch dimensions
    LaunchDim statement_dims =
        camp::get<cur_stmt>(stmts).calculateDimensions(data, max_physical);

    // call the next statement in the list
    LaunchDim next_dims =
        next_helper_t::calculateDimensions(stmts, data, max_physical);

    // Return the maximum of the two
    return statement_dims.maximum(next_dims);
  }
};

template <camp::idx_t num_stmts>
struct CudaStatementListExecutorHelper<num_stmts, num_stmts> {

  template <typename StmtTuple, typename Data>
  inline static __device__ void exec(StmtTuple &, Data &, int, int)
  {
    // nop terminator
  }

  template <typename StmtTuple, typename Data>
  inline static RAJA_HOST_DEVICE void initBlocks(StmtTuple &, Data &, int, int)
  {
    // nop terminator
  }

  template <typename StmtTuple, typename Data>
  inline static RAJA_DEVICE void initThread(StmtTuple &, Data &)
  {
    // nop terminator
  }

  template <typename StmtTuple, typename Data>
  inline static LaunchDim calculateDimensions(StmtTuple &,
                                              Data &,
                                              LaunchDim const &)
  {
    return LaunchDim();
  }
};


template <typename Data, typename Policy, typename IndexCalc>
struct CudaStatementExecutor;

template <typename Data, typename StmtList, typename IndexCalc>
struct CudaStatementListExecutor;


template <typename Data, typename... Stmts, typename IndexCalc>
struct CudaStatementListExecutor<Data, StatementList<Stmts...>, IndexCalc> {

  using enclosed_stmts_t =
      camp::tuple<CudaStatementExecutor<Data, Stmts, IndexCalc>...>;
  enclosed_stmts_t enclosed_stmts;


  inline __device__ void exec(Data &data,
                              int num_logical_blocks,
                              int block_carry)
  {

    // Execute statements in order with helper class
    CudaStatementListExecutorHelper<0, sizeof...(Stmts)>::exec(
        enclosed_stmts, data, num_logical_blocks, block_carry);
  }


  inline RAJA_HOST_DEVICE void initBlocks(Data &data,
                                    int num_logical_blocks,
                                    int block_stride)
  {

    // Execute statements in order with helper class
    CudaStatementListExecutorHelper<0, sizeof...(Stmts)>::initBlocks(
        enclosed_stmts, data, num_logical_blocks, block_stride);
  }

  inline RAJA_DEVICE void initThread(Data &data)
  {

    // Execute statements in order with helper class
    CudaStatementListExecutorHelper<0, sizeof...(Stmts)>::initThread(
        enclosed_stmts, data);
  }


  RAJA_INLINE
  LaunchDim calculateDimensions(Data const &data, LaunchDim const &max_physical)
  {

    // Compute this statements launch dimensions
    return CudaStatementListExecutorHelper<0, sizeof...(Stmts)>::
        calculateDimensions(enclosed_stmts, data, max_physical);
  }
};


template <typename StmtList, typename Data>
using cuda_statement_list_executor_t =
    CudaStatementListExecutor<Data,
                              StmtList,
                              CudaIndexCalc_Terminator<
                                  typename Data::segment_tuple_t>>;


template <camp::idx_t ArgumentId, int threads_per_block>
struct CudaBlockLoop {

  int num_blocks;
  int block_i;

  template <typename Stmts, typename Data>
  inline RAJA_HOST_DEVICE void initBlockLoop(Stmts &enclosed_stmts,
                                       Data &data,
                                       int len,
                                       int num_logical_blocks,
                                       int block_stride)
  {
    num_blocks = len / threads_per_block;
    if (num_blocks * threads_per_block < len) {
      num_blocks++;
    }

    block_i = block_stride % num_blocks;

    // initialize enclosed blocks and statements
    enclosed_stmts.initBlocks(data,
                              num_logical_blocks / num_blocks,
                              block_stride / num_blocks);
  }

  template <typename Stmts, typename Data>
  inline __device__ void execBlockDistribute(Stmts &enclosed_stmts,
                                             Data &data,
                                             int num_logical_blocks,
                                             int block_carry)
  {

    // Increment block by carry-in
    block_i += block_carry;

    // Compute carry out
    // TODO: make this more efficient!
    int carry_out = 0;
    while (block_i >= num_blocks) {
      ++carry_out;
      block_i -= num_blocks;
    }

    auto &segment = camp::get<ArgumentId>(data.segment_tuple);
    using segment_t = camp::decay<decltype(segment)>;

    // Store original segment for later
    segment_t orig_segment = segment;

    // Slice the segment for thread iteration
    segment =
        orig_segment.slice(block_i * threads_per_block, threads_per_block);
    data.template assign_offset<ArgumentId>(0);


    // Reinitialize thread calculations (TODO: optimize this)
    enclosed_stmts.initThread(data);

    // Execute enclosed statements
    enclosed_stmts.exec(data, num_logical_blocks, carry_out);

    // Replace original segment
    segment = orig_segment;
  }


  template <typename Executor, typename Data>
  inline __device__ void execBlockLoop(Executor &exec,
                                       Data &data,
                                       int num_logical_blocks,
                                       int block_carry)
  {
    // if we are already in a block work sharing region, we just assign
    // this block region
    if (block_carry >= 0) {
      execBlockDistribute(exec.enclosed_stmts,
                          data,
                          num_logical_blocks,
                          block_carry);
    }

    // We need to start the logical block grid-stride loop, before we
    // call ourself to distribute work
    else {

      // initialize block indexing for entire sub-tree
      exec.initBlocks(data, num_logical_blocks, blockIdx.x);
      execBlockDistribute(exec.enclosed_stmts, data, num_logical_blocks, 0);

      // set initial logical block to our physical block
      int logical_block = (int)blockIdx.x + gridDim.x;

      while (logical_block < num_logical_blocks) {

        // execute block
        execBlockDistribute(exec.enclosed_stmts,
                            data,
                            num_logical_blocks,
                            gridDim.x);

        // grid stride
        logical_block += gridDim.x;
      }
    }
  }
};


}  // namespace internal
}  // namespace RAJA

#endif  // closing endif for RAJA_ENABLE_CUDA guard

#endif  // closing endif for header file include guard
