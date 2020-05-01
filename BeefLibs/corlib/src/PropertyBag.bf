using System.Collections;

namespace System
{
	class PropertyBag
	{
		public Dictionary<String, Object> mProperties ~ DeleteDictionaryAndKeysAndItems!(_);

		public void Add<T>(String key, T value) where T : struct
		{
			if (mProperties == null)
				mProperties = new Dictionary<String, Object>();
			String* keyPtr;
			Object* objPtr;
			if (mProperties.TryAdd(key, out keyPtr, out objPtr))
			{
				*keyPtr = new String(key);
				*objPtr = new box value;
			}
			else
			{
				delete *objPtr;
				*objPtr = new box value;
			}
		}

		public void Add(String key, StringView value)
		{
			if (mProperties == null)
				mProperties = new Dictionary<String, Object>();
			String* keyPtr;
			Object* objPtr;
			if (mProperties.TryAdd(key, out keyPtr, out objPtr))
			{
				*keyPtr = new String(key);
				*objPtr = new String(value);
			}
			else
			{
				delete *objPtr;
				*objPtr = new String(value);
			}
		}

		public bool Get<T>(String key, out T value)
		{
			if (mProperties == null)
			{
				value = default;
				return false;
			}

			Object val;
			if (!mProperties.TryGetValue(key, out val))
			{
				value = default;
				return false;
			}

			value = (T)val;
			return true;
		}

		public T Get<T>(String key, T defaultVal = default)
		{
			T val;
			if (!Get(key, out val))
				val = defaultVal;
			return val;
		}

		public static bool operator==(PropertyBag lhs, PropertyBag rhs)
		{
			if (lhs.mProperties.Count != rhs.mProperties.Count)
				return false;

			for (let kv in lhs.mProperties)
			{
				Object lhsVal = *kv.value;
				Object rhsVal;
				if (!rhs.Get(kv.key, out rhsVal))
					return false;

				var lhsType = lhsVal.GetType();
				if (lhsType != rhsVal.GetType())
					return false;

				switch (lhsType)
				{
				case typeof(String):
					if ((String)lhsVal != (String)rhsVal)
						return false;
				case typeof(Boolean):
					if ((bool)lhsVal != (bool)rhsVal)
						return false;
				default:
					if (lhsVal != rhsVal)
						return false;
				}
			}
			return true;
		}
	}
}
