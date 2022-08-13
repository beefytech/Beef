#pragma warning disable 168

using System;

namespace IDETest
{
	class UsingFields
	{
		struct Vector2Int
		{
		    [Union]
			struct XWidth
			{
				public int x;
				public int width;

				public int GetX() => x;
			}
		    [Union] struct YHeight : this(int y, int height);

		    public using XWidth xWidth;
		    public using YHeight yHeight;
		}

		struct TestVec : Vector2Int
		{
		}

		public static void Test()
		{
			Vector2Int v0;
			v0.x = 123;
			v0.y = 234;

			TestVec v1;
			v1.x = 345;
			v0.y = 456;

			//Test_Start
			int v = v0.GetX();
		}
	}
}