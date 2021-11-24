#pragma warning disable 168

using System;

namespace Tests
{
	class Reflection2
	{
		public typealias RemovePtr<T> = comptype(RemovePtr(typeof(T)));

		[Comptime]
		public static Type RemovePtr(Type type)
		{
			if (type.IsPointer)
				return type.UnderlyingType;

			return type;
		}

		[Test]
		public static void TestBasics()
		{
			const Type t = typeof(StringView);
			int fieldCount = t.FieldCount;

			Test.Assert(typeof(RemovePtr<int32>) == typeof(int32));
			Test.Assert(typeof(RemovePtr<uint32*>) == typeof(uint32));
		}
	}
}
