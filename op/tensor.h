#pragma once
#include <string>
#include <string_view>
#include <vector>

// Tensor operation opcodes.

namespace kvlang::op {

struct TensorOp {
    std::string opcode;  // e.g. "tensor.add", "tensor.matmul"
    std::vector<std::string> input_paths;
    std::string output_path;
    std::vector<int64_t> shape;
};

} // namespace kvlang::op
