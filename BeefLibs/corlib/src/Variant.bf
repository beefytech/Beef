using System.Diagnostics;

namespace System
{
    struct Variant : IDisposable
	{
		enum ObjectType
		{
			UnownedObject,
			OwnedObject,
			NullObject
		}

		enum StructFlag
		{
			InternalValue,
			OwnedPtr,
			ExternalPtr
		}

		int mStructType; // 0 = unowned object, 1 = owned object, 2 = null value (mData is type), otherwise is struct type (|0 is internal, |1 is owned ptr, |2 is external ptr)
		int mData; // This is either an Object reference, struct data, or a pointer to struct data

		public bool OwnsMemory
		{
			get
			{
				if (mStructType <= 2)
					return mStructType == 1;
				return (mStructType & 1) != 0;
			}
		}

		public bool IsObject
		{
			get
			{
				if (mStructType <= 2)
					return true;
				if ((mStructType & 3) == (int)StructFlag.ExternalPtr)
					return VariantType.IsObject;
				return false;
			}
		}

		public bool IsValueType
		{
			get
			{
				if (mStructType <= 2)
					return false;
				if ((mStructType & 3) == (int)StructFlag.ExternalPtr)
					return VariantType.IsValueType;
				return mStructType != 0;
			}
		}

		public Type VariantType
		{
			get
			{
				if (mStructType == 2)
				{
					return (Type)Internal.UnsafeCastToObject((void*)mData);
				}
				if (mStructType <= 1)
				{
					return Internal.UnsafeCastToObject((void*)mData).GetType();
				}
				return (Type)Internal.UnsafeCastToObject((void*)(mStructType & ~3));
			}
		}

		public bool HasValue
		{
			get
			{
				return (mStructType != 0) || (mData != 0);
			}
		}

		public void* DataPtr
		{
			get mut
			{
				if (mStructType <= 3)
				{
					if (mStructType == 2)
						return null;
					Object obj = Internal.UnsafeCastToObject((void*)mData);
					return (uint8*)Internal.UnsafeCastToPtr(obj) + obj.GetType().[Friend]mMemberDataOffset;
				}

				if ((mStructType & 3) == (int)StructFlag.InternalValue)
					return (void*)&mData;
				else
					return (void*)mData;
			}
		}

		protected override void GCMarkMembers()
 		{
			if ((mStructType == (int)ObjectType.UnownedObject) || (mStructType == (int)ObjectType.OwnedObject))
			{
				var obj = Internal.UnsafeCastToObject((void*)mData);
				GC.Mark(obj);
			}
		}

		public void Dispose() mut
		{
			if (mStructType == (int)ObjectType.OwnedObject)
			{
				delete Internal.UnsafeCastToObject((void*)mData);
			}
			else if (OwnsMemory)
			{
				delete (void*)mData;
			}
			mStructType = 0;
			mData = 0;
		}

		public static Variant Create<T>(T val, bool owns = false) where T : class
		{
			Variant variant;
			if (val == null)
			{
				variant.mStructType = (int)ObjectType.NullObject;
				variant.mData = (int)Internal.UnsafeCastToPtr(typeof(T));
			}
			else
			{
				variant.mStructType = (int)(owns ? (int)ObjectType.OwnedObject : (int)ObjectType.UnownedObject);
				variant.mData = (int)Internal.UnsafeCastToPtr(val);
			}
			return variant;
		}

		public static Variant Create<T>(T val) where T : struct
		{
			Variant variant;
			Type type = typeof(T);
			if (sizeof(T) <= sizeof(int))
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type);
				variant.mData = 0;
				*(T*)&variant.mData = val;
			}
			else
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
				T* newVal = (T*)new uint8[sizeof(T)]*;
				*newVal = val;
				variant.mData = (int)(void*)newVal;
			}
			return variant;
		}

		public static Variant Create<T>(T val) where T : struct*
		{
			Variant variant;
			Type type = typeof(T);
			if (type.Size <= sizeof(int))
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type);
				variant.mData = 0;
				*(T*)&variant.mData = val;
			}
			else
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
				T* newVal = (T*)new uint8[sizeof(T)]*;
				*newVal = val;
				variant.mData = (int)(void*)newVal;
			}
			return variant;
		}

		public static Variant Create<T>(ref T val) where T : struct
		{
			Variant variant;
			Type type = typeof(T);
			variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 2;
			variant.mData = 0;
			variant.mData = (int)(void*)&val;
			return variant;
		}

		public static Variant CreateOwned<T>(T val) where T : struct
		{
			Variant variant;
			Type type = typeof(T);
			variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
			T* newVal = (T*)new uint8[sizeof(T)]*;
			*newVal = val;
			variant.mData = (int)(void*)newVal;
			return variant;
		}

		public void EnsureReference() mut
		{
			if ((mStructType <= 2) && (mStructType & 3 == (int)StructFlag.InternalValue))
				return;

			var val = mData;

			mStructType |= (int)StructFlag.OwnedPtr;
			int* newVal = (int*)new uint8[sizeof(int)]*;
			*newVal = val;
			mData = (int)(void*)newVal;
		}

		public static Variant Create(Type type, void* val)
		{
			Variant variant;
			Debug.Assert(!type.IsObject);
			if (type.Size <= sizeof(int))
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type);
				variant.mData = 0;
				Internal.MemCpy(&variant.mData, val, type.[Friend]mSize);
			}
			else
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
				void* data = new uint8[type.[Friend]mSize]*;
				Internal.MemCpy(data, val, type.[Friend]mSize);
				variant.mData = (int)data;
			}
			return variant;
		}

		public static Variant CreateReference(Type type, void* val)
		{
			Variant variant;
			variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 2;
			variant.mData = (int)val;
			return variant;
		}

		public static void* Alloc(Type type, out Variant variant)
		{
			variant = .();

			if (type.IsObject)
			{
				return &variant.mData;
			}
			else
			{
				if (type.Size <= sizeof(int))
				{
					variant.mStructType = (int)Internal.UnsafeCastToPtr(type);
					variant.mData = 0;
					return &variant.mData;
				}
				else
				{
					variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
					void* data = new uint8[type.[Friend]mSize]*;
					variant.mData = (int)data;
					return data;
				}
			}
		}

		public static void* AllocOwned(Type type, out Variant variant)
		{
			variant = .();

			if (type.IsObject)
			{
				return &variant.mData;
			}
			else
			{
				variant.mStructType = (int)Internal.UnsafeCastToPtr(type) | 1;
				void* data = new uint8[type.[Friend]mSize]*;
				variant.mData = (int)data;
				return data;
			}
		}

		public T Get<T>() => Runtime.NotImplemented();

		public T Get<T>() where T : class
		{
			Debug.Assert(IsObject);
			if (mStructType == 2)
				return (T)null;
			Type type = typeof(T);
			T obj;
			if (mStructType >= 3)
				obj = (T)Internal.UnsafeCastToObject(*(void**)(void*)mData);
			else
				obj = (T)Internal.UnsafeCastToObject((void*)mData);
			Debug.Assert(obj == null || obj.GetType().IsSubtypeOf(type));
			return obj;
		}

		public T Get<T>() where T : struct
		{
			Debug.Assert(!IsObject);
			//var type = VariantType;
			//Debug.Assert((typeof(T) == type) || (typeof(T) == type.GetUnderlyingType()));
			if ((mStructType & 3) == (int)StructFlag.InternalValue)
			{
				int data = mData;
				return *(T*)&data;
			}
			else
				return *(T*)(void*)mData;
		}

		public T Get<T>() where T : struct*
		{
			Debug.Assert(!IsObject);
			//var type = VariantType;
			//Debug.Assert((typeof(T) == type) || (typeof(T) == type.GetUnderlyingType()));
			if ((mStructType & 3) == (int)StructFlag.InternalValue)
			{
				int data = mData;
				return *(T*)&data;
			}
			else
				return *(T*)(void*)mData;
		}

		public Result<Object> GetBoxed()
		{
			if (IsObject)
				return .Err;

			var type = VariantType;
			var boxedType = type.BoxedType;
			if (boxedType == null)
				return .Err;

			var self = this;
			var object = Try!(boxedType.CreateObject());
			Internal.MemCpy((uint8*)Internal.UnsafeCastToPtr(object) + boxedType.[Friend]mMemberDataOffset, self.DataPtr, type.Size);
			return object;
		}

		/*public void Get<T>(ref T val)
		{
			if (VariantType != typeof(T))
				return;
			val = Get<T>();
		}*/

		public void CopyValueData(void* dest)
		{
			if (IsObject)
			{
				if (mStructType == 2)
					*((Object*)dest) = null;
				else
					*((Object*)dest) = Internal.UnsafeCastToObject((void*)mData);
				return;
			}
			
			var type = VariantType;
			if ((mStructType & 3) == (int)StructFlag.InternalValue)
			{
				int data = mData;
				Internal.MemCpy(dest, &data, type.Size);
			}
			else
			{
				Internal.MemCpy(dest, (void*)mData, type.Size);
			}	
		}

		public void* GetValueData() mut
		{
			Debug.Assert(!IsObject);
			if ((mStructType & 3) == (int)StructFlag.InternalValue)
			{
				return (void*)&mData;
			}
			else
			{
				return (void*)mData;
			}
		}

		[Commutable]
		public static bool operator==(Variant v1, Variant v2)
		{
			if (v1.IsObject)
			{
				if (!v2.IsObject)
					return false;
				if ((v1.mStructType == 2) != (v2.mStructType == 2))
					return false; // If one is null but the other isn't
				return v1.mData == v2.mData;
			}

			if (v1.mStructType & 3 != v2.mStructType & 3)
				return false;

			if ((v1.mStructType & 3 == 0) && (v2.mStructType & 3 == 0))
				return v1.mData == v2.mData;

			var v1;
			var v2;

			let type = v1.VariantType;
			let ptr1 = v1.DataPtr;
			let ptr2 = v2.DataPtr;
			return Internal.MemCmp(ptr1, ptr2, type.[Friend]mSize) == 0;
		}

		public static mixin Equals<T>(var v1, var v2)
		{
			v1.Get<T>() == v2.Get<T>()
		}

		public static Variant CreateFromVariant(Variant varFrom)
		{
			Variant varTo = varFrom;
			if (varTo.mStructType == 1)
				varTo.mStructType = 0;
			if (varTo.mStructType > 2)
			{
				varTo.mStructType &= ~3;

				let type = (Type)Internal.UnsafeCastToObject((void*)(varFrom.mStructType & ~3));
				if (type.[Friend]mSize > sizeof(int))
				{
					varTo.mStructType |= (int)StructFlag.OwnedPtr;
					void* data = new uint8[type.[Friend]mSize]*;
					Internal.MemCpy(data, (void*)varFrom.mData, type.[Friend]mSize);
					varTo.mData = (int)data;
				}
			}

			return varTo;
		}

		public static Variant CreateFromVariantRef(ref Variant varFrom)
		{
			Variant varTo = varFrom;
			if (varTo.mStructType == 1)
				varTo.mStructType = 0;
			if (varTo.mStructType > 2)
			{
				varTo.mStructType &= ~3;

				varTo.mStructType |= (int)StructFlag.ExternalPtr;
				varTo.mData = (int)varFrom.DataPtr;
			}

			return varTo;
		}

		public static Result<Variant> CreateFromBoxed(Object objectFrom)
		{
			if (objectFrom == null)
				return default;

			Variant variant = ?;
			Type objType = objectFrom.[Friend]RawGetType();
			if (objType.IsBoxed)
			{
				void* srcDataPtr = (uint8*)Internal.UnsafeCastToPtr(objectFrom) + objType.[Friend]mMemberDataOffset;

				var underlying = objType.UnderlyingType;
				variant.mStructType = (int)Internal.UnsafeCastToPtr(underlying);
				if (underlying.Size <= sizeof(int))
				{
					variant.mData = 0;
					*(int*)&variant.mData = *(int*)srcDataPtr;
				}
				else
				{
					variant.mStructType |= (int)StructFlag.OwnedPtr;
					void* data = new uint8[underlying.[Friend]mSize]*;
					Internal.MemCpy(data, srcDataPtr, underlying.[Friend]mSize);
					variant.mData = (int)data;
				}
			}
			else
			{
				variant.mStructType = 0;
				variant.mData = (int)Internal.UnsafeCastToPtr(objectFrom);
			}

			return variant;
		}
	}
}
