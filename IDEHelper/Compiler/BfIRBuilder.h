#pragma once

//#define BFIR_RENTRY_CHECK

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/ChunkedDataBuffer.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BfAstAllocator.h"
#include <unordered_map>

//#include "BfSystem.h"

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"

#pragma warning(pop)

NS_BF_BEGIN

typedef int addr_ce;

class BfModule;
class BfType;
class BfTypeInstance;
class BfIRConstHolder;
class BfIRCodeGen;
class BeIRCodeGen;
class BfMethodInstance;
class BfFieldInstance;
class BfMethodRef;

class BfFileInstance;
class BfParser;
class BfParserData;
class Val128;

class BfFilePosition
{
public:
	BfFileInstance* mFileInstance;
	int mCurSrcPos;
	int mCurLine;
	int mCurColumn;

public:
	BfFilePosition()
	{
		mFileInstance = NULL;
		mCurSrcPos = 0;
		mCurLine = 0;
		mCurColumn = 0;
	}
};

enum BfTypeCode : uint8
{
	BfTypeCode_None,
	BfTypeCode_CharPtr,
	BfTypeCode_StringId,
	BfTypeCode_Pointer,
	BfTypeCode_NullPtr,
	BfTypeCode_Self,
	BfTypeCode_Dot,
	BfTypeCode_Var,
	BfTypeCode_Let,
	BfTypeCode_Boolean,
	BfTypeCode_Int8,
	BfTypeCode_UInt8,
	BfTypeCode_Int16,
	BfTypeCode_UInt16,
	BfTypeCode_Int24,
	BfTypeCode_UInt24,
	BfTypeCode_Int32,
	BfTypeCode_UInt32,
	BfTypeCode_Int40,
	BfTypeCode_UInt40,
	BfTypeCode_Int48,
	BfTypeCode_UInt48,
	BfTypeCode_Int56,
	BfTypeCode_UInt56,
	BfTypeCode_Int64,
	BfTypeCode_UInt64,
	BfTypeCode_Int128,
	BfTypeCode_UInt128,
	BfTypeCode_IntPtr,
	BfTypeCode_UIntPtr,
	BfTypeCode_IntUnknown,
	BfTypeCode_UIntUnknown,
	BfTypeCode_Char8,
	BfTypeCode_Char16,
	BfTypeCode_Char32,
	BfTypeCode_Float,
	BfTypeCode_Double,
	BfTypeCode_Float2,
	BfTypeCode_Object,
	BfTypeCode_Interface,
	BfTypeCode_Struct,
	BfTypeCode_Enum,
	BfTypeCode_TypeAlias,
	BfTypeCode_Extension,
	BfTypeCode_FloatX2,
	BfTypeCode_FloatX3,
	BfTypeCode_FloatX4,
	BfTypeCode_DoubleX2,
	BfTypeCode_DoubleX3,
	BfTypeCode_DoubleX4,
	BfTypeCode_Int64X2,
	BfTypeCode_Int64X3,
	BfTypeCode_Int64X4,
	BfTypeCode_Length
};

enum BfConstType
{
	BfConstType_GlobalVar = BfTypeCode_Length,
	BfConstType_BitCast,
	BfConstType_BitCastNull,
	BfConstType_GEP32_1,
	BfConstType_GEP32_2,
	BfConstType_ExtractValue,
	BfConstType_PtrToInt,
	BfConstType_IntToPtr,
	BfConstType_TypeOf,
	BfConstType_TypeOf_WithData,
	BfConstType_AggZero,
	BfConstType_Agg,
	BfConstType_AggCE,
	BfConstType_ArrayZero,
	BfConstType_ArrayZero8,
	BfConstType_Undef,
	BfConstType_SizedArrayType,
	BfConstType_Box
};

enum BfIRValueFlags : uint8
{
	BfIRValueFlags_None,
	BfIRValueFlags_Value = 1,
	BfIRValueFlags_Arg = 2,
	BfIRValueFlags_Const = 4,
	BfIRValueFlags_FromLLVM = 8,
	BfIRValueFlags_Block = 16,
	BfIRValueFlags_Func = 32
};

enum BfIRCmd : uint8
{
	BfIRCmd_Module_Start,
	BfIRCmd_Module_SetTargetTriple,
	BfIRCmd_Module_AddModuleFlag,
	BfIRCmd_WriteIR,

	BfIRCmd_SetType,
	BfIRCmd_SetInstType,
	BfIRCmd_PrimitiveType,
	BfIRCmd_CreateAnonymousStruct,
	BfIRCmd_CreateStruct,
	BfIRCmd_StructSetBody,
	BfIRCmd_Type,
	BfIRCmd_TypeInst,
	BfIRCmd_TypeInstPtr,
	BfIRCmd_GetType,
	BfIRCmd_GetPointerToFuncType,
	BfIRCmd_GetPointerToType,
	BfIRCmd_GetSizedArrayType,
	BfIRCmd_GetVectorType,

	BfIRCmd_CreateConstStructZero,
	BfIRCmd_CreateConstAgg,
	BfIRCmd_CreateConstArrayZero,
	BfIRCmd_CreateConstString,
	BfIRCmd_ConfigConst,

	BfIRCmd_SetName,
	BfIRCmd_CreateUndefValue,
	BfIRCmd_NumericCast,
	BfIRCmd_CmpEQ,
	BfIRCmd_CmpNE,
	BfIRCmd_CmpSLT,
	BfIRCmd_CmpULT,
	BfIRCmd_CmpSLE,
	BfIRCmd_CmpULE,
	BfIRCmd_CmpSGT,
	BfIRCmd_CmpUGT,
	BfIRCmd_CmpSGE,
	BfIRCmd_CmpUGE,
	BfIRCmd_Add,
	BfIRCmd_Sub,
	BfIRCmd_Mul,
	BfIRCmd_SDiv,
	BfIRCmd_UDiv,
	BfIRCmd_SRem,
	BfIRCmd_URem,
	BfIRCmd_And,
	BfIRCmd_Or,
	BfIRCmd_Xor,
	BfIRCmd_Shl,
	BfIRCmd_AShr,
	BfIRCmd_LShr,
	BfIRCmd_Neg,
	BfIRCmd_Not,
	BfIRCmd_BitCast,
	BfIRCmd_PtrToInt,
	BfIRCmd_IntToPtr,
	BfIRCmd_InboundsGEP1_32,
	BfIRCmd_InboundsGEP2_32,
	BfIRCmd_InBoundsGEP1,
	BfIRCmd_InBoundsGEP2,
	BfIRCmd_IsNull,
	BfIRCmd_IsNotNull,
	BfIRCmd_ExtractValue,
	BfIRCmd_InsertValue,

	BfIRCmd_Alloca,
	BfIRCmd_AllocaArray,
	BfIRCmd_SetAllocaAlignment,
	BfIRCmd_SetAllocaNoChkStkHint,
	BfIRCmd_SetAllocaForceMem,
	BfIRCmd_AliasValue,
	BfIRCmd_LifetimeStart,
	BfIRCmd_LifetimeEnd,
	BfIRCmd_LifetimeSoftEnd,
	BfIRCmd_LifetimeExtend,
	BfIRCmd_ValueScopeStart,
	BfIRCmd_ValueScopeRetain,
	BfIRCmd_ValueScopeSoftEnd,
	BfIRCmd_ValueScopeHardEnd,
	BfIRCmd_Load,
	BfIRCmd_AlignedLoad,
	BfIRCmd_Store,
	BfIRCmd_AlignedStore,
	BfIRCmd_MemSet,
	BfIRCmd_Fence,
	BfIRCmd_StackSave,
	BfIRCmd_StackRestore,

	BfIRCmd_GlobalVariable,
	BfIRCmd_GlobalVar_SetUnnamedAddr,
	BfIRCmd_GlobalVar_SetInitializer,
	BfIRCmd_GlobalVar_SetAlignment,
	BfIRCmd_GlobalVar_SetStorageKind,
	BfIRCmd_GlobalStringPtr,
	BfIRCmd_SetReflectTypeData,

	BfIRCmd_CreateBlock,
	BfIRCmd_MaybeChainNewBlock,
	BfIRCmd_AddBlock,
	BfIRCmd_DropBlocks,
	BfIRCmd_MergeBlockDown,
	BfIRCmd_GetInsertBlock,
	BfIRCmd_SetInsertPoint,
	BfIRCmd_SetInsertPointAtStart,
	BfIRCmd_EraseFromParent,
	BfIRCmd_DeleteBlock,
	BfIRCmd_EraseInstFromParent,
	BfIRCmd_CreateBr,
	BfIRCmd_CreateBr_Fake,
	BfIRCmd_CreateBr_NoCollapse,
	BfIRCmd_CreateCondBr,
	BfIRCmd_MoveBlockToEnd,

	BfIRCmd_CreateSwitch,
	BfIRCmd_AddSwitchCase,
	BfIRCmd_SetSwitchDefaultDest,
	BfIRCmd_CreatePhi,
	BfIRCmd_AddPhiIncoming,

	BfIRCmd_GetIntrinsic,
	BfIRCmd_CreateFunctionType,
	BfIRCmd_CreateFunction,
	BfIRCmd_SetFunctionName,
	BfIRCmd_EnsureFunctionPatchable,
	BfIRCmd_RemapBindFunction,
	BfIRCmd_SetActiveFunction,
	BfIRCmd_CreateCall,
	BfIRCmd_SetCallCallingConv,
	BfIRCmd_SetFuncCallingConv,
	BfIRCmd_SetTailCall,
	BfIRCmd_SetCallAttribute,
	BfIRCmd_CreateRet,
	BfIRCmd_CreateSetRet,
	BfIRCmd_CreateRetVoid,
	BfIRCmd_CreateUnreachable,
	BfIRCmd_Call_AddAttribute,
	BfIRCmd_Call_AddAttribute1,
	BfIRCmd_Func_AddAttribute,
	BfIRCmd_Func_AddAttribute1,
	BfIRCmd_Func_SetParamName,
	BfIRCmd_Func_DeleteBody,
	BfIRCmd_Func_SafeRename,
	BfIRCmd_Func_SafeRenameFrom,
	BfIRCmd_Func_SetLinkage,

	BfIRCmd_Comptime_Error,
	BfIRCmd_Comptime_GetBfType,
	BfIRCmd_Comptime_GetReflectType,
	BfIRCmd_Comptime_DynamicCastCheck,
	BfIRCmd_Comptime_GetVirtualFunc,
	BfIRCmd_Comptime_GetInterfaceFunc,

	BfIRCmd_SaveDebugLocation,
	BfIRCmd_RestoreDebugLocation,
	BfIRCmd_DupDebugLocation,
	BfIRCmd_ClearDebugLocation,
	BfIRCmd_ClearDebugLocationInst,
	BfIRCmd_ClearDebugLocationInstLast,
	BfIRCmd_UpdateDebugLocation,
	BfIRCmd_SetCurrentDebugLocation,
	BfIRCmd_Nop,
	BfIRCmd_EnsureInstructionAt,
	BfIRCmd_StatementStart,
	BfIRCmd_ObjectAccessCheck,

	BfIRCmd_DbgInit,
	BfIRCmd_DbgFinalize,
	BfIRCmd_DbgCreateCompileUnit,
	BfIRCmd_DbgCreateFile,
	BfIRCmd_ConstValueI64,
	BfIRCmd_DbgGetCurrentLocation,
	BfIRCmd_DbgSetType,
	BfIRCmd_DbgSetInstType,
	BfIRCmd_DbgGetType,
	BfIRCmd_DbgGetTypeInst,
	BfIRCmd_DbgTrackDITypes,
	BfIRCmd_DbgCreateNamespace,
	BfIRCmd_DbgCreateImportedModule,
	BfIRCmd_DbgCreateBasicType,
	BfIRCmd_DbgCreateStructType,
	BfIRCmd_DbgCreateEnumerationType,
	BfIRCmd_DbgCreatePointerType,
	BfIRCmd_DbgCreateReferenceType,
	BfIRCmd_DbgCreateConstType,
	BfIRCmd_DbgCreateArtificialType,
	BfIRCmd_DbgCreateArrayType,
	BfIRCmd_DbgCreateReplaceableCompositeType,
	BfIRCmd_DbgCreateForwardDecl,
	BfIRCmd_DbgCreateSizedForwardDecl,
	BeIRCmd_DbgSetTypeSize,
	BfIRCmd_DbgReplaceAllUses,
	BfIRCmd_DbgDeleteTemporary,
	BfIRCmd_DbgMakePermanent,
	BfIRCmd_CreateEnumerator,
	BfIRCmd_DbgCreateMemberType,
	BfIRCmd_DbgStaticCreateMemberType,
	BfIRCmd_DbgCreateInheritance,
	BfIRCmd_DbgCreateMethod,
	BfIRCmd_DbgCreateFunction,
	BfIRCmd_DbgCreateParameterVariable,
	BfIRCmd_DbgCreateSubroutineType,
	BfIRCmd_DbgCreateAutoVariable,
	BfIRCmd_DbgInsertValueIntrinsic,
	BfIRCmd_DbgInsertDeclare,
	BfIRCmd_DbgLifetimeEnd,
	BfIRCmd_DbgCreateGlobalVariable,
	BfIRCmd_DbgCreateLexicalBlock,
	BfIRCmd_DbgCreateLexicalBlockFile,
	BfIRCmd_DbgCreateAnnotation,

	BfIRCmd_COUNT
};

enum BfIRCallingConv
{
	BfIRCallingConv_ThisCall,
	BfIRCallingConv_StdCall,
	BfIRCallingConv_CDecl,
	BfIRCallingConv_FastCall
};

enum BfIRParamType : uint8
{
	BfIRParamType_None,
	BfIRParamType_Const,
	BfIRParamType_Arg,
	BfIRParamType_StreamId_Abs8,
	BfIRParamType_StreamId_Rel,
	BfIRParamType_StreamId_Back1,
	BfIRParamType_StreamId_Back_LAST = 0xFF, // Use remaining encoding
};

enum BfIRLinkageType : uint8
{
	BfIRLinkageType_External,
	BfIRLinkageType_Internal
};

enum BfIRInitType : uint8
{
	BfIRInitType_NotSet, // Not specified
	BfIRInitType_NotNeeded, // Explicitly disable
	BfIRInitType_NotNeeded_AliveOnDecl, // Explicitly disable, treat variable as alive at declaration
	BfIRInitType_Uninitialized, // May set to 0xCC for debug
	BfIRInitType_Zero, // Must be zero
};

enum BfIRFenceType : uint8
{
	BfIRFenceType_AcquireRelease
};

enum BfIRConfigConst : uint8
{
	BfIRConfigConst_VirtualMethodOfs,
	BfIRConfigConst_DynSlotOfs
};

enum BfIRIntrinsic : uint8
{
	BfIRIntrinsic__PLATFORM,
	BfIRIntrinsic_Abs,
	BfIRIntrinsic_Add,
	BfIRIntrinsic_And,
	BfIRIntrinsic_AtomicAdd,
	BfIRIntrinsic_AtomicAnd,
	BfIRIntrinsic_AtomicCmpStore,
	BfIRIntrinsic_AtomicCmpStore_Weak,
	BfIRIntrinsic_AtomicCmpXChg,
	BfIRIntrinsic_AtomicFence,
	BfIRIntrinsic_AtomicLoad,
	BfIRIntrinsic_AtomicMax,
	BfIRIntrinsic_AtomicMin,
	BfIRIntrinsic_AtomicNAnd,
	BfIRIntrinsic_AtomicOr,
	BfIRIntrinsic_AtomicStore,
	BfIRIntrinsic_AtomicSub,
	BfIRIntrinsic_AtomicUMax,
	BfIRIntrinsic_AtomicUMin,
	BfIRIntrinsic_AtomicXChg,
	BfIRIntrinsic_AtomicXor,
	BfIRIntrinsic_BSwap,
	BfIRIntrinsic_Cast,
	BfIRIntrinsic_Cos,
	BfIRIntrinsic_Cpuid,
	BfIRIntrinsic_DebugTrap,
	BfIRIntrinsic_Div,
	BfIRIntrinsic_Eq,
	BfIRIntrinsic_Floor,
	BfIRIntrinsic_Free,
	BfIRIntrinsic_Gt,
	BfIRIntrinsic_GtE,
	BfIRIntrinsic_Index,
	BfIRIntrinsic_Log,
	BfIRIntrinsic_Log10,
	BfIRIntrinsic_Log2,
	BfIRIntrinsic_Lt,
	BfIRIntrinsic_LtE,
	BfIRIntrinsic_Malloc,
	BfIRIntrinsic_Max,
	BfIRIntrinsic_MemCpy,
	BfIRIntrinsic_MemMove,
	BfIRIntrinsic_MemSet,
	BfIRIntrinsic_Min,
	BfIRIntrinsic_Mod,
	BfIRIntrinsic_Mul,
	BfIRIntrinsic_Neq,
	BfIRIntrinsic_Not,
	BfIRIntrinsic_Or,
	BfIRIntrinsic_Pow,
	BfIRIntrinsic_PowI,
	BfIRIntrinsic_ReturnAddress,
	BfIRIntrinsic_Round,
	BfIRIntrinsic_SAR,
	BfIRIntrinsic_SHL,
	BfIRIntrinsic_SHR,
	BfIRIntrinsic_Shuffle,
	BfIRIntrinsic_Sin,
	BfIRIntrinsic_Sqrt,
	BfIRIntrinsic_Sub,
	BfIRIntrinsic_VAArg,
	BfIRIntrinsic_VAEnd,
	BfIRIntrinsic_VAStart,
	BfIRIntrinsic_Xgetbv,
	BfIRIntrinsic_Xor,

	BfIRIntrinsic_COUNT,
	BfIRIntrinsic_Atomic_FIRST = BfIRIntrinsic_AtomicAdd,
	BfIRIntrinsic_Atomic_LAST = BfIRIntrinsic_AtomicXor
};

enum BfIRAtomicOrdering : uint8
{
	BfIRAtomicOrdering_Unordered,
	BfIRAtomicOrdering_Relaxed,
	BfIRAtomicOrdering_Acquire,
	BfIRAtomicOrdering_Release,
	BfIRAtomicOrdering_AcqRel,
	BfIRAtomicOrdering_SeqCst,
	BfIRAtomicOrdering_ORDERMASK = 7,

	BfIRAtomicOrdering_Volatile = 8,
	BfIRAtomicOrdering_ReturnModified = 0x10 // Generally atomic instructions return original value, this overrides that
};

enum BfIRStorageKind : uint8
{
	BfIRStorageKind_Normal,
	BfIRStorageKind_Import,
	BfIRStorageKind_Export
};

//#define CHECK_CONSTHOLDER

struct BfIRRawValue
{
public:
	int mId;

	BfIRRawValue()
	{
		mId = -1;
	}

	operator bool() const
	{
		return mId != -1;
	}

	bool IsFake() const
	{
		return mId < -1;
	}
};

struct BfIRValue
{
public:
	// Reserved 'fake' Ids
	enum
	{
		ID_IMPLICIT = -3
	};

public:
	int mId;
	BfIRValueFlags mFlags;
	static BfIRValue sValueless;

#ifdef CHECK_CONSTHOLDER
	BfIRConstHolder* mHolder;
#endif

public:
	BfIRValue()
	{
		mId = -1;
		mFlags = BfIRValueFlags_None;
#ifdef CHECK_CONSTHOLDER
		mHolder = NULL;
#endif
	}

	BfIRValue(const BfIRValue& from)
	{
		mFlags = from.mFlags;
		mId = from.mId;
#ifdef CHECK_CONSTHOLDER
		mHolder = from.mHolder;
#endif
	}

	BfIRValue(BfIRValueFlags flags, int id)
	{
		mFlags = flags;
		mId = id;
	}

	operator bool() const
	{
		return mFlags != BfIRValueFlags_None;
	}

	bool IsFake() const;
	bool IsConst() const;
	bool IsArg() const;
	bool IsFromLLVM() const;

	bool operator==(const BfIRValue& rhs) const
	{
		if (mFlags != rhs.mFlags)
			return false;
		if (mId != rhs.mId)
			return false;
		return true;
	}

	bool operator!=(const BfIRValue& rhs) const
	{
		if (mFlags != rhs.mFlags)
			return true;
		if (mId != rhs.mId)
			return true;
		return false;
	}
};

struct BfIRTypeData
{
	enum TypeKind
	{
		TypeKind_None,
		TypeKind_TypeId,
		TypeKind_TypeCode,
		TypeKind_TypeInstId,
		TypeKind_TypeInstPtrId,
		TypeKind_Stream,
		TypeKind_SizedArray
	};

	TypeKind mKind;
	int mId;

	operator bool()
	{
		return (mId != -1);
	}
};

struct BfIRType : public BfIRTypeData
{
public:
	BfIRType()
	{
		mKind = TypeKind_None;
		mId = -1;
	}

	BfIRType(BfIRTypeData typeData)
	{
		mKind = typeData.mKind;
		mId = typeData.mId;
	}

	BfIRType(const BfIRValue& val) { mKind = TypeKind_Stream; mId = val.mId; }
};

struct BfIRBlock : public BfIRValue
{
public:
	BfIRBlock();
	BfIRBlock(const BfIRValue& fromVal) : BfIRValue(fromVal) {}
};

enum BfIRAttribute
{
	BfIRAttribute_NoReturn,
	BfIRAttribute_NoAlias,
	BfIRAttribute_NoCapture,
	BfIRAttribute_StructRet,
	BfIRAttribute_VarRet,
	BfIRAttribute_ZExt,
	BfIRAttribute_ByVal,
	BfIRAttribute_Dereferencable,
	BFIRAttribute_NoUnwind,
	BFIRAttribute_UWTable,
	BFIRAttribute_AlwaysInline,
	BFIRAttribute_NoFramePointerElim,
	BFIRAttribute_DllImport,
	BFIRAttribute_DllExport,
	BFIRAttribute_NoRecurse,
	BFIRAttribute_Constructor,
	BFIRAttribute_Destructor,
};

struct BfIRFunctionType
{
public:
	int mId;

public:
	BfIRFunctionType();
	BfIRFunctionType(const BfIRValue& val) { mId = val.mId; }
	operator bool() const
	{
		return mId != -1;
	}
};

struct BfIRFunction : public BfIRRawValue
{
public:
	BfIRFunction();
	BfIRFunction(const BfIRValue& val)
	{
		BF_ASSERT((val.mFlags == BfIRValueFlags_None) || (val.mFlags == BfIRValueFlags_Value));
		mId = val.mId;
	}
		//: BfIRValue(val) {}

	bool operator==(const BfIRFunction& rhs) const
	{
		if (mId != rhs.mId)
			return false;
		return true;
	}

	bool operator!=(const BfIRFunction& rhs) const
	{
		if (mId == rhs.mId)
			return false;
		return true;
	}

	operator bool() const
	{
		return mId != -1;
	}

	operator BfIRValue() const
	{
		return BfIRValue((mId == -1) ? BfIRValueFlags_None : BfIRValueFlags_Value, mId);
	}
};

struct BfIRMDNode
{
public:
	int mId;

	BfIRMDNode()
	{
		mId = -1;
	}

	BfIRMDNode(const BfIRValue& val)
	{
		mId = val.mId;
	}

	operator bool() const
	{
		return mId != -1;
	}

	bool operator==(const BfIRMDNode& rhs) const
	{
		if (mId != rhs.mId)
			return false;
		return true;
	}

	bool operator!=(const BfIRMDNode& rhs) const
	{
		if (mId == rhs.mId)
			return false;
		return true;
	}
};

class BfFileInstance
{
public:
	BfParserData* mParser;
	BfIRMDNode mDIFile;
	BfFilePosition mPrevPosition;

public:
	BfFileInstance()
	{
		mParser = NULL;
	}
};

struct BfGlobalVar
{
	BfConstType mConstType;
	const char* mName;
	BfIRType mType;
	bool mIsConst;
	BfIRLinkageType mLinkageType;
	int mStreamId;
	BfIRValue mInitializer;
	bool mIsTLS;
	int mAlignment;
};

struct BfGlobalVar_TypeInst
{
	BfConstType mConstType;
	const char* mName;
	BfTypeInstance* mTypeInst;
	bool mIsConst;
	BfIRLinkageType mLinkageType;
	int mStreamId;
	BfIRValue mInitializer;
	bool mIsTLS;
};

struct BfTypeOf_Const
{
	BfConstType mConstType;
	BfType* mType;
	BfIRValue mTypeData;
};

struct BfTypeOf_WithData_Const
{
	BfConstType mConstType;
	BfType* mType;
	BfIRValue mTypeData;
};

struct BfConstant
{
public:
	union
	{
		BfTypeCode mTypeCode;
		BfConstType mConstType;
	};
	union
	{
		bool mBool;
		int64 mInt64;
		int32 mInt32;
		int16 mInt16;
		int8 mInt8;
		uint64 mUInt64;
		uint32 mUInt32;
		uint16 mUInt16;
		uint8 mUInt8;
		uint8 mChar;
		uint32 mChar32;
		double mDouble;
		BfIRTypeData mIRType;
	};

	bool IsNull()
	{
		if (mTypeCode == BfTypeCode_NullPtr)
			return true;
		if (mConstType == BfConstType_BitCastNull)
			return true;
		return false;
	}
};

struct BfConstantSizedArrayType
{
	BfConstType mConstType;
	BfIRType mType;
	intptr mLength;
};

struct BfConstantUndef
{
	BfConstType mConstType;
	BfIRType mType;
};

struct BfConstantBitCast
{
	BfConstType mConstType;
	int mTarget;
	BfIRType mToType;
};

struct BfConstantBox
{
	BfConstType mConstType;
	int mTarget;
	BfIRType mToType;
};

struct BfConstantPtrToInt
{
	BfConstType mConstType;
	int mTarget;
	BfTypeCode mToTypeCode;
};

struct BfConstantIntToPtr
{
	BfConstType mConstType;
	int mTarget;
	BfIRType mToType;
};

struct BfConstantGEP32_1
{
	BfConstType mConstType;
	int mTarget;
	int mIdx0;
};

struct BfConstantGEP32_2
{
	BfConstType mConstType;
	int mTarget;
	int mIdx0;
	int mIdx1;
};

struct BfConstantExtractValue
{
	BfConstType mConstType;
	int mTarget;
	int mIdx0;
};

struct BfConstantAgg
{
	BfConstType mConstType;
	BfIRType mType;
	BfSizedArray<BfIRValue> mValues;
};

struct BfConstantAggCE
{
	BfConstType mConstType;
	BfIRType mType;
	addr_ce mCEAddr;
};

struct BfConstantArrayZero
{
	BfConstType mConstType;
	BfIRType mType;
	int mCount;
};

class BfIRConstHolder
{
public:
	BumpAllocatorT<256> mTempAlloc;
	BfModule* mModule;
	Dictionary<String, BfIRValue> mGlobalVarMap;

public:
	void FixTypeCode(BfTypeCode& typeCode);
	int GetSize(BfTypeCode typeCode);
	static int GetSize(BfTypeCode typeCode, int ptrSize);
	static bool IsInt(BfTypeCode typeCode);
	static bool IsChar(BfTypeCode typeCode);
	static bool IsIntable(BfTypeCode typeCode);
	static bool IsSigned(BfTypeCode typeCode);
	static bool IsFloat(BfTypeCode typeCode);
	const char* AllocStr(const StringImpl& str);

public:
	BfIRConstHolder(BfModule* module);
	virtual ~BfIRConstHolder();

	String ToString(BfIRValue irValue);
	String ToString(BfIRType irType);
	void pv(const BfIRValue& irValue);

	BfConstant* GetConstantById(int id);
	BfConstant* GetConstant(BfIRValue id);
	bool TryGetBool(BfIRValue id, bool& boolVal);
	int IsZero(BfIRValue val);
	bool IsConstValue(BfIRValue val);
	int CheckConstEquality(BfIRValue lhs, BfIRValue rhs); // -1 = fail, 0 = false, 1 = true
	//void WriteConstant(void* data, BeConstant* constVal);

	BfIRType GetSizedArrayType(BfIRType elementType, int length);

	BfIRValue CreateConst(BfTypeCode typeCode, uint64 val);
	BfIRValue CreateConst(BfTypeCode typeCode, int val);
	BfIRValue CreateConst(BfTypeCode typeCode, double val);
	BfIRValue CreateConst(BfConstant* fromConst, BfIRConstHolder* fromHolder);
	BfIRValue CreateConstNull();
	BfIRValue CreateConstNull(BfIRType nullType);
	BfIRValue CreateConstAggZero(BfIRType aggType);
	BfIRValue CreateConstAgg(BfIRType type, const BfSizedArray<BfIRValue>& values);
	BfIRValue CreateConstAggCE(BfIRType type, addr_ce ptr);
	BfIRValue CreateConstArrayZero(BfIRType type, int count);
	BfIRValue CreateConstArrayZero(int count);
	BfIRValue CreateConstBitCast(BfIRValue val, BfIRType type);
	BfIRValue CreateConstBox(BfIRValue val, BfIRType type);
	BfIRValue CreateTypeOf(BfType* type);
	BfIRValue CreateTypeOf(BfType* type, BfIRValue typeData);
	BfIRValue GetUndefConstValue(BfIRType type);
	BfIRValue CreateGlobalVariableConstant(BfIRType varType, bool isConstant, BfIRLinkageType linkageType, BfIRValue initializer, const StringImpl& name, bool isTLS = false);

	bool WriteConstant(BfIRValue val, void* ptr, BfType* type);
	BfIRValue ReadConstant(void* ptr, BfType* type);
};

enum BfIRPopulateType
{
	BfIRPopulateType_Identity,
	BfIRPopulateType_Declaration,
	BfIRPopulateType_Eventually_Full,
	BfIRPopulateType_Full,
	BfIRPopulateType_Full_ForceDefinition
};

struct BfIRState
{
	BfIRBlock mActualInsertBlock; // Only when not ignoring writes
	BfIRBlock mInsertBlock;
	BfIRFunction mActiveFunction;
	bool mActiveFunctionHasBody;
	Array<BfFilePosition> mSavedDebugLocs;
};

enum BfOverflowCheckKind : int8
{
	BfOverflowCheckKind_None = 0,
	BfOverflowCheckKind_Signed = 1,
	BfOverflowCheckKind_Unsigned = 2,
	BfOverflowCheckKind_Flag_UseAsm = 4
};

class BfIRBuilder : public BfIRConstHolder
{
public:
	bool mIgnoreWrites;
	bool mDbgVerifyCodeGen;
	int mCurFakeId;
	bool mHasGlobalDefs;
	bool mIsBeefBackend;
	int mNumFunctionsWithBodies;
	int mBlockCount;
	bool mHasStarted;
	int mCmdCount;

	ChunkedDataBuffer mStream;
	BfIRBlock mActualInsertBlock; // Only when not ignoring writes
	BfIRBlock mInsertBlock;
	bool mHasDebugLoc;
	bool mHasDebugInfo;
	bool mHasDebugLineInfo;
	Dictionary<BfMethodRef, BfIRFunctionType> mMethodTypeMap;
	Dictionary<StringView, BfIRFunction> mFunctionMap;
	Dictionary<BfType*, BfIRPopulateType> mTypeMap;
	Dictionary<int, BfIRValue> mConstMemMap;
	Array<BfTypeInstance*> mDITemporaryTypes;
	BfIRFunction mActiveFunction;
	bool mActiveFunctionHasBody;
	Array<BfFilePosition> mSavedDebugLocs;
	Array<BfType*> mDeferredDbgTypeDefs;

	BfIRCodeGenBase* mIRCodeGen;
	BfIRCodeGen* mBfIRCodeGen;
	BeIRCodeGen* mBeIRCodeGen;

#ifdef BFIR_RENTRY_CHECK
	std::set<BfType*> mDeclReentrySet;
	std::set<BfType*> mDefReentrySet;
#endif
	bool mOpFailed;

public:
	~BfIRBuilder();

	void WriteSLEB128(int64 val);
	void WriteSLEB128(int32 val);
	void Write(uint8 val);
	void Write(bool val);
	void Write(int val);
	void Write(int64 val);
	void Write(Val128 val);
	void Write(const StringImpl& str);
	void Write(const BfIRValue& irValue);
	void Write(BfTypeCode typeCode);
	void Write(const BfIRTypeData& type);
	void Write(BfIRFunctionType func);
	void Write(BfIRFunction funcType);
	void Write(BfIRBlock block);
	void Write(BfIRMDNode node);
	template <typename T>
	void Write(const BfSizedArray<T>& sizedArray)
	{
		WriteSLEB128(sizedArray.mSize);
		for (int i = 0; i < sizedArray.mSize; i++)
			Write(sizedArray.mVals[i]);
	}
	BfIRValue WriteCmd(BfIRCmd cmd);

	template <typename T>
	void WriteArg(const T& first)
	{
		Write(first);
	}

	template <typename T, typename... Args>
	void WriteArg(const T& first, const Args&... args)
	{
		Write(first);
		WriteArg(args...);
	}

	template <typename... Args>
	BfIRValue WriteCmd(BfIRCmd cmd, const Args&... args)
	{
		if (mIgnoreWrites)
			return GetFakeVal();
		//int dataPos = mStream.GetSize();
		auto result = WriteCmd(cmd);
		WriteArg(args...);
		return result;
		//return BfIRValue(BfIRValueFlags_Value, dataPos);
	}

public:
	void NewCmdInserted();
	BfIRMDNode CreateNamespaceScope(BfType* type, BfIRMDNode fileDIScope);
	String GetDebugTypeName(BfTypeInstance* typeInstance, bool includeOuterTypeName);
	void CreateDbgTypeDefinition(BfType* type);
	bool WantsDbgDefinition(BfType * type);
	void CreateTypeDeclaration(BfType* type, bool forceDbgDefine);
	void CreateTypeDefinition_Data(BfModule* populateModule, BfTypeInstance* typeInstance, bool forceDbgDefine);
	void CreateTypeDefinition(BfType* type, bool forceDbgDefine);
	void ReplaceDITemporaryTypes();
	void PushDbgLoc(BfTypeInstance* typeInst);
	BfIRPopulateType GetPopulateTypeState(BfType* type);
	void PopulateType(BfType* type, BfIRPopulateType populateType = BfIRPopulateType_Full);
	void SetType(BfType* type, BfIRType irType);
	void SetInstType(BfType* type, BfIRType irType);
	int GetFakeId();
	BfIRValue GetFakeVal();
	BfIRValue GetFakeConst();
	BfIRType GetFakeType();
	BfIRType GetFakeBlock();
	BfIRFunctionType GetFakeFunctionType();
	BfIRFunction GetFakeFunction();

public:
	void OpFailed();

	uint8 CheckedAdd(uint8 a, uint8 b);
	uint16 CheckedAdd(uint16 a, uint16 b);
	uint32 CheckedAdd(uint32 a, uint32 b);
	uint64 CheckedAdd(uint64 a, uint64 b);
	int8 CheckedAdd(int8 a, int8 b);
	int16 CheckedAdd(int16 a, int16 b);
	int32 CheckedAdd(int32 a, int32 b);
	int64 CheckedAdd(int64 a, int64 b);
	float CheckedAdd(float a, float b) { return a + b; }
	double CheckedAdd(double a, double b) { return a + b; }

	uint8 CheckedSub(uint8 a, uint8 b);
	uint16 CheckedSub(uint16 a, uint16 b);
	uint32 CheckedSub(uint32 a, uint32 b);
	uint64 CheckedSub(uint64 a, uint64 b);
	int8 CheckedSub(int8 a, int8 b);
	int16 CheckedSub(int16 a, int16 b);
	int32 CheckedSub(int32 a, int32 b);
	int64 CheckedSub(int64 a, int64 b);
	float CheckedSub(float a, float b) { return a - b; }
	double CheckedSub(double a, double b) { return a - b; }

	uint8 CheckedMul(uint8 a, uint8 b);
	uint16 CheckedMul(uint16 a, uint16 b);
	uint32 CheckedMul(uint32 a, uint32 b);
	uint64 CheckedMul(uint64 a, uint64 b);
	int8 CheckedMul(int8 a, int8 b);
	int16 CheckedMul(int16 a, int16 b);
	int32 CheckedMul(int32 a, int32 b);
	int64 CheckedMul(int64 a, int64 b);
	float CheckedMul(float a, float b) { return a * b; }
	double CheckedMul(double a, double b) { return a * b; }

	uint8 CheckedShl(uint8 a, uint8 b);
	uint16 CheckedShl(uint16 a, uint16 b);
	uint32 CheckedShl(uint32 a, uint32 b);
	uint64 CheckedShl(uint64 a, uint64 b);
	int8 CheckedShl(int8 a, int8 b);
	int16 CheckedShl(int16 a, int16 b);
	int32 CheckedShl(int32 a, int32 b);
	int64 CheckedShl(int64 a, int64 b);

public:
	BfIRBuilder(BfModule* module);
	bool HasExports(); // Contains non-empty functions and/or non-empty globals

	String ToString(BfIRValue irValue);
	String ToString(BfIRType irType);
	String ToString(BfIRFunction irFunc);
	String ToString(BfIRFunctionType irType);
	String ToString(BfIRMDNode irMDNode);
	String ActiveFuncToString();
	void PrintActiveFunc();
	void pv(const BfIRValue& irValue);
	void pt(const BfIRType& irType);
	void pbft(BfType* type);
	void pt(const BfIRFunction& irFun);
	void pft(const BfIRFunctionType& irType);
	void pmd(const BfIRMDNode& irMDNode);

	void GetBufferData(Array<uint8>& outBuffer);
	void ClearConstData();
	void ClearNonConstData();

	void Start(const StringImpl& moduleName, int ptrSize, bool isOptimized);
	void SetBackend(bool isBeefBackend);
	void RemoveIRCodeGen();
	void WriteIR(const StringImpl& fileName);

	void Module_SetTargetTriple(const StringImpl& targetTriple, const StringImpl& targetCPU);
	void Module_AddModuleFlag(const StringImpl& flag, int val);

	BfIRType GetPrimitiveType(BfTypeCode typeCode);
	BfIRType CreateStructType(const StringImpl& name);
	BfIRType CreateStructType(const BfSizedArray<BfIRType>& memberTypes);
	void StructSetBody(BfIRType type, const BfSizedArray<BfIRType>& memberTypes, int size, int align, bool isPacked);
	BfIRType MapType(BfType* type, BfIRPopulateType populateType = BfIRPopulateType_Declaration);
	BfIRType MapTypeInst(BfTypeInstance* typeInst, BfIRPopulateType populateType = BfIRPopulateType_Declaration);
	BfIRType MapTypeInstPtr(BfTypeInstance* typeInst);
	BfIRType GetType(BfIRValue val);
	BfIRType GetPointerTo(BfIRFunctionType funcType);
	BfIRType GetPointerTo(BfIRType type);
	BfIRType GetSizedArrayType(BfIRType elementType, int length);
	BfIRType GetVectorType(BfIRType elementType, int length);

	BfIRValue CreateConstAgg_Value(BfIRType type, const BfSizedArray<BfIRValue>& values);
	BfIRValue CreateConstString(const StringImpl& string);
	BfIRValue ConstToMemory(BfIRValue constVal);
	BfIRValue GetConfigConst(BfIRConfigConst constType, BfTypeCode typeCode);

	BfIRValue GetArgument(int argIdx);
	void SetName(BfIRValue val, const StringImpl& name);

	BfIRValue CreateUndefValue(BfIRType type);
	BfIRValue CreateNumericCast(BfIRValue val, bool valIsSigned, BfTypeCode typeCode);
	BfIRValue CreateCmpEQ(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateCmpNE(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateCmpLT(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateCmpLTE(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateCmpGT(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateCmpGTE(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateAdd(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind = BfOverflowCheckKind_None);
	BfIRValue CreateSub(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind = BfOverflowCheckKind_None);
	BfIRValue CreateMul(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind = BfOverflowCheckKind_None);
	BfIRValue CreateDiv(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateRem(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateAnd(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateOr(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateXor(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateShl(BfIRValue lhs, BfIRValue rhs);
	BfIRValue CreateShr(BfIRValue lhs, BfIRValue rhs, bool isSigned);
	BfIRValue CreateNeg(BfIRValue val);
	BfIRValue CreateNot(BfIRValue val);
	BfIRValue CreateBitCast(BfIRValue val, BfIRType type);
	BfIRValue CreatePtrToInt(BfIRValue val, BfTypeCode typeCode);
	BfIRValue CreateIntToPtr(BfIRValue val, BfIRType type);
	BfIRValue CreateIntToPtr(uint64 val, BfIRType type);
	BfIRValue CreateInBoundsGEP(BfIRValue val, int idx0);
	BfIRValue CreateInBoundsGEP(BfIRValue val, int idx0, int idx1);
	BfIRValue CreateInBoundsGEP(BfIRValue val, BfIRValue idx0);
	BfIRValue CreateInBoundsGEP(BfIRValue val, BfIRValue idx0, BfIRValue idx1);
	BfIRValue CreateIsNull(BfIRValue val);
	BfIRValue CreateIsNotNull(BfIRValue val);
	BfIRValue CreateExtractValue(BfIRValue val, int idx);
	BfIRValue CreateExtractValue(BfIRValue val, BfIRValue idx);
	BfIRValue CreateInsertValue(BfIRValue agg, BfIRValue val, int idx);

	BfIRValue CreateAlloca(BfIRType type);
	BfIRValue CreateAlloca(BfIRType type, BfIRValue arraySize);
	void SetAllocaAlignment(BfIRValue val, int alignment);
	// When we do a dynamic alloca where we know the memory access patterns will not cause a page fault, we can omit the __chkstk call
	//  Generally, this is when we allocate less than 4k and we know there will be a write on this memory before the next alloca
	void SetAllocaNoChkStkHint(BfIRValue val);
	void SetAllocaForceMem(BfIRValue val);
	BfIRValue CreateAliasValue(BfIRValue val);
	BfIRValue CreateLifetimeStart(BfIRValue val);
	BfIRValue CreateLifetimeEnd(BfIRValue val);
	BfIRValue CreateLifetimeSoftEnd(BfIRValue val);
	BfIRValue CreateLifetimeExtend(BfIRValue val);
	BfIRValue CreateValueScopeStart();
	void CreateValueScopeRetain(BfIRValue val); // When a value is held by a variable -- don't release until we have a HardValueScopeEnd
	void CreateValueScopeSoftEnd(BfIRValue scopeStart);
	void CreateValueScopeHardEnd(BfIRValue scopeStart);
	BfIRValue CreateLoad(BfIRValue val, bool isVolatile = false);
	BfIRValue CreateAlignedLoad(BfIRValue val, int align, bool isVolatile = false);
	BfIRValue CreateStore(BfIRValue val, BfIRValue ptr, bool isVolatile = false);
	BfIRValue CreateAlignedStore(BfIRValue val, BfIRValue ptr, int align, bool isVolatile = false);
	BfIRValue CreateMemSet(BfIRValue addr, BfIRValue val, BfIRValue size, int align);
	void CreateFence(BfIRFenceType fenceType);
	BfIRValue CreateStackSave();
	BfIRValue CreateStackRestore(BfIRValue stackVal);

	BfIRValue CreateGlobalVariable(BfIRType varType, bool isConstant, BfIRLinkageType linkageType, BfIRValue initializer, const StringImpl& name, bool isTLS = false);
	void CreateGlobalVariable(BfIRValue irValue);
	void GlobalVar_SetUnnamedAddr(BfIRValue val, bool unnamedAddr);
	void GlobalVar_SetInitializer(BfIRValue globalVar, BfIRValue initVal);
	void GlobalVar_SetAlignment(BfIRValue globalVar, int alignment);
	void GlobalVar_SetStorageKind(BfIRValue globalVar, BfIRStorageKind storageKind);
	BfIRValue CreateGlobalStringPtr(const StringImpl& str);
	void SetReflectTypeData(BfIRType type, BfIRValue globalVar);

	BfIRBlock CreateBlock(const StringImpl& name, bool addNow = false);
	BfIRBlock MaybeChainNewBlock(const StringImpl& name); // Creates new block if current block isn't empty
	void AddBlock(BfIRBlock block);
	void DropBlocks(BfIRBlock block);
	void MergeBlockDown(BfIRBlock fromBlock, BfIRBlock intoBlock);
	void SetInsertPoint(BfIRValue value);
	void SetInsertPoint(BfIRBlock block);
	void SetInsertPointAtStart(BfIRBlock block);
	void EraseFromParent(BfIRBlock block);
	void DeleteBlock(BfIRBlock block);
	void EraseInstFromParent(BfIRValue val);
	BfIRValue CreateBr(BfIRBlock block);
	BfIRValue CreateBr_Fake(BfIRBlock block);
	BfIRValue CreateBr_NoCollapse(BfIRBlock block);
	void CreateCondBr(BfIRValue val, BfIRBlock trueBlock, BfIRBlock falseBlock);
	BfIRBlock GetInsertBlock();
	void MoveBlockToEnd(BfIRBlock block);

	BfIRValue CreateSwitch(BfIRValue value, BfIRBlock dest, int numCases);
	BfIRValue AddSwitchCase(BfIRValue switchVal, BfIRValue caseVal, BfIRBlock caseBlock);
	void SetSwitchDefaultDest(BfIRValue switchVal, BfIRBlock caseBlock);
	BfIRValue CreatePhi(BfIRType type, int incomingCount);
	void AddPhiIncoming(BfIRValue phi, BfIRValue value, BfIRBlock comingFrom);

	BfIRFunction GetIntrinsic(String intrinName, int intrinId, BfIRType returnType, const BfSizedArray<BfIRType>& paramTypes);
	BfIRFunctionType MapMethod(BfMethodInstance* methodInstance);
	BfIRFunctionType CreateFunctionType(BfIRType resultType, const BfSizedArray<BfIRType>& paramTypes, bool isVarArg = false);
	BfIRFunction CreateFunction(BfIRFunctionType funcType, BfIRLinkageType linkageType, const StringImpl& name);
	void SetFunctionName(BfIRValue func, const StringImpl& name);
	void EnsureFunctionPatchable();
	BfIRValue RemapBindFunction(BfIRValue func);
	void SetActiveFunction(BfIRFunction func);
	BfIRFunction GetActiveFunction();
	BfIRFunction GetFunction(const StringImpl& name);
	BfIRValue CreateCall(BfIRValue func, const BfSizedArray<BfIRValue>& params);
	void SetCallCallingConv(BfIRValue callInst, BfIRCallingConv callingConv);
	void SetFuncCallingConv(BfIRFunction func, BfIRCallingConv callingConv);
	void SetTailCall(BfIRValue callInst);
	void SetCallAttribute(BfIRValue callInst, int paramIdx, BfIRAttribute attribute);
	BfIRValue CreateRet(BfIRValue val);
	BfIRValue CreateSetRet(BfIRValue val, int returnTypeId);
	void CreateRetVoid();
	void CreateUnreachable();
	void Call_AddAttribute(BfIRValue callInst, int argIdx, BfIRAttribute attr);
	void Call_AddAttribute(BfIRValue callInst, int argIdx, BfIRAttribute attr, int arg);
	void Func_AddAttribute(BfIRFunction func, int argIdx, BfIRAttribute attr);
	void Func_AddAttribute(BfIRFunction func, int argIdx, BfIRAttribute attr, int arg);
	void Func_SetParamName(BfIRFunction func, int argIdx, const StringImpl& name);
	void Func_DeleteBody(BfIRFunction func);
	void Func_SafeRename(BfIRFunction func);
	void Func_SafeRenameFrom(BfIRFunction func, const StringImpl& prevName);
	void Func_SetLinkage(BfIRFunction func, BfIRLinkageType linkage);

	void Comptime_Error(int errorKind);
	BfIRValue Comptime_GetBfType(int typeId, BfIRType resultType);
	BfIRValue Comptime_GetReflectType(int typeId, BfIRType resultType);
	BfIRValue Comptime_DynamicCastCheck(BfIRValue value, int typeId, BfIRType resultType);
	BfIRValue Comptime_GetVirtualFunc(BfIRValue value, int virtualTableId, BfIRType resultType);
	BfIRValue Comptime_GetInterfaceFunc(BfIRValue value, int typeId, int methodIdx, BfIRType resultType);

	void SaveDebugLocation();
	void RestoreDebugLocation();
	void DupDebugLocation();
	bool HasDebugLocation();
	void ClearDebugLocation();
	void ClearDebugLocation(BfIRValue inst);
	void ClearDebugLocation_Last();
	void UpdateDebugLocation(BfIRValue inst);
	void SetCurrentDebugLocation(int line, int column, BfIRMDNode diScope, BfIRMDNode diInlinedAt);
	void CreateNop();
	void CreateEnsureInstructionAt();
	void CreateStatementStart();
	void CreateObjectAccessCheck(BfIRValue value, bool useAsm);

	void DbgInit();
	void DbgFinalize();
	bool DbgHasInfo();
	bool DbgHasLineInfo();
	String DbgGetStaticFieldName(BfFieldInstance* fieldInstance);
	void DbgAddPrefix(String& name);
	BfIRMDNode DbgCreateCompileUnit(int lang, const StringImpl& filename, const StringImpl& directory, const StringImpl& producer, bool isOptimized,
		const StringImpl& flags, int runtimeVer, bool linesOnly);
	BfIRMDNode DbgCreateFile(const StringImpl& fileName, const StringImpl& directory, const Val128& md5Hash);
	BfIRMDNode DbgGetCurrentLocation();
	void DbgSetType(BfType * type, BfIRMDNode diType);
	void DbgSetInstType(BfType * type, BfIRMDNode diType);
	BfIRMDNode DbgCreateConstValue(int64 val);
	BfIRMDNode DbgGetType(BfType* type, BfIRPopulateType populateType = BfIRPopulateType_Declaration);
	BfIRMDNode DbgGetTypeInst(BfTypeInstance* typeInst, BfIRPopulateType populateType = BfIRPopulateType_Declaration);
	void DbgTrackDITypes(BfType* type);
	BfIRMDNode DbgCreateNameSpace(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNum);
	BfIRMDNode DbgCreateImportedModule(BfIRMDNode context, BfIRMDNode namespaceNode, int line);
	BfIRMDNode DbgCreateBasicType(const StringImpl& name, int64 sizeInBits, int64 alignInBits, int encoding);
	BfIRMDNode DbgCreateStructType(BfIRMDNode context, const StringImpl& name, BfIRMDNode file, int lineNum, int64 sizeInBits, int64 alignInBits,
		int flags, BfIRMDNode derivedFrom, const BfSizedArray<BfIRMDNode>& elements);
	BfIRMDNode DbgCreateEnumerationType(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNumber, int64 SizeInBits, int64 alignInBits,
		const BfSizedArray<BfIRMDNode>& elements, BfIRMDNode underlyingType);
	BfIRMDNode DbgCreatePointerType(BfIRMDNode diType);
	BfIRMDNode DbgCreateReferenceType(BfIRMDNode diType);
	BfIRMDNode DbgCreateConstType(BfIRMDNode diType);
	BfIRMDNode DbgCreateArtificialType(BfIRMDNode diType);
	BfIRMDNode DbgCreateArrayType(int64 sizeInBits, int64 alignInBits, BfIRMDNode elementType, int64 numElements);
	BfIRMDNode DbgCreateReplaceableCompositeType(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode F, int line, int64 sizeInBits = 0, int64 alignInBits = 0, int flags = 0);
	void DbgSetTypeSize(BfIRMDNode diType, int64 sizeInBits, int alignInBits);
	BfIRMDNode DbgCreateForwardDecl(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode F, int line);
	BfIRMDNode DbgCreateSizedForwardDecl(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode F, int line, int64 sizeInBits, int64 alignInBits);
	BfIRMDNode DbgReplaceAllUses(BfIRMDNode diPrevNode, BfIRMDNode diNewNode);
	void DbgDeleteTemporary(BfIRMDNode diNode);
	BfIRMDNode DbgMakePermanent(BfIRMDNode diNode, BfIRMDNode diBaseType, const BfSizedArray<BfIRMDNode>& elements);
	BfIRMDNode DbgCreateEnumerator(const StringImpl& name, int64 val);
	BfIRMDNode DbgCreateMemberType(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNumber, int64 sizeInBits, int64 alignInBits,
		int64 offsetInBits, int flags, BfIRMDNode type);
	BfIRMDNode DbgCreateStaticMemberType(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNumber, BfIRMDNode type, int flags, BfIRValue val);
	BfIRMDNode DbgCreateInheritance(BfIRMDNode type, BfIRMDNode baseType, int64 baseOffset, int flags);
	BfIRMDNode DbgCreateMethod(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNum, BfIRMDNode type,
		bool isLocalToUnit, bool isDefinition, int vk, int vIndex, BfIRMDNode vTableHolder, int flags, bool isOptimized, BfIRValue fn,
		const BfSizedArray<BfIRMDNode>& genericArgs, const BfSizedArray<BfIRValue>& genericConstValueArgs);
	BfIRMDNode DbgCreateFunction(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNum, BfIRMDNode type,
		bool isLocalToUnit, bool isDefinition, int scopeLine, int flags, bool isOptimized, BfIRValue fn);
	BfIRMDNode DbgCreateParameterVariable(BfIRMDNode scope, const StringImpl& name, int argNo, BfIRMDNode file, int lineNum, BfIRMDNode type,
		bool AlwaysPreserve = false, int flags = 0);
	BfIRMDNode DbgCreateSubroutineType(BfMethodInstance* methodInstance);
	BfIRMDNode DbgCreateSubroutineType(const BfSizedArray<BfIRMDNode>& elements);
	BfIRMDNode DbgCreateAutoVariable(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNo, BfIRMDNode type, BfIRInitType initType = BfIRInitType_NotSet);
	BfIRValue DbgInsertValueIntrinsic(BfIRValue val, BfIRMDNode varInfo);
	BfIRValue DbgInsertDeclare(BfIRValue val, BfIRMDNode varInfo, BfIRValue declareBefore = BfIRValue());
	BfIRValue DbgLifetimeEnd(BfIRMDNode varInfo);
	void DbgCreateGlobalVariable(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNumber,
		BfIRMDNode type, bool isLocalToUnit, BfIRValue val, BfIRMDNode Decl = BfIRMDNode());
	BfIRMDNode DbgCreateLexicalBlock(BfIRMDNode scope, BfIRMDNode file, int line, int col);
	void DbgCreateAnnotation(BfIRMDNode scope, const StringImpl& name, BfIRValue value);

	BfIRState GetState();
	void SetState(const BfIRState& state);
};

NS_BF_END
