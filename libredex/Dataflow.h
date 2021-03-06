/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "ControlFlow.h"
#include "IRInstruction.h"

template <typename T>
std::unique_ptr<std::unordered_map<IRInstruction*, T>> forwards_dataflow(
    const std::vector<Block*>& blocks,
    const T& bottom,
    const std::function<void(FatMethod::iterator, T*)>& trans,
    const T& entry_value) {
  std::vector<T> block_outs(blocks.size(), bottom);
  std::deque<Block*> work_list(blocks.begin(), blocks.end());
  while (!work_list.empty()) {
    auto block = work_list.front();
    work_list.pop_front();
    auto insn_in = bottom;
    if (block->id() == 0) {
      insn_in = entry_value;
    }
    for (Block* pred : block->preds()) {
      insn_in.meet(block_outs[pred->id()]);
    }
    for (auto it = block->begin(); it != block->end(); ++it) {
      if (it->type != MFLOW_OPCODE) {
        continue;
      }
      trans(it, &insn_in);
    }
    if (insn_in != block_outs[block->id()]) {
      block_outs[block->id()] = std::move(insn_in);
      for (auto succ : block->succs()) {
        if (std::find(work_list.begin(), work_list.end(), succ) ==
            work_list.end()) {
          work_list.push_back(succ);
        }
      }
    }
  }

  auto insn_in_map =
      std::make_unique<std::unordered_map<IRInstruction*, T>>();
  for (const auto& block : blocks) {
    auto insn_in = bottom;
    if (block->id() == 0) {
      insn_in = entry_value;
    }
    for (Block* pred : block->preds()) {
      insn_in.meet(block_outs[pred->id()]);
    }
    for (auto it = block->begin(); it != block->end(); ++it) {
      if (it->type != MFLOW_OPCODE) {
        continue;
      }
      IRInstruction* insn = it->insn;
      insn_in_map->emplace(insn, insn_in);
      trans(it, &insn_in);
    }
  }

  return insn_in_map;
}

template <typename T>
std::unique_ptr<std::unordered_map<IRInstruction*, T>> forwards_dataflow(
    const std::vector<Block*>& blocks,
    const T& bottom,
    const std::function<void(FatMethod::iterator, T*)>& trans) {
  return forwards_dataflow(blocks, bottom, trans, bottom);
}
