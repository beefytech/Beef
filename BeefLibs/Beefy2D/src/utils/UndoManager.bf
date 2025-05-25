using System;
using System.Collections;
using System.Text;
using System.Diagnostics;

namespace Beefy.utils
{
    public class UndoAction
    {        
        public virtual bool Merge(UndoAction nextAction)
        {
            return false;
        }

        public virtual bool Undo()
        {
            return true;
        }

        public virtual bool Redo()
        {
            return true;
        }

		public virtual int32 GetCost()
		{
			return 1;
		}
    }

    public interface IUndoBatchStart
    {
        IUndoBatchEnd BatchEnd { get; }
        String Name { get; }
    }

    public interface IUndoBatchEnd
    {
        IUndoBatchStart BatchStart { get; }
        String Name { get; }
    }

    public class UndoBatchStart : UndoAction, IUndoBatchStart
    {
        public String mName;
        public UndoBatchEnd mBatchEnd;

		public int32 mBatchInsertIdx;
		public int32 mBatchSize = 0; // Don't close out until we add the end

        public String Name
        {
            get
            {
                return mName;
            }
        }

        public this(String name)
        {
            mName = name;
            mBatchEnd = new UndoBatchEnd();
            mBatchEnd.mBatchStart = this;
        }

        public IUndoBatchEnd BatchEnd 
        {
            get
            {
                return mBatchEnd;
            }
        }

		public override int32 GetCost()
		{
			return Math.Min(mBatchSize, 256); // Don't allow a large batch (ie: rename) to cause us to pull too much out of the undo buffer
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"UndoBatchStart {mName}");
		}
    }

    public class UndoBatchEnd : UndoAction, IUndoBatchEnd
    {
        public IUndoBatchStart mBatchStart;
        public String Name
        {
            get
            {
                return mBatchStart.Name;
            }
        }

        public IUndoBatchStart BatchStart
        {
            get
            {
                return mBatchStart;
            }
        }

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"UndoBatchEnd {Name}");
		}
    }

    public class UndoManager
    {
        List<UndoAction> mUndoList = new List<UndoAction>() ~ DeleteContainerAndItems!(_);
        int32 mUndoIdx = 0;
        int32 mMaxCost = 8192;
		int32 mCurCost = 0;
        bool mSkipNextMerge; // Don't merge after we do an undo or redo step
		int32 mFreezeDeletes;

        public ~this()
        {
            Clear();
        }

        public void WithActions(delegate void(UndoAction) func)
        {
            for (var action in mUndoList)
                func(action);
        }

        public void Clear()
        {
            while (mUndoList.Count > 0)
            {
				var undoAction = mUndoList.PopBack();
				delete undoAction;
            }
            mUndoIdx = 0;
			mCurCost = 0;
        }

		bool TryMerge(UndoAction action)
		{
			var currentBatchEnd = action as UndoBatchEnd;
			if (currentBatchEnd == null)
				return false;

			var currentBatchStart = currentBatchEnd.mBatchStart as UndoBatchStart;
			var prevBatchEndIdx = mUndoList.IndexOf(currentBatchStart) - 1;
			if (prevBatchEndIdx <= 0)
				return false;

			var prevBatchEnd = mUndoList[prevBatchEndIdx] as UndoBatchEnd;
			if (prevBatchEnd == null)
				return false;

			var prevBatchStart = prevBatchEnd.mBatchStart as UndoBatchStart;
			if (prevBatchStart == null)
				return false;
			if (prevBatchStart.Merge(currentBatchStart) == false)
				return false;

			mUndoList.Remove(currentBatchStart);
			mUndoList.Remove(currentBatchEnd);

			mUndoList.Remove(prevBatchEnd);
			mUndoList.Add(prevBatchEnd);

			delete currentBatchStart;
			delete currentBatchEnd;

			mUndoIdx = (.)mUndoList.Count;

			Debug.WriteLine("SUCCESS: Merged");
			return true;
		}

        public void Add(UndoAction action, bool allowMerge = true)
        {
			if ((allowMerge) && (TryMerge(action)))
				return;

			if (mFreezeDeletes == 0)
				mCurCost += action.GetCost();
			if (action is IUndoBatchStart)
			{
				mFreezeDeletes++;
				if (var undoBatchStart = action as UndoBatchStart)
				{
					undoBatchStart.mBatchInsertIdx = GetActionCount();
				}
			}
			else if (action is IUndoBatchEnd)
			{
				mFreezeDeletes--;
				if (var undoBatchEnd = action as UndoBatchEnd)
				{
					if (var undoBatchStart = undoBatchEnd.mBatchStart as UndoBatchStart)
					{
						undoBatchStart.mBatchSize = GetActionCount() - undoBatchStart.mBatchInsertIdx;
						int32 cost = undoBatchStart.GetCost();
						Debug.Assert(cost >= 0);
						mCurCost += cost;
					}
				}
			}

            if (mUndoIdx < mUndoList.Count)
            {
				int32 batchDepth = 0;
				for (int checkIdx = mUndoIdx; checkIdx < mUndoList.Count; checkIdx++)
				{
					var checkAction = mUndoList[checkIdx];
					if (batchDepth == 0)
						mCurCost -= checkAction.GetCost();

					if (checkAction is IUndoBatchStart)
						batchDepth++;
					else if (checkAction is IUndoBatchEnd)
						batchDepth--;

					delete checkAction;
				}
				Debug.Assert(batchDepth == 0);

				mUndoList.RemoveRange(mUndoIdx, mUndoList.Count - mUndoIdx);
            }

            if ((allowMerge) && (!mSkipNextMerge))
            {
                UndoAction prevAction = GetLastUndoAction();
				if (prevAction is UndoBatchStart)
				{
					var undoBatchStart = (UndoBatchStart)prevAction;
					if (undoBatchStart.mBatchEnd == action)
					{
						// It's an empty batch!
						mUndoList.PopBack();
						delete prevAction;
						delete action;
						mUndoIdx--;
						return;
					}
				}

                if ((prevAction != null) && (prevAction.Merge(action)))
				{
					delete action;
                    return;
				}
            }

            mSkipNextMerge = false;
            mUndoList.Add(action);
            mUndoIdx++;

			if ((mCurCost > mMaxCost) && (mFreezeDeletes == 0))
			{
				int32 wantCost = (int32)(mMaxCost * 0.8f); // Don't just remove one item at a time

				int32 checkIdx = 0;
				int32 batchDepth = 0;
				while (((mCurCost > wantCost) || (batchDepth > 0)) &&
					(checkIdx < mUndoList.Count))
				{
					var checkAction = mUndoList[checkIdx];
					if (batchDepth == 0)
						mCurCost -= checkAction.GetCost();

					if (checkAction is IUndoBatchStart)
						batchDepth++;
					else if (checkAction is IUndoBatchEnd)
						batchDepth--;

					delete checkAction;
					checkIdx++;
				}

				mUndoList.RemoveRange(0, checkIdx);
				mUndoIdx -= checkIdx;
			}
        }

        public UndoAction GetLastUndoAction()
        {
            if (mUndoIdx == 0)
                return null;
            return mUndoList[mUndoIdx - 1];
        }

        public bool Undo()
        {
            mSkipNextMerge = true;

            if (mUndoIdx == 0)
                return false;

            var undoAction = mUndoList[mUndoIdx - 1];
            if (IUndoBatchEnd undoBatchEnd = undoAction as IUndoBatchEnd)
            {
                while (true)
                {
                    if (!undoAction.Undo())
                        return false;
                    if (mUndoIdx == 0)
                        return false;
                    mUndoIdx--;
                    if ((undoAction == undoBatchEnd.BatchStart) || (mUndoIdx == 0))
                        return true;
                    undoAction = mUndoList[mUndoIdx - 1];
                }
            }

            if (!undoAction.Undo())
                return false;

            mUndoIdx--;
            return true;
        }

		public void PreUndo(delegate void (UndoAction) dlg)
		{
			int undoIdx = mUndoIdx;
			if (undoIdx == 0)
			    return;

			var undoAction = mUndoList[undoIdx - 1];
			if (IUndoBatchEnd undoBatchEnd = undoAction as IUndoBatchEnd)
			{
			    while (true)
			    {
					dlg(undoAction);
			        if (undoIdx == 0)
			            return;
			        undoIdx--;
			        if ((undoAction == undoBatchEnd.BatchStart) || (undoIdx == 0))
			            return;
			        undoAction = mUndoList[undoIdx - 1];
			    }
			}

			dlg(undoAction);
			undoIdx--;
		}

        public bool Redo()
        {
            mSkipNextMerge = true;
            
            if (mUndoIdx >= mUndoList.Count)
                return false;

            int32 startUndoIdx = mUndoIdx;
            var undoAction = mUndoList[mUndoIdx];
            if (undoAction is UndoBatchStart)
            {
                UndoBatchStart undoBatchStart = (UndoBatchStart)undoAction;
                while (true)
                {
                    if (!undoAction.Redo())
                    {
                        mUndoIdx = startUndoIdx;
                        return false;
                    }
                    if (mUndoIdx >= mUndoList.Count)
                        return false;
                    mUndoIdx++;
                    if (undoAction == undoBatchStart.mBatchEnd)
                        return true;
                    undoAction = mUndoList[mUndoIdx];
                }
            }

            if (!undoAction.Redo())
                return false;

            mUndoIdx++;
            return true;
        }

		public int32 GetActionCount()
		{
			return mUndoIdx;
		}

		public override void ToString(String str)
		{
			for (int i < mUndoList.Count)
			{
				if (i == mUndoIdx)
					str.Append(">");
				else
					str.Append(" ");

				var entry = mUndoList[i];
				str.AppendF($"{i}. {entry}");

				str.Append("\n");
			}
		}
    }
}
