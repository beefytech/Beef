#pragma warning disable 168

using System;
using System.Collections;

namespace IDETest
{
	class Visualizers
	{
		enum Color
		{
			Red,
			Green
		}

		enum Shape
		{
			case Circle(float radius);
			case Rect(int w, int h);
		}

		[AllowDuplicates]
		enum Flags
		{
			None = 0,
			A = 1,
			B = 2,
			AB = 3
		}

		struct Point
		{
			public int mX;
			public int mY;
		}

		class Node
		{
			public int mVal;
			public Node mNext;
		}

		delegate int IntDlg(int a);

		static int Twice(int a) => a * 2;

		public static void Test()
		{
			String str = scope .("Hello");
			String longStr = scope .();
			for (int i < 20)
				longStr.Append("abcde");
			StringView sv = "World";
			char8* cstr = "cstr";
			List<int> list = scope .();
			list.Add(1);
			list.Add(2);
			list.Add(3);
			List<String> strList = scope .();
			strList.Add("one");
			strList.Add("two");
			Dictionary<String, int> dict = scope .();
			dict["a"] = 1;
			HashSet<int> set = scope .();
			set.Add(5);
			set.Add(7);
			Queue<int> queue = scope .();
			queue.Add(9);
			int[] arr = new .(10, 20, 30);
			defer delete arr;
			int[3] sizedArr = .(4, 5, 6);
			Span<int> span = .(arr);
			Point pt = .() { mX = 3, mY = 4 };
			Point* ptPtr = &pt;
			Node node = scope .() { mVal = 1 };
			node.mNext = scope .() { mVal = 2 };
			Object obj = node;
			Color color = .Green;
			Shape shape = .Rect(3, 4);
			Flags flags = .AB;
			(int, float) tuple = (1, 2.5f);
			int? someVal = 7;
			int? nothing = null;
			bool b = true;
			char8 c8 = 'x';
			char32 c32 = 'y';
			float f = 1.5f;
			double d = 2.25;
			int64 big = 1234567890123;
			uint8 u8 = 200;
			Object nullObj = null;
			IntDlg dlg = scope => Twice;
			function int(int a) fn = => Twice;
			Variant variant = Variant.Create(42);
			Result<int> result = .Ok(3);
			//Visualizers_Break
			int dummy = 0;
		}
	}
}
