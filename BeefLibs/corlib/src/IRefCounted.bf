using System.Threading;
using System.Diagnostics;

namespace System
{
	interface IRefCounted
	{
		void AddRef();
		void Release();
	}

	class RefCounted : IRefCounted
	{
		protected int32 mRefCount = 1;

		public ~this()
		{
			Debug.Assert(mRefCount == 0);
		}

		public int RefCount
		{
			get
			{
				return mRefCount;
			}
		}

		public void DeleteUnchecked()
		{
			mRefCount = 0;
			delete this;
		}

		public void AddRef()
		{
			Interlocked.Increment(ref mRefCount);
		}

		public void ReleaseRef()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			if (refCount == 0)
			{
				delete this;
			}
		}

		public void ReleaseLastRef()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount == 0);
			if (refCount == 0)
			{
				delete this;
			}
		}

		public int ReleaseRefNoDelete()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			return refCount;
		}

		void IRefCounted.Release()
		{
			ReleaseRef();
		}

		struct Alloc
		{
			public void* Alloc(Type type, int size, int align)
			{
				int sizeAdd = size + Math.Max(align, sizeof(int));

				void* data = Internal.Malloc(sizeAdd);
				return (uint8*)data + sizeAdd;
			}
		}	
	}

	class RefCounted<T> : IRefCounted where T : class, delete
	{
		public T mVal;
		public int mRefCount = 1;

		public int RefCount => mRefCount;
		public T Value => mVal;

		protected this()
		{

		}

		protected ~this()
		{
			Debug.Assert(mRefCount == 0);
			delete mVal;
		}

		[OnCompile(.TypeInit), Comptime]
		static void Init()
		{
			String emitStr = scope .();

			for (var methodInfo in typeof(T).GetMethods(.Public | .DeclaredOnly))
			{
				if (methodInfo.IsStatic)
					continue;
				if (!methodInfo.IsConstructor)
					continue;

				emitStr.AppendF("public static RefCounted<T> Create(");
				methodInfo.GetParamsDecl(emitStr);
				emitStr.AppendF(")\n");
				emitStr.AppendF("{{\n");
				emitStr.AppendF("\treturn new [Friend] RefCountedAppend<T>(");
				methodInfo.GetArgsList(emitStr);
				emitStr.AppendF(");\n}}\n");
			}

			Compiler.EmitTypeBody(typeof(Self), emitStr);
		}

		public static RefCounted<T> Attach(T val)
		{
			return new Self() { mVal = val };
		}

		public virtual void DeleteSelf()
		{
			delete this;
		}

		public void DeleteUnchecked()
		{
			mRefCount = 0;
			DeleteSelf();
		}

		public void AddRef()
		{
			Interlocked.Increment(ref mRefCount);
		}

		public void Release()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			if (refCount == 0)
				DeleteSelf();
		}

		public void ReleaseLastRef()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount == 0);
			if (refCount == 0)
				DeleteSelf();
		}

		public int ReleaseRefNoDelete()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			return refCount;
		}

		public virtual T Detach()
		{
			var val = mVal;
			mVal = null;
			return val;
		}

		public static T operator->(Self self)
		{
			return self.mVal;
		}

		public static T operator implicit(Self self)
		{
			return self.mVal;
		}
	}

	class RefCountedAppend<T> : RefCounted<T> where T : class, new, delete
	{
		protected ~this()
		{
			Debug.Assert(mRefCount == 0);
			delete:append mVal;
			mVal = null;
		}

		[OnCompile(.TypeInit), Comptime]
		static void Init()
		{
			String emitStr = scope .();

			for (var methodInfo in typeof(T).GetMethods(.Public | .DeclaredOnly))
			{
				if (methodInfo.IsStatic)
					continue;
				if (!methodInfo.IsConstructor)
					continue;

				emitStr.AppendF("[AllowAppend]\nprotected this(");
				methodInfo.GetParamsDecl(emitStr);
				emitStr.AppendF(")\n");
				emitStr.AppendF("{{\n");
				emitStr.AppendF("\tvar val = append T(");
				methodInfo.GetArgsList(emitStr);
				emitStr.AppendF(");\n");
				emitStr.AppendF("\tmVal = val;\n");
				emitStr.AppendF("}}\n");
			}

			Compiler.EmitTypeBody(typeof(Self), emitStr);
		}

		public override T Detach()
		{
			Runtime.FatalError("Can only detach from objects created via RefCounted<T>.Attach");
		}
	}
}
