using System;
using System.Collections;

namespace Tests
{
	class Queues
	{
		static void CheckQueue(Queue<int> queue, params int[] expected)
		{
			Test.Assert(queue.Count == expected.Count);
			for (int i < expected.Count)
				Test.Assert(queue[i] == expected[i]);
		}

		[Test]
		public static void TestRemoveAt()
		{
			Queue<int> queue = scope .();
			queue.Add(1);
			queue.Add(2);
			queue.Add(3);
			queue.RemoveAt(1);
			queue.Add(4);
			CheckQueue(queue, 1, 3, 4);

			// Wrapped around the end of the buffer
			queue.Clear();
			for (int i < 4)
				queue.Add(i);
			queue.PopFront();
			queue.PopFront();
			queue.Add(4);
			queue.Add(5);
			queue.RemoveAt(1);
			CheckQueue(queue, 2, 4, 5);
			queue.Add(6);
			CheckQueue(queue, 2, 4, 5, 6);

			// Removing through the enumerator
			for (var val in queue)
			{
				if (val % 2 == 0)
					@val.Remove();
			}
			CheckQueue(queue, 5);
		}
	}
}
