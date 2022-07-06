#pragma warning disable 168

using System;
using System.Interop;
using System.Collections;

namespace Tests
{
	class Expressions
	{
		[Test]
		public static void TestBasics()
		{
			int num = -1;
			if ( {
				    var obj = scope Object();
				    num > 999
				 })
			{
				Test.FatalError();
			}

			String inStr = "Abc";
			bool b =
			{
				inStr.Length > 1 || 2 == 3
			};
		}

		static void GetName<T>(String str)
		{
			str.Append(nameof(T));
		}

		[Test]
		public static void TestNameof()
		{
			var point = (x: 3, y: 4);

			Test.Assert(nameof(System) == "System");
			Test.Assert(nameof(System.Collections) == "Collections");
			Test.Assert(nameof(point) == "point");
			Test.Assert(nameof(point.x) == "x");
			Test.Assert(nameof(Tests.Expressions) == "Expressions");
			Test.Assert(nameof(c_int) == "c_int");
			Test.Assert(nameof(List<int>) == "List");
			Test.Assert(nameof(List<int>.Add) == "Add");
			Test.Assert(nameof(TestBasics) == "TestBasics");
			Test.Assert(GetName<String>(.. scope .()) == "T");
		}
	}
}
