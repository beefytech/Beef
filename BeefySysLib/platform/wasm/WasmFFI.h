#pragma once

// A dynamic call for wasm, standing in for libffi in FFILIB when the runtime is built with
// BF_DISABLE_FFI under emscripten. See WasmFFI.cpp for the ABI it implements and its limits.

#include <stdint.h>

// Fills the cif record that BfWasmFFI_Call reads back. `cif` is corlib's FFILIB.FFICIF,
// which mirrors libffi's ffi_cif field for field; `rtype` and `argTypes` are corlib's
// FFIType. Returns libffi's FFI_OK (0) or FFI_BAD_TYPEDEF (1).
int32_t BfWasmFFI_PrepCif(void* cif, int32_t abi, int32_t nargs, void* rtype, void** argTypes);

// Calls `funcPtr` through the wasm function table with the arguments `args`, each a pointer
// to its value, and writes the result to `rvalue` (null for a void call).
void BfWasmFFI_Call(void* cif, void* funcPtr, void* rvalue, void** args);
