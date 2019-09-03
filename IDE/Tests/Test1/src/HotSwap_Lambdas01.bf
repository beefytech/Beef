#pragma warning disable 168

namespace IDETest
{
	class HotSwap_Lambdas01
	{
		class ClassA
		{
			public delegate int() mDlg0 ~ delete _;
			public delegate int() mDlg1 ~ delete _;
			public delegate int() mDlg2 ~ delete _;

			int mA = 123;

			public this()
			{
				int val = 234;

				mDlg0 = new () =>
				{
					int ret = 200;
					/*Dlg0_0
					ret += mA;
					*/
					return ret;
				};

				mDlg1 = new () =>
				{
					int ret = 300;
					//*Dlg1_0
					ret += mA;
					/*@*/
					return ret;
				};

				mDlg2 = new () =>
				{
					int ret = 400;
					//*Dlg2_0
					ret += val;
					/*@*/
					ret += mA;
					return ret;
				};
			}
		}

		public static void Test()
		{
			//Test_Start
			ClassA ca = scope .();
			int val0 = ca.mDlg0();
			int val1 = ca.mDlg1();
			int val2 = ca.mDlg2();

			val0 = ca.mDlg0();
			val1 = ca.mDlg1();
			val2 = ca.mDlg2();
		}
	}
}
