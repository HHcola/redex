/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <mutex>

#include "ConstPropV3Config.h"
#include "GlobalConstProp.h"
#include "IRCode.h"
#include "LocalConstProp.h"
#include "Pass.h"

using std::placeholders::_1;
using std::vector;

/** Intraprocedural Constant propagation
 * This code leverages the analysis built by LocalConstantPropagation
 * with works at the basic block level and extends its capabilities by
 * leveraging the Abstract Interpretation Framework's FixPointIterator
 * and HashedAbstractEnvironment facilities.
 *
 * By running the fix point iterator, instead of having no knowledge at
 * the start of a basic block, we can now run the analsys with constants
 * that have been propagated beyond the basic block boundary making this
 * more powerful than its predecessor pass.
 */
class IntraProcConstantPropagation final
    : public ConstantPropFixpointAnalysis<Block*,
                                          MethodItemEntry,
                                          std::vector<Block*>,
                                          InstructionIterable> {
 public:
  explicit IntraProcConstantPropagation(ControlFlowGraph& cfg,
                                        const ConstPropV3Config& config)
      : ConstantPropFixpointAnalysis<Block*,
                                     MethodItemEntry,
                                     vector<Block*>,
                                     InstructionIterable>(
            cfg.entry_block(),
            cfg.blocks(),
            std::bind(&Block::succs, _1),
            std::bind(&Block::preds, _1)),
        m_lcp{config} {}

  void simplify_instruction(
      Block* const& block,
      MethodItemEntry& mie,
      const ConstPropEnvironment& current_state) const override;
  void analyze_instruction(const MethodItemEntry& mie,
                           ConstPropEnvironment* current_state) const override;
  void apply_changes(DexMethod* method) const;

  size_t branches_removed() const { return m_lcp.num_branch_propagated(); }
  size_t materialized_consts() const { return m_lcp.num_materialized_consts(); }

 private:
  mutable LocalConstantPropagation m_lcp;
};

class ConstantPropagationPassV3 : public Pass {
 public:
  ConstantPropagationPassV3()
      : Pass("ConstantPropagationPassV3"), m_branches_removed(0) {}

  virtual void configure_pass(const PassConfig& pc) override;
  virtual void run_pass(DexStoresVector& stores,
                        ConfigFiles& cfg,
                        PassManager& mgr) override;


 private:
  ConstPropV3Config m_config;

  // stats
  std::mutex m_stats_mutex;
  size_t m_branches_removed;
  size_t m_materialized_consts;
};
