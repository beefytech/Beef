using System.Reflection;

namespace System
{
	struct Enum
	{
		public static void EnumToString(Type type, String strBuffer, int64 iVal)
		{
			for (var field in type.GetFields())
			{
				if (field.[Friend]mFieldData.[Friend]mData == iVal)
				{
					strBuffer.Append(field.Name);
					return;
				}
			}

			((int32)iVal).ToString(strBuffer);
		}

		public static Result<T> Parse<T>(StringView str, bool ignoreCase = false) where T : enum
		{
			var typeInst = (TypeInstance)typeof(T);
			for (var field in typeInst.GetFields())
			{
				if (str.Equals(field.[Friend]mFieldData.mName, ignoreCase))
					return .Ok(*((T*)(&field.[Friend]mFieldData.mData)));
			}

			return .Err;
		}

		public static void GetNames<T>(ref StringView[] strBuffer)
		{
			GetNames(typeof(T), ref strBuffer);
		}

		public static void GetNames(Type type, ref StringView[] strBuffer)
		{
			int i = Count(type);
			if (strBuffer.Count != i)
			{
				// if strBuffer is Stack Allocated and incorrect size this will throw an error
				delete strBuffer;
				strBuffer = new StringView[i];
			}
			i = 0;
			for (let field in type.GetFields())
			{
				strBuffer[i] = field.Name;
				i++;
			}
		}

		public static void GetValues<T>(ref int[] iVals)
		{
			GetValues(typeof(T), ref iVals);
		}

		public static void GetValues(Type type, ref int[] iVals)
		{
			int i = Count(type);
			if (iVals.Count != i)
			{
				// if iVals is Stack Allocated and incorrect size this will throw an error
				delete iVals;
				iVals = new int[i];
			}
			i = 0;
			for (let field in type.GetFields())
			{
				iVals[i] = field.[Friend]mFieldData.[Friend]mData;
				i++;
			}
		}

		public static int Count<T>()
		{
			return Count(typeof(T));
		}

		public static int Count(Type type)
		{
			return type.[Friend]FieldCount;
		}

		/*public override void ToString(String strBuffer) mut
		{
			Type type = GetType();
			int32* iPtr = (int32*)((int)(&this) + (int)type.Size);
			EnumToString(type, strBuffer, *iPtr);
			//EnumToString(GetType(), )
		}*/
	}
}
