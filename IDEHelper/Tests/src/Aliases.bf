#pragma warning disable 168

using System.Collections;
using System;

namespace Tests
{
	class Aliases
	{
		class ClassA<T>
		{
			public typealias AliasA0 = int32;
			public typealias AliasA1 = List<T>;
			public typealias AliasA2<T2> = Dictionary<T, T2>;

			public typealias AliasA3 = delegate T();
			public typealias AliasA4<T2> = delegate T(T2 val);

			public delegate T Zag();
		}

		[Test]
		public static void TestBasics()
		{
			ClassA<float>.AliasA0 a0 = default;
			a0 = 123;

			ClassA<float>.AliasA1 list = scope List<float>();
			Dictionary<float, int16> dict = scope ClassA<float>.AliasA2<int16>();

			delegate double() dlg = default;
			ClassA<double>.AliasA3 dlg2 = dlg;

			delegate double(char8) dlg3 = default;
			ClassA<double>.AliasA4<char8> dlg4 = dlg3;
		}
	}
}
