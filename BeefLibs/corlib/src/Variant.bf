using System.Diagnostics;

namespace System
{
    struct Variant
	{
		int mStructType; // 0 = unowned object, 1 = owned object, 2 = null value (mData is type), otherwise is struct type
		int mData; // This is either an Object reference, struct data, or a pointer to struct data

		public bool OwnsMemory
		{
			get
			{
				if (mStructType <= 2)
					return mStructType == 1;
				return VariantType.Size > sizeof(int);
			}
		}

		public bool IsObject
		{
			get
			{
				return mStructType <= 2;
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
				return (Type)Internal.UnsafeCastToObject((void*)mStructType);
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
				if (IsObject)
				{
					if (mStructType == 2)
						return null;
					Object obj = Internal.UnsafeCastToObject((void*)mData);
					return (uint8*)Internal.UnsafeCastToPtr(obj) + obj.GetType().[Friend]mMemberDataOffset;
				}

				var type = VariantType;
				if (type.Size <= sizeof(int))
					return (void*)&mData;
				else
					return (void*)mData;
			}
		}

		protected override void GCMarkMembers()
 		{
			if ((mStructType == 1) || (mStructType == 0))
			{
				var obj = Internal.UnsafeCastToObject((void*)mData);
				GC.Mark(obj);
			}
		}

		public void Dispose() mut
		{
			if (mStructType == 1)
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
				variant.mStructType = 2;
				variant.mData = (int)(void*)typeof(T);
			}
			else
			{
				variant.mStructType = (int)(owns ? 1 : 0);
				variant.mData = (int)(void*)val;
			}
			return variant;
		}

		public static Variant Create<T>(T val) where T : struct
		{
			Variant variant;
			Type type = typeof(T);
			variant.mStructType = (int)(void*)type;
			if (sizeof(T) <= sizeof(int))
			{
				variant.mData = 0;
				*(T*)&variant.mData = val;
			}
			else
			{
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
			variant.mStructType = (int)(void*)type;
			if (type.Size <= sizeof(int))
			{
				variant.mData = 0;
				*(T*)&variant.mData = val;
			}
			else
			{
				T* newVal = (T*)new uint8[sizeof(T)]*;
				*newVal = val;
				variant.mData = (int)(void*)newVal;
			}
			return variant;
		}

		public static Variant Create(Type type, void* val)
		{
			Variant variant;
			Debug.Assert(!type.IsObject);
			//Debug.Assert((type.GetUnderlyingType() == typeof(T)) || (type == typeof(T)));
			variant.mStructType = (int)(void*)type;
			if (type.Size <= sizeof(int))
			{
				variant.mData = 0;
				Internal.MemCpy(&variant.mData, val, type.[Friend]mSize);
			}
			else
			{
				void* data = new uint8[type.[Friend]mSize]*;
				Internal.MemCpy(data, val, type.[Friend]mSize);
				variant.mData = (int)data;
			}
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
				variant.mStructType = (int)(void*)type;
				if (type.Size <= sizeof(int))
				{
					variant.mData = 0;
					return &variant.mData;
				}
				else
				{
					void* data = new uint8[type.[Friend]mSize]*;
					variant.mData = (int)data;
					return data;
				}
			}
		}

		public T Get<T>() where T : class
		{
			Debug.Assert(IsObject);
			if (mStructType == 2)
				return (T)null;
			Type type = typeof(T);
			T obj = (T)Internal.UnsafeCastToObject((void*)mData);
			Debug.Assert(obj.GetType().IsSubtypeOf(type));
			return obj;
		}

		public T Get<T>() where T : struct
		{
			Debug.Assert(!IsObject);
			var type = VariantType;
			//Debug.Assert((typeof(T) == type) || (typeof(T) == type.GetUnderlyingType()));
			if (type.Size <= sizeof(int))
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
			var type = VariantType;
			//Debug.Assert((typeof(T) == type) || (typeof(T) == type.GetUnderlyingType()));
			if (type.Size <= sizeof(int))
			{
				int data = mData;
				return *(T*)&data;
			}
			else
				return *(T*)(void*)mData;
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
			if (type.Size <= sizeof(int))
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
			var type = VariantType;
			if (type.Size <= sizeof(int))
			{
				return (void*)&mData;
			}
			else
			{
				return (void*)mData;
			}
		}

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

			if (v1.mStructType != v2.mStructType)
				return false;

			let type = v1.VariantType;
			if (type.[Friend]mSize <= sizeof(int))
				return v1.mData == v2.mData;
			for (int i < type.[Friend]mSize)
			{
				if (((uint8*)(void*)v1.mData)[i] != ((uint8*)(void*)v2.mData)[i])
					return false;
			}
			return true;
		}

		public static mixin Equals<T>(var v1, var v2)
		{
			v1.Get<T>() == v2.Get<T>()
		}

		public static Result<Variant> CreateFromVariant(Variant varFrom, bool reference = true)
		{
			Variant varTo = varFrom;
			if (varTo.mStructType == 1)
				varTo.mStructType = 0;
			return varTo;
		}

		/*public static Result<Variant> CreateFromObject(Object objectFrom, bool reference = true)
		{
			Type objType = objectFrom.[Friend]RawGetType();
			
		}*/
	}
}