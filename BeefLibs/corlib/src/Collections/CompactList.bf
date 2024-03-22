namespace System.Collections;

[Union]
struct CompactList<T> : IDisposable, IEnumerable<T>
{
	public bool IsEmpty => true;
	public int Count => 0;

	public void Dispose() mut {}
	public void Add(T value) mut {}
	public bool Remove(T value) mut => false;
	public void RemoveAt(int idx) mut {}

	public T this[int idx]
	{
		get mut => default;
		set mut {}
	}

	public static operator Span<T>(ref Self self) => default;

	public Enumerator GetEnumerator() => .(this);

	public struct Enumerator : IEnumerator<T>
	{
	    private CompactList<T> mList;
	    private int mIndex;
	    private T mCurrent;

	    public this(CompactList<T> list)
	    {
	        mList = list;
	        mIndex = 0;
	        mCurrent = default;
	    }

	    public void Dispose()
	    {
	    }

	    public bool MoveNext() mut
	    {
	        if ((uint(mIndex) < uint(mList.Count)))
	        {
	            mCurrent = mList[mIndex];
	            mIndex++;
	            return true;
	        }			   
	        return MoveNextRare();
	    }

	    private bool MoveNextRare() mut
	    {
	    	mIndex = mList.Count + 1;
	        mCurrent = default;
	        return false;
	    }

	    public T Current
	    {
	        get
	        {
	            return mCurrent;
	        }
	    }


		public int Index
		{
			get
			{
				return mIndex - 1;
			}				
		}

		public int Length
		{
			get
			{
				return mList.Count;
			}				
		}

	    public void Reset() mut
	    {
	        mIndex = 0;
	        mCurrent = default;
	    }

		public Result<T> GetNext() mut
		{
			if (!MoveNext())
				return .Err;
			return Current;
		}

		public void Remove() mut
		{
			int curIdx = mIndex - 1;
			mList.RemoveAt(curIdx);
			mIndex = curIdx;
		}

		public void RemoveFast() mut
		{
			int curIdx = mIndex - 1;
			int lastIdx = mList.Count - 1;
			if (curIdx < lastIdx)
		        mList[curIdx] = mList[lastIdx];
			mList.RemoveAt(lastIdx);
			mIndex = curIdx;
		}
	}
}

extension CompactList<T> where T : class
{
	Object mObject;

	[Inline]
	Object NullSentinel => Internal.UnsafeCastToObject((.)1);

	[Inline]
	public new bool IsEmpty => mObject == null;
	
	public int Count
	{
		get
		{
			if (mObject == NullSentinel)
				return 1;
			if (var list = mObject as List<T>)
				return list.Count;
			return (mObject != null) ? 1 : 0;
		}
	}

	public T this[int idx]
	{
		get mut
		{
			if (mObject == NullSentinel)
			{
				Runtime.Assert(idx == 0);
				return null;
			}
			if (var list = mObject as List<T>)
				return list[idx];
			Runtime.Assert(!IsEmpty);
			Runtime.Assert(idx == 0);
			return *(T*)&mObject;
		}

		set mut
		{
			if ((mObject != NullSentinel) && (var list = mObject as List<T>))
			{
				list[idx] = value;
				return;
			}
			Runtime.Assert(idx == 0);
			if (value == null)
				mObject = NullSentinel;
			else
				mObject = value;
		}
	}

	public new void Dispose() mut
	{
		if (var list = mObject as List<T>)
		{
			delete list;
			mObject = null;
		}
	}

	public new void Add(T value) mut
	{
		if (IsEmpty)
		{
			if (value == null)
				mObject = NullSentinel;
			else
				mObject = value;
			return;
		}

		var list = mObject as List<T>;
		if (list == null)
		{
			list = new .();
			list.Add((T)mObject);
			mObject = list;
		}
		list.Add(value);
	}

	public new bool Remove(T value) mut
	{
		if (var list = mObject as List<T>)
			return list.Remove(value);
		if (((value == null) && (mObject == NullSentinel)) ||
			(value == mObject))
		{
			mObject = null;
			return true;
		}
		return false;
	}

	public new void RemoveAt(int idx) mut
	{
		Runtime.Assert(!IsEmpty);
		if (var list = mObject as List<T>)
		{
			list.RemoveAt(idx);
			return;
		}
		Runtime.Assert(idx == 0);
		mObject = null;
	}


	public new static operator Span<T>(ref Self self)
	{
		if (self.IsEmpty)
			return default;
		self.MakeList();
		return (Span<T>)(List<T>)self.mObject;
	}

	void MakeList() mut
	{
		if (IsEmpty)
			return;
		var list = mObject as List<T>;
		if (list == null)
		{
			list = new .();
			list.Add((T)mObject);
			mObject = list;
		}
	}
}

extension CompactList<T> where T : ValueType
{
	enum State
	{
		case Empty;
		case Single(T value);
		case Multiple(List<T> list);
	}

	State mState;

	public this()
	{
		this = default;
	}

	public bool IsEmpty => mState case .Empty;

	public int Count
	{
		get
		{
			switch (mState)
			{
			case .Empty:
				return 0;
			case .Single:
				return 1;
			case .Multiple(let list):
				return list.Count;
			}
		}
	}

	public ref T this[int idx]
	{
		get mut
		{
			switch (mState)
			{
			case .Empty:
				Runtime.FatalError();
			case .Single(var ref value):
				Runtime.Assert(idx == 0);
				return ref value;
			case .Multiple(let list):
				return ref list[idx];
			}
		}
	}

	public new static operator Span<T>(ref Self self)
	{
		switch (self.mState)
		{
		case .Empty:
			Runtime.FatalError();
		case .Single(var ref value):
			return .(&value, 1);
		case .Multiple(let list):
			return list;
		}
	}

	public new void Dispose() mut
	{
		if (mState case .Multiple(let list))
		{
			delete list;
			mState = .Empty;
		}
	}

	public new void Add(T value) mut
	{
		if (mState case .Empty)
		{
			mState = .Single(value);
			return;
		}
		if (mState case .Single(let prevValue))
			mState = .Multiple(new List<T>()..Add(prevValue));
		if (mState case .Multiple(let list))
			list.Add(value);
	}

	public new bool Remove(T value) mut
	{
		if (mState case .Empty)
			return false;
		if (mState case .Single(let prevValue))
		{
			if (prevValue == value)
			{
				mState = .Empty;
				return true;
			}
			return false;
		}
		if (mState case .Multiple(let list))
			return list.Remove(value);
		return false;
	}

	public new void RemoveAt(int idx) mut
	{
		Runtime.Assert(!IsEmpty);
		if (mState case .Multiple(let list))
		{
			list.RemoveAt(idx);
			return;
		}
		Runtime.Assert(idx == 0);
		mState = .Empty;
	}
}

#if TEST
class CompactListTest
{
	[Test]
	public static void TestStruct()
	{
		int itrCount = 0;
		CompactList<int32> cl = default;
		for (var i in cl)
		{
			itrCount++;
		}
		Test.Assert(cl.IsEmpty);
		Test.Assert(itrCount == 0);
		Test.Assert(cl.Count == 0);
		cl.Add(123);
		Test.Assert(cl[0] == 123);
		Test.Assert(cl.Count == 1);
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == 123);
			itrCount++;
		}
		Test.Assert(itrCount == 1);
		itrCount = 0;
		for (var val in (Span<int32>)cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == 123);
			itrCount++;
		}
		Test.Assert(itrCount == 1);
		cl[0] = 234;
		Test.Assert(cl[0] == 234);
		cl.Add(345);
		Test.Assert(!cl.IsEmpty);
		itrCount = 0;
		for (var val in (Span<int32>)cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == 234);
			if (@val.Index == 1)
				Test.Assert(val == 345);
			itrCount++;
		}
		itrCount = 0;
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == 234);
			if (@val.Index == 1)
			{
				Test.Assert(val == 345);
				@val.Remove();
			}
			itrCount++;
		}
		Test.Assert(cl.Count == 1);
		cl.Dispose();
	}

	[Test]
	public static void TestStructPtr()
	{
		int32 val123 = 123;
		int32 val234 = 234;
		int32 val345 = 345;

		int itrCount = 0;
		CompactList<int32*> cl = default;
		for (var i in cl)
		{
			itrCount++;
		}
		Test.Assert(cl.IsEmpty);
		Test.Assert(itrCount == 0);
		Test.Assert(cl.Count == 0);
		cl.Add(&val123);
		Test.Assert(cl[0] == &val123);
		Test.Assert(cl.Count == 1);
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == &val123);
			itrCount++;
		}
		Test.Assert(itrCount == 1);
		itrCount = 0;
		for (var val in (Span<int32*>)cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == &val123);
			itrCount++;
		}
		Test.Assert(itrCount == 1);
		cl[0] = &val234;
		Test.Assert(cl[0] == &val234);
		cl.Add(&val345);
		Test.Assert(!cl.IsEmpty);
		itrCount = 0;
		for (var val in (Span<int32*>)cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == &val234);
			if (@val.Index == 1)
				Test.Assert(val == &val345);
			itrCount++;
		}
		itrCount = 0;
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == &val234);
			if (@val.Index == 1)
			{
				Test.Assert(val == &val345);
				@val.Remove();
			}
			itrCount++;
		}
		Test.Assert(cl.Count == 1);
		cl.Dispose();
	}

	[Test]
	public static void TestClass()
	{
		int itrCount = 0;
		CompactList<String> cl = default;
		for (var i in cl)
		{
			itrCount++;
		}
		Test.Assert(cl.IsEmpty);
		Test.Assert(itrCount == 0);
		Test.Assert(cl.Count == 0);
		cl.Add(null);
		Test.Assert(cl[0] == null);
		Test.Assert(cl.Count == 1);
		cl[0] = "123";
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == "123");
			itrCount++;
		}
		Test.Assert(itrCount == 1);
		cl[0] = "234";
		Test.Assert(cl[0] == "234");
		cl.Add("345");
		Test.Assert(!cl.IsEmpty);
		itrCount = 0;
		for (var val in (Span<String>)cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == "234");
			if (@val.Index == 1)
				Test.Assert(val == "345");
			itrCount++;
		}
		itrCount = 0;
		for (var val in cl)
		{
			if (@val.Index == 0)
				Test.Assert(val == "234");
			if (@val.Index == 1)
			{
				Test.Assert(val == "345");
				@val.Remove();
			}
			itrCount++;
		}
		Test.Assert(cl.Count == 1);
		cl.Dispose();
	}
}
#endif