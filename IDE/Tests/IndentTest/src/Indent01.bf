namespace IDETest
{
	class Indent01
	{
		static void Test01()
		{
			if (true)

			//Test01
		}

		static void Test02()
		{
			if (true)
				if (true)

			//Test02
		}

		static void Test03()
		{
			if (true)
				if (true)
					;
			    else

			//Test03
		}

		static void Test04()
		{
			if (true)
				if (true)
					for ()

			//Test04
		}

		static void Test05()
		{
			if (true)
				if (true)
					;
			    else
					for ()

			//Test05
		}

		static void Test06()
		{
			if (true)
				if (true)
					;
			    else if (true)

			//Test06
		}

		static void Test07()
		{
			if (true)
				if (true)
					;
			    else if (true)
					for (int i < 10)

			//Test07
		}

		static void Test08()
		{
			if (true)
				if (true)
					;
			    else if (true)
					for (int i < 10)
						;
			    else

			//Test08
		}

		static void Test09()
		{
			if (true)
				if (true)
					;
			    else if (true)
					for (int i < 10)
						;
			    else
					;

			for (int i < 20)

			//Test09
		}
	}
}
