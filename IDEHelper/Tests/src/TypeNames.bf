#pragma warning disable 168

using System;

namespace Tests
{
	/// Composing a type's full name.
	///
	/// A composed name walks type ids the runtime type table may have no entry for: a field's
	/// type, a splat member's, a base type's, an array's element, a specialization's
	/// unspecialized form. GetType answers null for one of those, and writing the name
	/// through that null crashed the process rather than answering anything, which takes
	/// down any walk over Type.Types that names what it finds.
	class TypeNames
	{
		struct Pair
		{
			public int32 mA;
			public float mB;
		}

		class Box<T>
		{
			public T mValue;
		}

		[Test]
		public static void TestNames()
		{
			(int32 a, float b) named = (1, 2.0f);
			(int32, int32) bare = (3, 4);
			Test.Assert(typeof(decltype(named)).GetFullName(.. scope .()) == "(int32 a, float b)");
			Test.Assert(typeof(decltype(bare)).GetFullName(.. scope .()) == "(int32, int32)");
			Test.Assert(typeof(Pair).GetFullName(.. scope .()) == "Tests.TypeNames.Pair");
			Test.Assert(typeof(Box<int32>).GetFullName(.. scope .()) == "Tests.TypeNames.Box<int32>");
		}

		[Test]
		public static void TestComposedNames()
		{
			Test.Assert(typeof(int32*).GetFullName(.. scope .()) == "int32*");
			Test.Assert(typeof(int32[]).GetFullName(.. scope .()) == "int32[]");
			Test.Assert(typeof(int32[4]).GetFullName(.. scope .()) == "int32[4]");
			Test.Assert(typeof(int32[2][3]).GetFullName(.. scope .()) == "int32[2][3]");
			Test.Assert(typeof(Pair*).GetFullName(.. scope .()) == "Tests.TypeNames.Pair*");
			Test.Assert(typeof(Box<Pair>*).GetFullName(.. scope .()) == "Tests.TypeNames.Box<Tests.TypeNames.Pair>*");
		}
	}
}
