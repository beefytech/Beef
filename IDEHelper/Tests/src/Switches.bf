#pragma warning disable 168

using System;

namespace Tests
{
	class Switches
	{
		enum Shape
		{
		    case Rectangle(int x, int y, int width, int height);
		    case Circle(int x, int y, int radius);
		}

		static int Switch0(Result<int> res)
		{
			switch (res)
			{
			case .Ok(let a):
				return 0;
			case .Err(let b):
				 return 1;
			}
		}

		static int Switch1(Shape shape)
		{
			switch (shape)
			{
			case .Circle(let x, let y, let radius) when x == 10:
				return 12;
			default:
				return 23;
			}
		}

		enum ETest
		{
			case A(int a);
			case B(float f);
			case C;
		}

		[Test]
		public static void TestBasics()
		{
			Result<int> val0 = .Ok(1);
			Test.Assert(Switch0(val0) == 0);
			val0 = .Err;
			Test.Assert(Switch0(val0) == 1);

			Shape shape = .Circle(10, 20, 30);
			Test.Assert(Switch1(shape) == 12);

			int val = 123;
			int result = 0;
			switch (val)
			{
			case 0:
				result = 1;
			default:
				SWITCH2:
				switch (val)
				{
				case 2:
					result = 2;
				default:
					result = 3;
					break SWITCH2;
				}

				result = 4;
			}
			Test.Assert(result == 4);

			result = 0;
			const int constVal = 123;
			switch (constVal)
			{
			case 10:
				result = 1;
			case 123:
				result = 2;
			default:
				result = 3;
			}
			Test.Assert(result == 2);

			result = 99;
			const Result<int> iResult = .Err;
			bool eq = iResult case .Ok(ref result);
			Test.Assert(result == 99);

			if (iResult not case .Ok(var result2))
			{
			}
			else
			{
				Test.FatalError();
			}
			Test.Assert(result2 == 0);

			const ETest t = .B(234.5f);
			switch (t)
			{
			case .A(let a):
				result = 1;
			case .B(let b):
				result = (.)b;
			case .C:
				result = 3;
			default:
				result = 4;
			}
			Test.Assert(result == 234);
		}
	}
}
