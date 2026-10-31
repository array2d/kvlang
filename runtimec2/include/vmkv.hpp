#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "status.hpp"
#include "value.hpp"
#include "vmbin.hpp"

namespace kvlang::runtimec2 {

// PC 路径：/vthread/<vid>/[<depth>]/[<s0>,<s1>]（明文 PC 是外部/持久契约）。
struct Pc {
    std::string vid;
    int depth = 0;
    int s0 = 0;
    int s1 = 0;
};

bool parse_pc(std::string_view pc, Pc* out) noexcept;

struct Vmkv {
    std::string vthread_root(std::string_view vid);
    std::string frame_root(std::string_view vid, int depth);
    std::string entry_pc(std::string_view vid, int depth);
    std::string next_pc(const Pc& pc);
    std::string pc_key_for(const Pc& pc);
    std::string instr_key(const Pc& pc);
    std::string param_key(std::string_view frame_root, int s0, int s1);
    std::string fold_op(std::string_view op);

    bool resolve_desc(vmbin::Frame& frame, const std::string& name,
                      std::string* key, std::string* err);
    bool reg_slot(vmbin::Frame& frame, std::uint8_t index,
                  kvspaceXvalueBodyView_t* out);
    bool reg_body(vmbin::Frame& frame, std::uint8_t index, std::uint8_t** body,
                  std::uint64_t* cap);
    bool slot_read_scalar(const kvspaceXvalueBodyView_t& view,
                          std::string_view langtype, Scalar* out);
    bool ensure_reg_key(vmbin::Frame& frame, vmbin::Register& reg);
    bool reg_langtype(vmbin::Frame& frame, std::uint8_t index, std::string* langtype);
    bool read_reg(vmbin::Frame& frame, std::uint8_t index, Wire* out);
    bool read_scalar(vmbin::Frame& frame, std::uint8_t index, Scalar* out);
    bool read_i64(vmbin::Frame& frame, std::uint8_t index, std::int64_t* out);
    bool read_bool(vmbin::Frame& frame, std::uint8_t index, bool* out);
    void flush_frame_locals(vmbin::Frame& frame);
    void invalidate_path(vmbin::Frame& frame, std::string_view path);
    bool write_reg(vmbin::Frame& frame, std::uint8_t index,
                   const std::uint8_t* data, std::uint32_t len);
    bool slot_write_i64(vmbin::Frame& frame, std::uint8_t index, std::int64_t value);
    bool write_member_scalar(vmbin::Frame& frame, std::uint8_t index,
                             const Scalar& scalar, std::uint8_t width);
    bool store_i64_slot(vmbin::Frame& frame, vmbin::Register& reg,
                        std::int64_t value);
    bool slot_write_bool(vmbin::Frame& frame, std::uint8_t index, bool value);
    bool slot_write_scalar(vmbin::Frame& frame, std::uint8_t index,
                           const Scalar& scalar, std::uint8_t width);
    void sync_fast_after_write(vmbin::Frame& frame, std::uint8_t index,
                               const Scalar& scalar);
    bool is_frame_local(const vmbin::Frame& frame, const std::string& key);
    const char* kind_name(Scalar::K kind, std::uint8_t width);
    bool write_named_i64(void* handle, std::uint8_t index, vmbin::Frame& frame,
                         const char* kind, std::int64_t value);

    bool truthy(const Scalar& scalar);
    bool is_cmp(rwir::Op op);
    bool same_class(Scalar::K left, Scalar::K right);
    bool compute_int(rwir::Op op, Scalar::K kind, const Scalar& left,
                     const Scalar& right, Scalar* result);
    bool compute_float(rwir::Op op, double left, double right, double* result);
    bool compare(rwir::Op op, Scalar::K kind, const Scalar& left,
                 const Scalar& right, bool* out);

    bool enter_frame(vmbin::Frame& frame, const vmbin::Program* program,
                     std::string* err);
    vmbin::Outcome run_frame(vmbin::Frame& frame, int start_s0,
                             std::string* stop_pc, std::string* err);
    vmbin::Outcome do_call(vmbin::Frame& frame, const vmbin::Program* program,
                           const vmbin::Instruction& instruction,
                           std::string* stop_pc, std::string* err);

    vmbin::State vmbin;
};

bool set_value_typed(void* handle, const char* key, const std::uint8_t* value,
                     std::uint32_t value_len, const char* langtype, char* err,
                     std::uint32_t err_cap);

extern Vmkv g_vmkv;

}  // namespace kvlang::runtimec2
