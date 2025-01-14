/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fbgemm_gpu/permute_multi_embedding_function.h"
#include <cstdint>
#include <iostream>

namespace fbgemm_gpu {

using Tensor = at::Tensor;
using torch::autograd::AutogradContext;
using torch::autograd::variable_list;

variable_list PermuteMultiEmbeddingOp::forward(
    AutogradContext* ctx,
    const at::TensorList& pooled_embs,
    const Tensor& permutes,
    const Tensor& in_shapes,
    const Tensor& out_shapes,
    const std::vector<int64_t>& out_lengths) {
  ctx->saved_data["permutes"] = permutes;
  ctx->saved_data["in_shapes"] = in_shapes;
  ctx->saved_data["out_shapes"] = out_shapes;

  std::vector<int64_t> in_lengths;
  in_lengths.reserve(pooled_embs.size());
  for (auto i : c10::irange(pooled_embs.size())) {
    in_lengths.push_back(pooled_embs[i].size(1));
  }
  ctx->saved_data["in_lengths"] = in_lengths;

  /*
    select the correct dispatched (cpu/gpu) forward function
    the cpu/gup function needs to be registered in the dispatcher,
    e.g., DISPATCH_TO_CPU, DISPATCH_TO_CUDA, etc.
  */
  const auto permute_op =
      torch::Dispatcher::singleton()
          .findSchemaOrThrow("fbgemm::permute_multi_embedding_function", "")
          .typed<decltype(permute_multi_embedding_cpu)>();

  return permute_op.call(
      pooled_embs, permutes, in_shapes, out_shapes, out_lengths, false);
}

variable_list PermuteMultiEmbeddingOp::backward(
    AutogradContext* ctx,
    variable_list grad_output) {
  const auto permutes = ctx->saved_data["permutes"].toTensor();
  const auto in_shapes = ctx->saved_data["in_shapes"].toTensor();
  const auto out_shapes = ctx->saved_data["out_shapes"].toTensor();
  const auto in_lengths = ctx->saved_data["in_lengths"].toIntVector();

  /*
    select the correct dispatched (cpu/gpu) backward function
    the cpu/gup function needs to be registered in the dispatcher,
    e.g., DISPATCH_TO_CPU, DISPATCH_TO_CUDA, etc.
  */
  const auto permute_op =
      torch::Dispatcher::singleton()
          .findSchemaOrThrow("fbgemm::permute_multi_embedding_function", "")
          .typed<decltype(permute_multi_embedding_cpu)>();
  auto grad_input = permute_op.call(
      grad_output, permutes, out_shapes, in_shapes, in_lengths, true);
  grad_input.push_back(torch::autograd::Variable()); // permutes
  grad_input.push_back(torch::autograd::Variable()); // in_shapes
  grad_input.push_back(torch::autograd::Variable()); // out_shapes
  grad_input.push_back(torch::autograd::Variable()); // out_lengths
  return grad_input;
}

} // namespace fbgemm_gpu
