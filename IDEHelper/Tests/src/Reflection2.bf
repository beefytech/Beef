#pragma warning disable 168

using System;

namespace Tests
{
	class Reflection2
	{
		[Test]
		public static void TestBasics()
		{
			const Type t = typeof(StringView);
			int fieldCount = t.FieldCount;
		}
	}
}
