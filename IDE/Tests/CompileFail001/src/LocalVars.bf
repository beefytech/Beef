#pragma warning disable 168

using System;

namespace IDETest
{
	class LocalVars
	{
		public void If1()
		{
			int a;
			int b = 123;
			if (b == 234)
			{
				a = 234;
				return;
			}
			b = a; //FAIL
		}

		public void If2(out int a) //FAIL
		{
			int b = 123;
			if (b == 234)
				return;
			a = 234;
			b = a;
		}

		public static void If3(out int a)
		{
			int b = 123;
		    if (b == 234)
			{
		        a = 1;
			}
		    else
		    {
		        a = 2;
		        return;
			}
		}

		public void For1(out int a)
		{
			for (int b < 2)
				a = 9;
		}

		public void For2(out int a) //FAIL
		{
			int b = 123;
			for (int c < b)
				a = 9;
		}

		public void Do1(out int a) //FAIL
		{
			int b = 123;
			do
			{
				if (b == 234)
					break;
				a = 9;
			}
			b = a;
		}

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

		public void Switch6()
		{
			int val;

			Result<int> iResult = .Ok(123);
			switch (iResult)
			{
			case .Ok(out val):
			case .Err: break;
			}

			int a = val; //FAIL
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

		public bool GetVal(out int a)
		{
			a = 1;
			return true;
		}

		public void Local1()
		{
			if ((GetVal(var a)) && (GetVal(var b)))
			{
				int c = a;
				int d = b;
			}

			int e = a;
			int f = b; //FAIL
		}

		public void Local2()
		{
			int a;
			int b;
			if ((GetVal(out a)) && (GetVal(out b)))
			{
				int c = a;
				int d = b;
			}

			int e = a;
			int f = b; //FAIL
		}

		public void Local3()
		{
			if ((GetVal(var a)) || (GetVal(var b))) //FAIL
			{
				int c = a;
				int d = b;
			}

			int e = a;
			int f = b; //FAIL
		}

		public void Local4()
		{
			int a;
			int b;
			if ((GetVal(out a)) || (GetVal(out b)))
			{
				int c = a;
				int d = b; //FAIL
			}

			int e = a;
			int f = b; //FAIL
		}

		public void Local5()
		{
			if (!((GetVal(var a)) && (GetVal(var b)))) //FAIL
			{

			}
		}

		public void Local6()
		{
			int b;
			if (!((GetVal(var a)) && (GetVal(out b))))
			{

			}
		}

		public void Local7()
		{
			int a = 1;
			int b;
			int c;
			int d;

			if ((a == 1) && ({b = 2; if (a == 1) {c = 1;} a == 1}))
			{
				int a2 = a;
				int b2 = b;
				int c2 = c; //FAIL
			}
			int a3 = a;
			int b3 = b; //FAIL
			int c3 = c; //FAIL
		}

		Result<int> Read()
		{
		    return 0;
		}

		public void Local8()
		{
		    int read;
		    loop: repeat
		    {
		        switch (Read())
		        {
		            case .Err: return;
		            case .Ok(let val): read = val;
		        }
		    }
		    while (read > 0);
		}

		public void Local9()
		{
		    int read;
		    loop: repeat
		    {
		        switch (Read())
		        {
		            case .Err: break loop;
		            case .Ok(let val): read = val;
		        }
				int a = read;
		    }
		    while (read > 0);
		}

		public void Local10()
		{
		    int read;
		    loop: repeat
		    {
		        switch (Read())
		        {
		            case .Err: break;
		            case .Ok(let val): read = val;
		        }
				int a = read; //FAIL
		    }
		    while (read > 0); //FAIL
		}

		public void Local11()
		{
			int a = 123;

		    int read;
		    Loop: repeat
		    {
		        break;
		    }
		    while (read > 0);
		}

		public void Local12()
		{
			int a = 123;

		    int read;
		    Loop: repeat
		    {
				if (a == 123)
		        	break;
		    }
		    while (read > 0); //FAIL
		}

		public void Local13()
		{
			int a = 123;
			int b;
			switch (a)
			{
			default: b = 0;
			}
			int c = b;
		}

		public void Local14()
		{
			int a = 123;
			int b;
			switch (a)
			{
			default: b = 0; break;
			}
			int c = b;
		}

		public void Local15()
		{
			int a = 123;
			int b;
			switch (a)
			{
			default: break;
			}
			int c = b; //FAIL
		}
	}
}
