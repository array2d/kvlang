#pragma once
#include "../vtype/vtype.h"

// Tensor value type — identical API to vtype/tensor.go.

namespace kvlang::vtype {

// Register tensor VTypes (scalar, vec, mat, tensor) into the vtype registry.
void register_tensor_types();

} // namespace kvlang::vtype
