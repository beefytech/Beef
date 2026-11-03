#pragma warning disable 168

namespace IDETest
{
	class Stepping_Delete
	{
		class Thing
		{
			public int mValue = 3;
		}

		static int sCount;

		static T Make<T>() where T : new, class
		{
			sCount++;
			return new T();
		}

		static Thing MakeThing()
		{
			sCount++;
			return new Thing();
		}

		static void MarkA() { sCount++; }
		static void MarkB() { sCount++; }
		static void MarkC() { sCount++; }

		public static void Test()
		{
			//Stepping_Delete_A
			delete Make<Thing>();
			MarkA();
			//Stepping_Delete_B
			delete MakeThing();
			MarkB();
			var thing = new Thing();
			//Stepping_Delete_C
			delete thing;
			MarkC();
		}
	}
}
