#pragma once
#include <string>
#include <string_view>

// KV path constants — single source of truth for all KV tree paths.
// Keep in sync with keytree/const.go, keytree/const.rs.

namespace kvlang::keytree {

// ── /sys ──────────────────────────────────────────
inline constexpr std::string_view SysRoot     = "/sys";
inline constexpr std::string_view SysVM       = "/sys/vm";
inline constexpr std::string_view SysVT       = "/sys/vthread";
inline constexpr std::string_view SysLib      = "/sys/lib";

// ── /lib ──────────────────────────────────────────
inline constexpr std::string_view LibRoot     = "/lib";

// ── /vthread ──────────────────────────────────────
inline constexpr std::string_view VTRoot      = "/vthread";

// ── Frame keys ────────────────────────────────────
inline constexpr std::string_view FramePC     = ".pc";
inline constexpr std::string_view FrameStatus = ".status";
inline constexpr std::string_view FrameRetVal = ".retval";
inline constexpr std::string_view FrameErr    = ".err";
inline constexpr std::string_view FrameDebug  = ".debugger";
inline constexpr std::string_view FrameX      = ".x";        // local variable prefix
inline constexpr std::string_view FrameRParam = ".rparam";   // read params
inline constexpr std::string_view FrameWParam = ".wparam";   // write params

// Path builder helpers
std::string vt_path(std::string_view vtid);
std::string vt_pc(std::string_view vtid);
std::string vt_status(std::string_view vtid);
std::string lib_func(std::string_view pkg, std::string_view name);
std::string lib_func_src(std::string_view pkg, std::string_view name);
std::string frame_local(std::string_view frame_root, std::string_view slot);
std::string frame_rparam(std::string_view frame_root, std::string_view name);
std::string frame_wparam(std::string_view frame_root, std::string_view name);

} // namespace kvlang::keytree
