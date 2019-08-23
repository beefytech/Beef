namespace System.FFI
{
	[CRepr]
	struct FFIType
	{
		public enum TypeKind : uint16
		{
			Void,
			Int,
			Float,
			Double,
			LongDouble,
			UInt8,
			SInt8,
			UInt16,
			SInt16,
			UInt32,
			SInt32,
			UInt64,
			SInt64,
			Struct,
			Pointer
		}

		public int mSize;
		public uint16 mAlignment;
		public TypeKind mTypeKind;
		public FFIType** mElements;

		public this(int size, int align, TypeKind typeKind, FFIType** elements = null)
		{
			mSize = size;
			mAlignment = (.)align;
			mTypeKind = typeKind;
			mElements = elements;
		}

		public this(int size, TypeKind typeKind)
		{
			mSize = size;
			mAlignment = (uint16)size;
			mTypeKind = typeKind;
			mElements = null;
		}
		
		public static FFIType Void = .(1, .Void);
		public static FFIType UInt8 = .(1, .UInt8);
		public static FFIType Int8 = .(1, .SInt8);
		public static FFIType UInt16 = .(2, .UInt16);
		public static FFIType Int16 = .(2, .SInt16);
		public static FFIType UInt32 = .(4, .UInt32);
		public static FFIType Int32 = .(4, .SInt32);
		public static FFIType UInt64 = .(8, .UInt64);
		public static FFIType Int64 = .(8, .SInt64);
		public static FFIType Float = .(4, .Float);
		public static FFIType Double = .(8, .Double);
		public static FFIType Pointer = .(sizeof(void*), .Pointer);

		public static FFIType* Get(Type type, void* allocBytes = null, int* inOutSize = null)
		{
			var type;

			if (type.IsTypedPrimitive)
				type = type.UnderlyingType;

			switch (type.mTypeCode)
			{
			case .None:
				return &FFIType.Void;
			case .Boolean,
				 .Char8,
				 .UInt8:
				return &FFIType.UInt8;
			case .Int8:
				return &FFIType.Int8;
			case .Char16,
				 .UInt16:
				return &FFIType.UInt16;
			case .Int16:
				return &FFIType.Int16;
			case .Char32,
				 .UInt32:
				return &FFIType.UInt32;
			case .Int32:
				return &FFIType.Int32;
			case .UInt64:
				return &FFIType.UInt64;
			case .Int64:
				return &FFIType.Int64;
			case .Float:
				return &FFIType.Float;
			case .Double:
				return &FFIType.Double;
			case .Struct:
				int wantSize = sizeof(FFIType);
				if (allocBytes == null)
				{
					*inOutSize = wantSize;
					return null;
				}

				FFIType* ffiType = (FFIType*)allocBytes;
				ffiType.mSize = type.mSize;
				ffiType.mAlignment = type.mAlign;
				ffiType.mElements = null;
				ffiType.mTypeKind = .Struct;
				return ffiType;
			case .Pointer,
				 .Interface,
				 .Object:
				return &FFIType.Pointer;

#if BF_64_BIT
			case .Int:
				return &FFIType.Int64;
			case .UInt:
				return &FFIType.UInt64;
#else
			case .Int:
				return &FFIType.Int32;
			case .UInt:
				return &FFIType.UInt32;
#endif
			default:
				return null;
			}
		}
	}

	enum FFIResult : int32
	{
		OK,
		BadTypeDef,
		BadABI
	}

#if BF_PLATFORM_WINDOWS

#if BF_64_BIT
	enum FFIABI : int32
	{
		NotSet = 0,
		StdCall = 1,
		ThisCall = 1,
		FastCall = 1,
		MS_CDecl = 1,
		Default = 1
	}
#else
	enum FFIABI : int32
	{
		NotSet = 0,
		SysV,
		StdCall,
		ThisCall,
		FastCall,
		MS_CDecl,

		Default = MS_CDecl
	}
#endif

#else //!BF_PLATFORM_WINDOWS

#if BF_64_BIT
	enum FFIABI : int32
	{
		NotSet = 0,
		SysV,
		Unix64,
		Default = Unix64
	}
#else
	enum FFIABI : int32
	{
		NotSet = 0,
		SysV,
		Unix64,
		Default = SysV
	}
#endif

#endif

	struct FFILIB
	{
		[CRepr]
		public struct FFICIF
		{
			FFIABI mAbi;
			uint32 mNArgs;
			FFIType **mArgTypes;
			FFIType *mRType;
			uint32 mBytes;
			uint32 mFlags;
		}

		public static extern void* ClosureAlloc(int size, void** outFunc);
		public static extern FFIResult PrepCif(FFICIF* cif, FFIABI abi, int32 nargs, FFIType* rtype, FFIType** argTypes);
		public static extern void Call(FFICIF* cif, void* funcPtr, void* rvalue, void** args);
	}

	struct FFICaller
	{
		FFILIB.FFICIF mCIF;

		public Result<void, FFIResult> Prep(FFIABI abi, int32 nargs, FFIType* rtype, FFIType** argTypes) mut
		{
			let res = FFILIB.PrepCif(&mCIF, abi, nargs, rtype, argTypes);
			if (res == .OK)
				return .Ok;
			return .Err(res);
		}

		public void Call(void* funcPtr, void* rvalue, void** args) mut
		{
			FFILIB.Call(&mCIF, funcPtr, rvalue, args);
		}
	}
}
