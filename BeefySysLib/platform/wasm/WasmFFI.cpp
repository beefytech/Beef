// A dynamic call for wasm, which has no libffi.
//
// It does not need one. libffi is complicated because ABIs like SysV x86-64 scatter a
// struct's fields across registers, so a call has to be assembled per signature. Wasm does
// not: a call through the function table takes one number per parameter, and the caller
// pushes them. The exported table is the mechanism: wasmTable.get(fp) hands back the
// function and apply() calls it. That needs -sEXPORTED_RUNTIME_METHODS=wasmTable on the
// link, which BuildContext passes for every wasm target.
//
// WHAT THIS COVERS. Everything MethodInfo.Invoke can ask for. corlib decides the calling
// convention before FFILIB sees anything: a struct parameter is either splatted into its
// fields, exactly as the compiler passed it, or handed over as a pointer, and a struct
// return becomes a void call with the destination pointer as a leading argument (on every
// target but aarch64). So for Beef's own methods this layer only ever sees scalars and
// pointers: i32 for Int, the sized integers up to 32 bits and Pointer; f32; f64; and i64.
//
// WHAT IT DOES NOT. A Struct FFIType can only reach here from user code driving
// System.FFI directly against a C function. It is passed as the address of the value,
// which is clang's wasm32 C ABI for aggregates, except that clang passes a struct holding a
// single scalar as that scalar; that case is not special cased here. LongDouble is treated
// as f64, which is wrong for a C long double on wasm32 (an f128, passed indirectly), and no
// Beef type maps to it. Closures (ClosureAlloc) stay unimplemented, as they are on every
// backend. wasm32 only: the slot mapping assumes 32 bit pointers.

#include "WasmFFI.h"

#include <emscripten.h>
#include <stdlib.h>

static_assert(sizeof(void*) == 4, "the wasm FFI slot mapping assumes wasm32 pointers");

namespace
{
	// Mirrors System.FFI.FFIType in corlib. Beef allocates these, so the layout is theirs.
	struct BfWasmFFIType
	{
		intptr_t mSize;
		uint16_t mAlignment;
		uint16_t mTypeKind;
		BfWasmFFIType** mElements;
	};

	// Mirrors System.FFI.FFILIB.FFICIF, which mirrors libffi's ffi_cif field for field. Only
	// the first four are read back; bytes and flags exist to keep the size right.
	struct BfWasmFFICif
	{
		int32_t mAbi;
		uint32_t mNArgs;
		BfWasmFFIType** mArgTypes;
		BfWasmFFIType* mRType;
		uint32_t mBytes;
		uint32_t mFlags;
	};

	// System.FFI.FFIType.TypeKind, in declaration order.
	enum BfWasmFFITypeKind
	{
		BfWasmFFITypeKind_Void = 0,
		BfWasmFFITypeKind_Int,
		BfWasmFFITypeKind_Float,
		BfWasmFFITypeKind_Double,
		BfWasmFFITypeKind_LongDouble,
		BfWasmFFITypeKind_UInt8,
		BfWasmFFITypeKind_SInt8,
		BfWasmFFITypeKind_UInt16,
		BfWasmFFITypeKind_SInt16,
		BfWasmFFITypeKind_UInt32,
		BfWasmFFITypeKind_SInt32,
		BfWasmFFITypeKind_UInt64,
		BfWasmFFITypeKind_SInt64,
		BfWasmFFITypeKind_Struct,
		BfWasmFFITypeKind_Pointer
	};

	// libffi's ffi_status values, which corlib's FFIResult mirrors.
	enum
	{
		BfWasmFFI_OK = 0,
		BfWasmFFI_BadTypeDef = 1
	};

	// What the JS side does with each slot. Everything the wasm ABI passes in an i32
	// collapses to one case, which is most of them.
	enum BfWasmSlotKind
	{
		BfWasmSlot_Void = 0,
		BfWasmSlot_I32,
		BfWasmSlot_F32,
		BfWasmSlot_F64,
		BfWasmSlot_I64,
		BfWasmSlot_Struct
	};

	int32_t BfWasmSlotKindOf(const BfWasmFFIType* type)
	{
		if (type == NULL)
			return BfWasmSlot_Void;

		switch (type->mTypeKind)
		{
		case BfWasmFFITypeKind_Void:
			return BfWasmSlot_Void;
		case BfWasmFFITypeKind_Float:
			return BfWasmSlot_F32;
		case BfWasmFFITypeKind_Double:
		case BfWasmFFITypeKind_LongDouble:
			return BfWasmSlot_F64;
		case BfWasmFFITypeKind_UInt64:
		case BfWasmFFITypeKind_SInt64:
			return BfWasmSlot_I64;
		case BfWasmFFITypeKind_Struct:
			return BfWasmSlot_Struct;
		default:
			// Int, the sized integers and Pointer: all one i32 on wasm32.
			return BfWasmSlot_I32;
		}
	}
}

// args[i] points AT the value, which is why a struct costs nothing here: the address the
// wasm ABI wants for an aggregate is the slot address itself. sret is the destination for a
// struct return, prepended as argument zero, and is null when the return is not a struct.
EM_JS(void, BfWasmFFI_CallJS, (void* funcPtr, int32_t nargs, const int32_t* kinds, void** args,
	void* rvalue, int32_t retKind, void* sret), {
	var fn = wasmTable.get(funcPtr);
	var callArgs = [];
	if (sret !== 0)
		callArgs.push(sret);

	for (var i = 0; i < nargs; i++)
	{
		var kind = HEAP32[(kinds >> 2) + i];
		var slot = HEAPU32[(args >> 2) + i];
		switch (kind)
		{
		case 2: callArgs.push(HEAPF32[slot >> 2]); break;
		case 3: callArgs.push(HEAPF64[slot >> 3]); break;
		case 4:
			// WASM_BIGINT is on by default, so an i64 parameter is a BigInt.
			callArgs.push((BigInt(HEAPU32[slot >> 2]) |
				(BigInt(HEAP32[(slot >> 2) + 1]) << 32n)));
			break;
		case 5: callArgs.push(slot); break; // the struct's address IS the argument
		default: callArgs.push(HEAP32[slot >> 2]); break;
		}
	}

	var result = fn.apply(null, callArgs);
	if (rvalue === 0)
		return;

	switch (retKind)
	{
	case 2: HEAPF32[rvalue >> 2] = result; break;
	case 3: HEAPF64[rvalue >> 3] = result; break;
	case 4:
		HEAP32[rvalue >> 2] = Number(BigInt(result) & 0xFFFFFFFFn) | 0;
		HEAP32[(rvalue >> 2) + 1] = Number(BigInt(result) >> 32n) | 0;
		break;
	case 0: case 5: break; // void, or already written through sret
	default: HEAP32[rvalue >> 2] = result; break;
	}
});

int32_t BfWasmFFI_PrepCif(void* cif, int32_t abi, int32_t nargs, void* rtype, void** argTypes)
{
	// Nothing to compile: without libffi the cif is just the record Call reads back.
	if (cif == NULL)
		return BfWasmFFI_BadTypeDef;

	BfWasmFFICif* wasmCif = (BfWasmFFICif*)cif;
	wasmCif->mAbi = abi;
	wasmCif->mNArgs = (uint32_t)nargs;
	wasmCif->mArgTypes = (BfWasmFFIType**)argTypes;
	wasmCif->mRType = (BfWasmFFIType*)rtype;
	wasmCif->mBytes = 0;
	wasmCif->mFlags = 0;
	return BfWasmFFI_OK;
}

void BfWasmFFI_Call(void* cif, void* funcPtr, void* rvalue, void** args)
{
	if ((cif == NULL) || (funcPtr == NULL))
		return;

	BfWasmFFICif* wasmCif = (BfWasmFFICif*)cif;
	int32_t nargs = (int32_t)wasmCif->mNArgs;

	int32_t stackKinds[16];
	int32_t* kinds = stackKinds;
	if (nargs > (int32_t)(sizeof(stackKinds) / sizeof(stackKinds[0])))
		kinds = (int32_t*)malloc(sizeof(int32_t) * nargs);

	for (int32_t i = 0; i < nargs; i++)
		kinds[i] = BfWasmSlotKindOf(wasmCif->mArgTypes[i]);

	int32_t retKind = BfWasmSlotKindOf(wasmCif->mRType);
	// A struct comes back through a hidden pointer the caller supplies, which is rvalue.
	void* sret = (retKind == BfWasmSlot_Struct) ? rvalue : NULL;

	BfWasmFFI_CallJS(funcPtr, nargs, kinds, args, rvalue, retKind, sret);

	if (kinds != stackKinds)
		free(kinds);
}
