/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "AccessMarking.h"

#include <unordered_map>

#include "Devirtualizer.h"
#include "DexUtil.h"
#include "Mutators.h"
#include "ReachableClasses.h"
#include "Resolver.h"
#include "Walkers.h"

namespace {
size_t mark_classes_final(const DexStoresVector& stores) {
  size_t n_classes_finalized = 0;
  for (auto const& dex : DexStoreClassesIterator(stores)) {
    for (auto const& cls : dex) {
      if (is_seed(cls) || is_abstract(cls) || is_final(cls)) continue;
      auto const& children = get_children(cls->get_type());
      if (children.empty()) {
        TRACE(ACCESS, 2, "Finalizing class: %s\n", SHOW(cls));
        set_final(cls);
        ++n_classes_finalized;
      }
    }
  }
  return n_classes_finalized;
}

const DexMethod* find_override(const DexMethod* method, const DexClass* cls) {
  std::vector<const DexType*> children;
  get_all_children(cls->get_type(), children);
  for (auto const& childtype : children) {
    auto const& child = type_class(childtype);
    assert(child);
    for (auto const& child_method : child->get_vmethods()) {
      if (signatures_match(method, child_method)) {
        return child_method;
      }
    }
  }
  return nullptr;
}

size_t mark_methods_final(const DexStoresVector& stores) {
  size_t n_methods_finalized = 0;
  for (auto const& dex : DexStoreClassesIterator(stores)) {
    for (auto const& cls : dex) {
      for (auto const& method : cls->get_vmethods()) {
        if (is_seed(method) || is_abstract(method) || is_final(method)) {
          continue;
        }
        if (!find_override(method, cls)) {
          TRACE(ACCESS, 2, "Finalizing method: %s\n", SHOW(method));
          set_final(method);
          ++n_methods_finalized;
        }
      }
    }
  }
  return n_methods_finalized;
}

bool uses_this(const DexMethod* method) {
  auto const& code = method->get_code();
  if (!code) return false;
  auto const this_reg = code->get_registers_size() - code->get_ins_size();
  for (auto const& insn : code->get_instructions()) {
    if (insn->has_range()) {
      if (this_reg >= insn->range_base() &&
          this_reg < (insn->range_base() + insn->range_size())) {
        return true;
      }
    }
    for (unsigned i = 0; i < insn->srcs_size(); i++) {
      if (this_reg == insn->src(i)) return true;
    }
  }
  return false;
}

std::unordered_set<DexMethod*> find_static_methods(
  const std::vector<DexMethod*>& candidates
) {
  std::unordered_set<DexMethod*> staticized;
  for (auto const& method : candidates) {
    if (uses_this(method) || is_seed(method) || is_abstract(method)) {
      continue;
    }
    TRACE(ACCESS, 2, "Staticized method: %s\n", SHOW(method));
    staticized.emplace(method);
  }
  return staticized;
}

void fix_call_sites(
  const std::vector<DexClass*>& scope,
  const std::unordered_set<DexMethod*>& statics
) {
  walk_opcodes(
    scope,
    [](DexMethod*) { return true; },
    [&](DexMethod*, DexInstruction* inst) {
      if (!inst->has_methods()) return;
      auto mi = static_cast<DexOpcodeMethod*>(inst);
      auto method = mi->get_method();
      if (!method->is_concrete()) {
        method = resolve_method(method, MethodSearch::Any);
      }
      if (statics.count(method)) {
        mi->rewrite_method(method);
        mi->set_opcode(
          is_invoke_range(inst->opcode())
          ? OPCODE_INVOKE_STATIC_RANGE
          : OPCODE_INVOKE_STATIC);
      }
    }
  );
}

void mark_methods_static(const std::unordered_set<DexMethod*>& statics) {
  for (auto method : statics) {
    mutators::make_static(method);
  }
}
}

void AccessMarkingPass::run_pass(
  DexStoresVector& stores,
  ConfigFiles& cfg,
  PassManager& pm
) {
  if (!cfg.using_seeds) {
    return;
  }
  auto n_classes_final = mark_classes_final(stores);
  pm.incr_metric("finalized_classes", n_classes_final);
  TRACE(ACCESS, 1, "Finalized %lu classes\n", n_classes_final);

  auto n_methods_final = mark_methods_final(stores);
  pm.incr_metric("finalized_methods", n_methods_final);
  TRACE(ACCESS, 1, "Finalized %lu methods\n", n_methods_final);

  auto scope = build_class_scope(stores);
  auto const& candidates = devirtualize(scope);
  auto static_methods = find_static_methods(candidates);
  fix_call_sites(scope, static_methods);
  mark_methods_static(static_methods);
  pm.incr_metric("staticized_methods", static_methods.size());
  TRACE(ACCESS, 1, "Staticized %lu methods\n", static_methods.size());
}

static AccessMarkingPass s_pass;