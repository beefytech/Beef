using System;
using System.Collections;

namespace Tests
{
	class ScopeStackMark
	{
		class Item
		{
			public static bool sCollect;

			public ~this()
			{
				if (sCollect)
				{
					sCollect = false;
					GC.Collect(false);
				}
			}
		}

		// The appended list makes a scope Owner one the GC marks through GCMarkMembers
		class Owner
		{
			public append List<Item> mItems ~ ClearAndDeleteItems!(_);
		}

		[Test]
		public static void TestCollectDuringScopeDtor()
		{
			// The first item ~Owner deletes runs a collection while the other nine are still in mItems,
			//  so the owner must still be marked while its destructor runs
			{
				let owner = scope Owner();
				for (int i < 10)
					owner.mItems.Add(new .());
				Item.sCollect = true;
			}
			Test.Assert(!Item.sCollect);

			// The same through an anonymous subclass, whose methods are not slotted when the allocation is made
			{
				let owner = scope Owner()
					{
						public override void ToString(String strBuffer)
						{
							strBuffer.Append("Anonymous");
						}
					};
				for (int i < 10)
					owner.mItems.Add(new .());
				Item.sCollect = true;
			}
			Test.Assert(!Item.sCollect);

			// scope:: in a loop: all the owners are destroyed when the method ends
			for (int round < 3)
			{
				let owner = scope:: Owner();
				for (int i < 10)
					owner.mItems.Add(new .());
			}
			Item.sCollect = true;
		}
	}
}
