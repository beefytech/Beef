using System;

namespace Tests
{
	class Generics2
	{
		struct TestFunc<T, Del>
		{
			private int mId;
			private Del mComparer;

			public static TestFunc<T, Del> Create(int id, Del comparer)
			{
				return .()
				{
					mId = id,
					mComparer = comparer
				};
			}

			public bool CheckDlg(T item)
			{
				return false;
			}

			public bool CheckDlg(T item) where Del : delegate bool(T)
			{
				return mComparer(item);
			}

			public bool CheckDlg(T item) where Del : delegate bool(int, T)
			{
				return mComparer(mId, item);
			}

			public bool CallCheck(T val)
			{
				return CheckDlg(val);
			}
		}

		[Test]
		public static void TestBasics()
		{
			let testF = TestFunc<String, delegate bool(String)>.Create(10, scope (s) => s == "Str");
			Test.Assert(testF.CallCheck("Str"));
			Test.Assert(!testF.CallCheck("Str2"));
		}
	}
}
