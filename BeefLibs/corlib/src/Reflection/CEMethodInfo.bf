using System.Diagnostics;
using System.Collections;

namespace System.Reflection
{
	struct ComptimeMethodInfo
	{
		[CRepr, Packed]
		public struct Info
		{
			public int32 mReturnTypeId;
			public int32 mParamCount;
			public MethodFlags mMethodFlags;
		}

		[CRepr, Packed]
		public struct ParamInfo
		{
			public int32 mParamTypeId;
			public TypeInstance.ParamFlags mParamFlags;
			public String mName;
		}

		public int64 mNativeMethodInstance;

		public bool IsInitialized => true;
		public StringView Name
		{
			get
			{
				if (Compiler.IsComptime)
					return Type.[Friend]Comptime_Method_GetName(mNativeMethodInstance);
				return "?";
			}
		}
		public int ParamCount
		{
			get
			{
				if (Compiler.IsComptime)
					return Type.[Friend]Comptime_Method_GetInfo(mNativeMethodInstance).mParamCount;
				return 0;
			}
		}
		public bool IsConstructor => Name == "__BfCtor" || Name == "__BfStaticCtor";
		public bool IsDestructor => Name == "__BfStaticDtor" || Name == "__BfStaticDtor";
		public Type ReturnType
		{
			get
			{
				if (Compiler.IsComptime)
					return Type.[Friend]GetType((.)Type.[Friend]Comptime_Method_GetInfo(mNativeMethodInstance).mReturnTypeId);
				return null;
			}
		}

		public this(int64 nativeMethodInstance)
		{
			mNativeMethodInstance = nativeMethodInstance;
		}

		public Type GetParamType(int paramIdx)
		{
			if (Compiler.IsComptime)
				return Type.[Friend]GetType((.)Type.[Friend]Comptime_Method_GetParamInfo(mNativeMethodInstance, (.)paramIdx).mParamTypeId);
			return null;
		}

		public StringView GetParamName(int paramIdx)
		{
			if (Compiler.IsComptime)
				return Type.[Friend]Comptime_Method_GetParamInfo(mNativeMethodInstance, (.)paramIdx).mName;
			return default;
		}

		public override void ToString(String strBuffer)
		{
			if (Compiler.IsComptime)
			{
				String str = Type.[Friend]Comptime_Method_ToString(mNativeMethodInstance);
				strBuffer.Append(str);
			}
		}

		public struct Enumerator : IEnumerator<ComptimeMethodInfo>
		{
			BindingFlags mBindingFlags;
			TypeInstance mTypeInstance;
		    int32 mIdx;
			int32 mCount;

		    public this(TypeInstance typeInst, BindingFlags bindingFlags)
		    {
				//Debug.WriteLine($"this {typeInst}");

		        mTypeInstance = typeInst;
				mBindingFlags = bindingFlags;
		        mIdx = -1;
				if ((mTypeInstance == null) || (!Compiler.IsComptime))
					mCount = 0;
				else
					mCount = Type.[Friend]Comptime_GetMethodCount((.)mTypeInstance.TypeId);
		    }

		    public void Reset() mut
		    {
		        mIdx = -1;
		    }

		    public void Dispose()
		    {
		    }

		    public bool MoveNext() mut
		    {
				if (mTypeInstance == null)
					return false;

				for (;;)
				{
					mIdx++;
					if (mIdx == mCount)
						return false;

					int64 nativeMethodHandle = Type.[Friend]Comptime_GetMethod((int32)mTypeInstance.TypeId, mIdx);
					let info = Type.[Friend]Comptime_Method_GetInfo(nativeMethodHandle);

					bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (info.mMethodFlags.HasFlag(.Static)));
					matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!info.mMethodFlags.HasFlag(.Static)));
					if (matches)
						break;
				}
		        return true;
		    }

		    public ComptimeMethodInfo Current
		    {
		        get
		        {
					int64 nativeMethodHandle = Type.[Friend]Comptime_GetMethod((int32)mTypeInstance.TypeId, mIdx);
		            return ComptimeMethodInfo(nativeMethodHandle);
		        }
		    }

			public Result<ComptimeMethodInfo> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
