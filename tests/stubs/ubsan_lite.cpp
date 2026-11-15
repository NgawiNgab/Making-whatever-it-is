// Host-build no-op definitions for the UBSan handlers zig's libc++ references.
//
// zig's prebuilt libc++ is compiled with undefined-behaviour checks, so it expects
// these handlers to exist. A host test does not care whether they fire.
extern "C" {
void __ubsan_handle_add_overflow(void*, void*, void*) {}
void __ubsan_handle_alignment_assumption(void*, void*, void*, void*) {}
void __ubsan_handle_builtin_unreachable(void*, void*) {}
void __ubsan_handle_divrem_overflow(void*, void*, void*) {}
void __ubsan_handle_float_cast_overflow(void*, void*, void*) {}
void __ubsan_handle_invalid_builtin(void*, void*) {}
void __ubsan_handle_load_invalid_value(void*, void*, void*) {}
void __ubsan_handle_mul_overflow(void*, void*, void*) {}
void __ubsan_handle_negate_overflow(void*, void*, void*) {}
void __ubsan_handle_nonnull_arg(void*, void*) {}
void __ubsan_handle_out_of_bounds(void*, void*, void*) {}
void __ubsan_handle_pointer_overflow(void*, void*, void*, void*) {}
void __ubsan_handle_shift_out_of_bounds(void*, void*, void*, void*) {}
void __ubsan_handle_sub_overflow(void*, void*, void*) {}
void __ubsan_handle_type_mismatch_v1(void*, void*, void*) {}
}
