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

		enum Kind
		{
			A,
			B
		}

		const bool cNever = false;
		static int sBreakDeferCount;

		static int NestedBreak(int val, bool doBreak)
		{
			switch (val)
			{
			case 0:
				if (doBreak)
				{
					break;
				}
				return 1;
			default:
				return 2;
			}
			return 0;
		}

		static int NestedBreakDefault(int val, bool doBreak)
		{
			switch (val)
			{
			case 0:
				return 1;
			default:
				if (doBreak)
					break;
				return 2;
			}
			return 0;
		}

		static int NestedBreakDefer(int val, bool doBreak)
		{
			switch (val)
			{
			case 0:
				{
					defer { sBreakDeferCount++; }
					if (doBreak)
						break;
				}
				return 1;
			default:
				return 2;
			}
			return 0;
		}

		static int NestedBreakEnum(Kind kind, bool doBreak)
		{
			switch (kind)
			{
			case .A:
				if (doBreak)
					break;
				return 1;
			case .B:
				return 2;
			}
			return 0;
		}

		static int NestedBreakAssign(int val, bool doBreak)
		{
			int result;
			switch (val)
			{
			case 0:
				if (doBreak)
				{
					result = 10;
					break;
				}
				result = 11;
				fallthrough;
			case 1:
				result = 12;
			default:
				result = 13;
			}
			return result;
		}

		static int LabeledBreakFromInnerSwitch(int val, int innerVal)
		{
			OUTER:
			switch (val)
			{
			case 0:
				switch (innerVal)
				{
				case 0:
					break OUTER;
				default:
					return 3;
				}
				return 1;
			default:
				return 2;
			}
			return 0;
		}

		// These breaks don't leave the outer switch, so every case returns and no trailing return is needed
		static int InnerLoopBreak(int val)
		{
			switch (val)
			{
			case 0:
				for (int i < 3)
				{
					if (i == 1)
						break;
				}
				return 1;
			default:
				return 2;
			}
		}

		static int InnerSwitchBreak(int val, int innerVal)
		{
			switch (val)
			{
			case 0:
				switch (innerVal)
				{
				case 0:
					break;
				default:
					return 3;
				}
				return 1;
			default:
				return 2;
			}
		}

		static int ConstFalseBreak(int val)
		{
			switch (val)
			{
			case 0:
				if (cNever)
					break;
				return 1;
			default:
				if (cNever)
					break;
				return 2;
			}
		}

		[Test]
		public static void TestNestedBreak()
		{
			Test.Assert(NestedBreak(0, true) == 0);
			Test.Assert(NestedBreak(0, false) == 1);
			Test.Assert(NestedBreak(1, true) == 2);

			Test.Assert(NestedBreakDefault(1, true) == 0);
			Test.Assert(NestedBreakDefault(1, false) == 2);
			Test.Assert(NestedBreakDefault(0, true) == 1);

			sBreakDeferCount = 0;
			Test.Assert(NestedBreakDefer(0, true) == 0);
			Test.Assert(NestedBreakDefer(0, false) == 1);
			Test.Assert(sBreakDeferCount == 2);

			Test.Assert(NestedBreakEnum(.A, true) == 0);
			Test.Assert(NestedBreakEnum(.A, false) == 1);
			Test.Assert(NestedBreakEnum(.B, true) == 2);

			Test.Assert(NestedBreakAssign(0, true) == 10);
			Test.Assert(NestedBreakAssign(0, false) == 12);
			Test.Assert(NestedBreakAssign(2, true) == 13);

			Test.Assert(LabeledBreakFromInnerSwitch(0, 0) == 0);
			Test.Assert(LabeledBreakFromInnerSwitch(0, 1) == 3);
			Test.Assert(LabeledBreakFromInnerSwitch(1, 0) == 2);

			Test.Assert(InnerLoopBreak(0) == 1);
			Test.Assert(InnerLoopBreak(1) == 2);
			Test.Assert(InnerSwitchBreak(0, 0) == 1);
			Test.Assert(InnerSwitchBreak(0, 1) == 3);
			Test.Assert(ConstFalseBreak(0) == 1);
			Test.Assert(ConstFalseBreak(1) == 2);
		}
	}
}
