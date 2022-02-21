// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_LINKEDLIST
#endif

using System;
using System.Diagnostics;

using internal System.Collections; // to allow the deletion of LinkedListNode.

namespace System.Collections
{
	public class LinkedList<T> : ICollection<T>, IEnumerable<T>
	{
		// This LinkedList is a doubly-Linked circular list.
		private LinkedListNode<T> mHead;
		private int_cosize mCount;
#if VERSION_LINKEDLIST
		private int32 mVersion;
#endif

		public this()
		{
		}

		public this(IEnumerator<T> collection)
		{
			for (T item in collection)
				AddLast(item);
		}

		public ~this()
		{
			Clear();
		}

		public int Count
		{
			get
			{
				return mCount;
			}
		}

		public LinkedListNode<T> First
		{
			get
			{
				return mHead;
			}
		}

		public LinkedListNode<T> Last
		{
			get
			{
				return mHead == null ? null : mHead.mPrev;
			}
		}

		void ICollection<T>.Add(T value)
		{
			AddLast(value);
		}

		public LinkedListNode<T> AddAfter(LinkedListNode<T> node, T value)
		{
			Debug.Assert(node != null);
			Debug.Assert(node.mList == this, "The LinkedList node already belongs to a LinkedList");
			LinkedListNode<T> result = new LinkedListNode<T>(node.mList, value);
			publicInsertNodeBefore(node.mNext, result);
			return result;
		}

		public void AddAfter(LinkedListNode<T> node, LinkedListNode<T> newNode)
		{
			Debug.Assert(node != null && newNode != null);
			Debug.Assert(node.mList == this, "The LinkedList node does not belong to current LinkedList");
			Debug.Assert(newNode.mList == null, "The LinkedList node already belongs to a LinkedList");
			publicInsertNodeBefore(node.mNext, newNode);
			newNode.mList = this;
		}

		public LinkedListNode<T> AddBefore(LinkedListNode<T> node, T value)
		{
			Debug.Assert(node != null);
			Debug.Assert(node.mList == this, "The LinkedList node does not belong to current LinkedList");
			LinkedListNode<T> result = new LinkedListNode<T>(node.mList, value);
			publicInsertNodeBefore(node, result);
			if (node == mHead)
				mHead = result;
			return result;
		}

		public void AddBefore(LinkedListNode<T> node, LinkedListNode<T> newNode)
		{
			Debug.Assert(node != null && newNode != null);
			Debug.Assert(node.mList == this, "The LinkedList node does not belong to current LinkedList");
			Debug.Assert(newNode.mList == null, "The LinkedList node already belongs to a LinkedList");
			publicInsertNodeBefore(node, newNode);
			newNode.mList = this;
			if (node == mHead)
				mHead = newNode;
		}

		public LinkedListNode<T> AddFirst(T value)
		{
			LinkedListNode<T> result = new LinkedListNode<T>(this, value);
			if (mHead == null)
			{
				publicInsertNodeToEmptyList(result);
			}
			else
			{
				publicInsertNodeBefore(mHead, result);
				mHead = result;
			}
			return result;
		}

		public void AddFirst(LinkedListNode<T> node)
		{
			Debug.Assert(node != null);
			Debug.Assert(node.mList == null, "The LinkedList node already belongs to a LinkedList");

			if (mHead == null)
			{
				publicInsertNodeToEmptyList(node);
			}
			else
			{
				publicInsertNodeBefore(mHead, node);
				mHead = node;
			}
			node.mList = this;
		}

		public LinkedListNode<T> AddLast(T value)
		{
			LinkedListNode<T> result = new LinkedListNode<T>(this, value);
			if (mHead == null)
			{
				publicInsertNodeToEmptyList(result);
			}
			else
			{
				publicInsertNodeBefore(mHead, result);
			}
			return result;
		}

		public void AddLast(LinkedListNode<T> node)
		{
			Debug.Assert(node != null);
			Debug.Assert(node.mList == null, "The LinkedList node already belongs to a LinkedList");

			if (mHead == null)
			{
				publicInsertNodeToEmptyList(node);
			}
			else
			{
				publicInsertNodeBefore(mHead, node);
			}
			node.mList = this;
		}

		public void Clear()
		{
			LinkedListNode<T> current = mHead;
			while (current != null)
			{
				LinkedListNode<T> temp = current;
				current = current.Next; // use Next the instead of "next", otherwise it will loop forever
				delete temp;
			}

			mHead = null;
			mCount = 0;
#if VERSION_LINKEDLIST
			mVersion++;
#endif
		}

		public bool Contains(T value)
		{
			return Find(value) != null;
		}

		public void CopyTo(Span<T> array)
		{
			Debug.Assert(array.Length < Count);

			LinkedListNode<T> node = mHead;
			if (node != null)
			{
				int index = 0;
				repeat
				{
					array[index++] = node.mItem;
					node = node.mNext;
				} while (node != mHead);
			}
		}

		public LinkedListNode<T> Find(T value)
		{
			LinkedListNode<T> node = mHead;
			if (node != null)
			{
				if (value != null)
				{
					repeat
					{
						if (node.mItem == value)
						{
							return node;
						}
						node = node.mNext;
					} while (node != mHead);
				}
				else
				{
					repeat
					{
						if (node.mItem == null)
						{
							return node;
						}
						node = node.mNext;
					} while (node != mHead);
				}
			}
			return null;
		}

		public LinkedListNode<T> FindLast(T value)
		{
			if (mHead == null) return null;

			LinkedListNode<T> last = mHead.mPrev;
			LinkedListNode<T> node = last;
			if (node != null)
			{
				if (value != null)
				{
					repeat
					{
						if (node.mItem == value)
						{
							return node;
						}

						node = node.mPrev;
					} while (node != last);
				}
				else
				{
					repeat
					{
						if (node.mItem == null)
						{
							return node;
						}
						node = node.mPrev;
					} while (node != last);
				}
			}
			return null;
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public bool Remove(T value)
		{
			LinkedListNode<T> node = Find(value);
			if (node != null)
			{
				publicRemoveNode(node);
				return true;
			}
			return false;
		}

		public void Remove(LinkedListNode<T> node)
		{
			Debug.Assert(node != null);
			Debug.Assert(node.mList == this, "The LinkedList node does not belong to current LinkedList");
			publicRemoveNode(node);
		}

		public void RemoveFirst()
		{
			Debug.Assert(mHead != null, "RemoveFirst called on a LinkedList with no nodes");
			publicRemoveNode(mHead);
		}

		public void RemoveLast()
		{
			Debug.Assert(mHead != null, "RemoveLast called on a LinkedList with no nodes");
			publicRemoveNode(mHead.mPrev);
		}

		private void publicInsertNodeBefore(LinkedListNode<T> node, LinkedListNode<T> newNode)
		{
			newNode.mNext = node;
			newNode.mPrev = node.mPrev;
			node.mPrev.mNext = newNode;
			node.mPrev = newNode;
#if VERSION_LINKEDLIST
			mVersion++;
#endif
			mCount++;
		}

		private void publicInsertNodeToEmptyList(LinkedListNode<T> newNode)
		{
			Debug.Assert(mHead == null && mCount == 0, "LinkedList must be empty when this method is called!");
			newNode.mNext = newNode;
			newNode.mPrev = newNode;
			mHead = newNode;
#if VERSION_LINKEDLIST
			mVersion++;
#endif
			mCount++;
		}

		private void publicRemoveNode(LinkedListNode<T> node)
		{
			Debug.Assert(node.mList == this, "Deleting the node from another list!");
			Debug.Assert(mHead != null, "This method shouldn't be called on empty list!");
			if (node.mNext == node)
			{
				Debug.Assert(mCount == 1 && mHead == node, "this should only be true for a list with only one node");
				DeleteAndNullify!(mHead);
			}
			else
			{
				node.mNext.mPrev = node.mPrev;
				node.mPrev.mNext = node.mNext;
				if (mHead == node)
				{
					mHead = node.mNext;
				}
				delete node;
			}
			mCount--;
#if VERSION_LINKEDLIST
			mVersion++;
#endif
		}

		public struct Enumerator : IEnumerator<T>, IResettable
		{
			private LinkedList<T> mList;
			private LinkedListNode<T> mNode;
#if VERSION_LINKEDLIST
			private int32 mVersion;
#endif
			private T mCurrent;
			private int mIndex;

			public this(LinkedList<T> list)
			{
				mList = list;
#if VERSION_LINKEDLIST
				mVersion = list.mVersion;
#endif
				mNode = list.mHead;
				mCurrent = default(T);
				mIndex = 0;
			}

			public T Current
			{
				get
				{
					return mCurrent;
				}
			}

			public bool MoveNext() mut
			{
#if VERSION_LINKEDLIST
				if (mVersion != mList.mVersion)
					Runtime.FatalError("LinkedList changed during enumeration");
#endif

				if (mNode == null)
				{
					mIndex = mList.Count + 1;
					return false;
				}

				++mIndex;
				mCurrent = mNode.mItem;
				mNode = mNode.mNext;
				if (mNode == mList.mHead)
					mNode = null;
				return true;
			}

			public Result<T> GetNext() mut
			{
				if (MoveNext())
					return .Ok(Current);
				return .Err;
			}

			void IResettable.Reset() mut
			{
#if VERSION_LINKEDLIST
				if (mVersion != mList.mVersion)
					Runtime.FatalError("LinkedList changed during enumeration");
#endif

				mCurrent = default(T);
				mNode = mList.mHead;
				mIndex = 0;
			}
		}
	}

	public sealed class LinkedListNode<T>
	{
		public LinkedList<T> mList;
		public LinkedListNode<T> mNext;
		public LinkedListNode<T> mPrev;
		public T mItem;

		public this(T value)
		{
			mItem = value;
		}

		public this(LinkedList<T> list, T value)
		{
			mList = list;
			mItem = value;
		}

		// This class can only be deleted by the LinkedList.
		internal ~this()
		{
		}

		public LinkedList<T> List
		{
			get
			{
				return mList;
			}
		}

		public LinkedListNode<T> Next
		{
			get
			{
				return mNext == null || mNext == mList.[Friend]mHead ? null : mNext;
			}
		}

		public LinkedListNode<T> Previous
		{
			get
			{
				return mPrev == null || this == mList.[Friend]mHead ? null : mPrev;
			}
		}

		public T Value
		{
			get
			{
				return mItem;
			}

			set
			{
				mItem = value;
			}
		}
	}
}
