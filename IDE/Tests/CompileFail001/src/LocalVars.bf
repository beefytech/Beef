#pragma warning disable 168

using System;

namespace IDETest
{
	class LocalVars
	{
		public void Switch1()
		{
			int val;

			Result<int> iResult = .Ok(123);
			switch (iResult)
			{
			case .Ok(out val):
			case .Err:
			}

			int a = val; //FAIL
		}

		public void Switch2()
		{
			int val;

			Result<int> iResult = .Ok(123);
			switch (iResult)
			{
			case .Ok(out val):
			case .Err: val = 1;
			}

			int a = val;
		}

		public void Switch3()
		{
			int val;

			Result<int> iResult = .Ok(123);
			switch (iResult)
			{
			case .Ok(out val):
			case .Err: return;
			}

			int a = val;
		}

		public void Switch4()
		{
			int a = 1;
			int b;

			switch (a)
			{
			case 1:
				b = 1;
			case 2:
			case 3:
				fallthrough;
			case 4:
				b = 2;
			default:
				b = 3;
			}

			int c = b; //FAIL
		}

		public void Switch5()
		{
			int a = 1;
			int b;

			switch (a)
			{
			case 1:
				b = 1;
			case 2:
				fallthrough;
			case 3:
				fallthrough;
			case 4:
				b = 2;
			default:
				b = 3;
			}

			int c = b;
		}

		public void While1()
		{
			int a = 1;
			int b;

			while (true)
			{
				if (a == 2)
					break;
				b = 2;
			}

			int c = b; //FAIL
		}

		public void While2()
		{
			int a = 1;
			int b;

			while (true)
			{
				if (a == 2)
					return;
				b = 2;
				break;
			}

			int c = b; 
		}

		public void While3()
		{
			int a = 1;
			int b;

			while (a == 1)
			{
				if (a == 2)
					return;
				b = 2;
				break;
			}

			int c = b; //FAIL
		}

		public void While4()
		{
			for (int i < 2)
			{
				int a = 1;
				int b;

				while (true)
				{
					if (a == 2)
						return;
					b = 2;
					break;
				}

				int c = b; 
			}
		}

		public void While5()
		{
			for (int i < 2)
			{
				int a = 1;
				int b;

				while (a == 1)
				{
					if (a == 2)
						return;
					b = 2;
					break;
				}

				int c = b; //FAIL
			}
		}
	}
}
