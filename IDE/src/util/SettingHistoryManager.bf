using System;
using System.Collections;

namespace IDE.util
{
	class SettingHistoryManager
	{
		public List<PropertyBag> mHistory = new .() ~ DeleteContainerAndItems!(_);
		public int mHistoryIdx;
		public PropertyBag mEmptyEnd ~ delete _;

		public this(bool endOnEmpty)
		{
			if (endOnEmpty)
				mEmptyEnd = new PropertyBag();
		}

		public bool HasPrev()
		{
			if ((mHistoryIdx == 0) || (mHistory == null))
				return false;
			return true;
		}

		public PropertyBag GetPrev()
		{
			if ((mHistoryIdx == 0) || (mHistory == null))
				return null;
			return mHistory[--mHistoryIdx];
		}

		public bool HasNext()
		{
			if (mHistoryIdx >= mHistory.Count - 1)
			{
				if (mEmptyEnd != null)
					return true;
				return false;
			}
			return true;
		}

		public PropertyBag GetNext()
		{
			if ((mHistoryIdx >= mHistory.Count - 1) && (mEmptyEnd != null))
			{
				if (mHistoryIdx == mHistory.Count - 1)
					mHistoryIdx++;
				return mEmptyEnd;
			}

			if (mHistoryIdx >= mHistory.Count - 1)
				return null;
			return mHistory[++mHistoryIdx];
		}

		public void Add(PropertyBag propertyBag)
		{
			while (mHistoryIdx < mHistory.Count - 1)
			{
				var prevEntry = mHistory.Back;
				mHistory.PopBack();
				delete prevEntry;
			}

			if (!mHistory.IsEmpty)
			{
				// Ignore duplicate
				if (propertyBag == mHistory.Back)
				{
					delete propertyBag;
					return;
				}
			}

			mHistory.Add(propertyBag);
			mHistoryIdx++;
		}

		public void ToEnd()
		{
			mHistoryIdx = mHistory.Count;
		}
	}
}
