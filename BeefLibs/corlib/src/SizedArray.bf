using System.Collections;
namespace System
{
	[AlwaysInclude]
	struct SizedArray<T, CSize> : IEnumerable<T> where CSize : const int
	{
		protected T[CSize] mVal;

		public int Count
		{
			[Inline]
			get
			{
				return CSize;
			}
		}	

		

		public explicit static operator T[CSize] (Self val)
		{
			return val.mVal;
		}

		public implicit static operator Span<T> (in Self val)
		{
#unwarn
			return .(&val.mVal, CSize);
		}

		public override void ToString(String strBuffer) mut
		{
			if (typeof(T) == typeof(char8))
			{
				int len = 0;
				for (; len < CSize; len++)
				{
					if (mVal[len] == default)
						break;
				}
				strBuffer.Append((char8*)&mVal, len);
				return;
			}

			strBuffer.Append('(');
			for (int i < CSize)
			{
				if (i != 0)
					strBuffer.Append(", ");
				mVal[i].ToString(strBuffer);
			}
			strBuffer.Append(')');
		}

		public Enumerator GetEnumerator()
		{
			return .((T[CSize])this);
		}

		public struct Enumerator : IEnumerator<T>
		{
		    private T[CSize] mList;
		    private int mIndex;
		    private T* mCurrent;

		    public this(T[CSize] list)
		    {
		        mList = list;
		        mIndex = 0;
		        mCurrent = null;
		    }

		    public bool MoveNext() mut
		    {
		        if ((uint(mIndex) < uint(CSize)))
		        {
		            mCurrent = &mList[mIndex];
		            mIndex++;
		            return true;
		        }			   
		        return MoveNextRare();
		    }

		    private bool MoveNextRare() mut
		    {
		    	mIndex = CSize + 1;
		        mCurrent = null;
		        return false;
		    }

		    public T Current
		    {
		        get
		        {
		            return *mCurrent;
		        }
		    }

			public int Index
			{
				get
				{
					return mIndex - 1;
				}
			}

			public Result<T> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public static T[CSize] InitAll
		{
			[Error("Element type has no default constructor")]
			get
			{
				return default;
			}
		}
	}

	extension SizedArray<T, CSize> where T : struct, new
	{
		public static T[CSize] InitAll
		{
			get
			{
				T[CSize] val;
				for (int i < CSize)
					val[i] = T();
				return val;
			}
		}
	}
}
