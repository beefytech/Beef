using System;

namespace Tests
{
	class ShortCircuit
	{
		const bool cOff = false;
		const bool cOn = true;
		static bool sOff = false;
		static int sCalls;

		static Result<int> Get(int val)
		{
			sCalls++;
			if (val > 0)
				return .Ok(val);
			return .Err;
		}

		static bool Check()
		{
			sCalls++;
			return true;
		}

		[Test]
		public static void TestConstFalseAndCase()
		{
			sCalls = 0;
			bool taken = false;
			if (false && (Get(5) case .Ok(let a)))
				taken = true;
			Test.Assert(!taken);
			if (cOff && (Get(5) case .Ok(let b)))
				taken = true;
			Test.Assert(!taken);
			if (false && (Get(5) case .Ok(let c)) && (c > 1))
				taken = true;
			Test.Assert(!taken);
			while (cOff && (Get(5) case .Ok(let d)))
			{
				taken = true;
				break;
			}
			Test.Assert(!taken);
			Test.Assert(sCalls == 0);

			if (false && Check())
				taken = true;
			Test.Assert(!taken);
			if (sOff && (Get(5) case .Ok(let e)))
				taken = true;
			Test.Assert(!taken);
			Test.Assert(sCalls == 0);
			int val = 0;
			if (cOn && (Get(5) case .Ok(let f)))
				val = f;
			Test.Assert(val == 5);
			Test.Assert(sCalls == 1);
		}
	}
}
