#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generates tiny, hand-built ONNX graphs that satisfy this project's
stage tensor contract (see docs/MODELS.md), for use as fixtures in the C++
unit tests and as a smoke-test for the pipeline plumbing.

These are NOT trained noise-suppression/VAD/speaker-isolation models -- they
are deterministic toy graphs (identity, a fixed gain, a running difference)
whose output can be predicted exactly in a test assertion, so the tests
verify the C++ pipeline/ONNX-Runtime wiring rather than any audio quality.

Usage:
    python3 scripts/gen_test_models.py [--out-dir models/test] [--frame-size 480]
"""
import argparse
import os

import numpy as np
import onnx
from onnx import TensorProto, helper


def save(graph_name, nodes, inputs, outputs, initializers, out_path, opset=17):
    graph = helper.make_graph(nodes, graph_name, inputs, outputs, initializer=initializers)
    model = helper.make_model(graph, producer_name="denoise-test-fixtures",
                               opset_imports=[helper.make_opsetid("", opset)])
    model.ir_version = 9  # compatible with onnxruntime 1.19.x
    onnx.checker.check_model(model)
    onnx.save(model, out_path)
    print(f"wrote {out_path}")


def gen_identity_denoise(frame_size, out_dir):
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [frame_size])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [frame_size])
    node = helper.make_node("Identity", ["input"], ["output"])
    save("identity_denoise", [node], [inp], [out], [], os.path.join(out_dir, "identity_denoise.onnx"))


def gen_gain_denoise(frame_size, out_dir, gain=0.5):
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [frame_size])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [frame_size])
    gain_init = helper.make_tensor("gain", TensorProto.FLOAT, [], [gain])
    node = helper.make_node("Mul", ["input", "gain"], ["output"])
    save("gain_denoise", [node], [inp], [out], [gain_init], os.path.join(out_dir, "gain_denoise.onnx"))


def gen_constant_vad(frame_size, out_dir):
    # output = mean(abs(input)), shape [1] -- deterministic function of the
    # input so the C++ test can compute the expected value independently.
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [frame_size])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1])
    abs_node = helper.make_node("Abs", ["input"], ["abs_input"])
    mean_node = helper.make_node("ReduceMean", ["abs_input"], ["output"], keepdims=1, axes=[0])
    save("constant_vad", [abs_node, mean_node], [inp], [out], [], os.path.join(out_dir, "mean_abs_vad.onnx"))


def gen_fixed_gain_speaker(frame_size, out_dir, gain=0.25):
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [frame_size])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1])
    gain_init = helper.make_tensor("gain_scalar", TensorProto.FLOAT, [1], [gain])
    node = helper.make_node("Identity", ["gain_scalar"], ["output"])
    save("fixed_gain_speaker", [node], [inp], [out], [gain_init],
         os.path.join(out_dir, "fixed_gain_speaker.onnx"))


def gen_stateful_diff_denoise(frame_size, out_dir):
    # output = input - state_in ; state_out = input
    # First call (state_in == 0) reproduces the input exactly; each
    # subsequent call outputs the difference from the previous frame. This
    # exercises OnnxModel's persisted state_in/state_out feedback loop.
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT, [frame_size])
    state_in = helper.make_tensor_value_info("state_in", TensorProto.FLOAT, [frame_size])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT, [frame_size])
    state_out = helper.make_tensor_value_info("state_out", TensorProto.FLOAT, [frame_size])

    diff_node = helper.make_node("Sub", ["input", "state_in"], ["output"])
    copy_node = helper.make_node("Identity", ["input"], ["state_out"])
    save("stateful_diff_denoise", [diff_node, copy_node], [inp, state_in], [out, state_out], [],
         os.path.join(out_dir, "stateful_diff_denoise.onnx"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", default=os.path.join(os.path.dirname(__file__), "..", "models", "test"))
    parser.add_argument("--frame-size", type=int, default=480)
    args = parser.parse_args()

    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)

    gen_identity_denoise(args.frame_size, out_dir)
    gen_gain_denoise(args.frame_size, out_dir)
    gen_constant_vad(args.frame_size, out_dir)
    gen_fixed_gain_speaker(args.frame_size, out_dir)
    gen_stateful_diff_denoise(args.frame_size, out_dir)


if __name__ == "__main__":
    main()
