using System.Collections;
namespace utils;

class StableIndexedList<T>
{
	public List<T> mList = new .() ~ delete _;
	public List<int32> mFreeIndices = new .() ~ delete _;

	public this()
	{
		mList.Add(default); // Reserve idx 0
	}

	public ref T this[int idx]
	{
		get
		{
			return ref mList[idx];
		}

		set
		{
			mList[idx] = value;
		}
	}

	public int Add(T val)
	{
		if (!mFreeIndices.IsEmpty)
		{
			int32 idx = mFreeIndices.PopBack();
			mList[idx] = val;
			return idx;
		}
		mList.Add(val);
		return mList.Count - 1;
	}

	public void RemoveAt(int idx)
	{
		if (idx == mList.Count - 1)
		{
			mList.PopBack();
		}
		else
		{
			mList[idx] = default;
			mFreeIndices.Add((.)idx);
		}
	}

	public List<T>.Enumerator GetEnumerator() => mList.GetEnumerator();

	public T SafeGet(int idx)
	{
		if ((idx < 0) || (idx > mList.Count))
			return default;
		return mList[idx];
	}
}