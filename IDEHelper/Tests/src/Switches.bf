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
			case .Circle(let x, let y, let radius) when x > 0 && y == 10:
				return 12;
			default:
				return 23;
			}
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
		}
	}
}
