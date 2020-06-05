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

			public typealias AliasA5 = (int, T);
			public typealias AliasA6<T2> = (T, T2);

			public typealias AliasA7 = T[];
			public typealias AliasA8 = T[3];

			public delegate T Zag();
		}

		public static void Test<T>()
		{
			T LocalA(int16 a)
			{
				return default(T);
			}

			ClassA<T>.AliasA6<float> t0 = (default(T), 1.2f);
			ClassA<T>.AliasA4<int16> dlg0 = scope => LocalA;

			ClassA<T>.AliasA7 arr0 = scope T[123];
			T[3] arr1 = .(default, default, default);
			ClassA<T>.AliasA8 arr2 = arr1;
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

			var t0 = (123, 1.2f);
			ClassA<float>.AliasA5 t1 = t0;

			var t2 = (1.2f, 3.4);
			ClassA<float>.AliasA6<double> v3 = t2;
		}
	}
}
