using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using IDE.ui;
using Beefy;
using System.IO;

namespace IDE
{    
    public class HistoryEntry : TrackedTextElement
    {
        //public String mNotes ~ delete _;

		public bool mNoMerge;

        public this()
        {
            mSnapToLineStart = false;
        }

		public ~this()
		{

		}
    }

    public class HistoryManager
    {
        const int32 LINE_MERGE_MAX_DELTA = 8;
        const int32 MAX_ENTRIES = 24;

        public List<HistoryEntry> mHistoryList = new List<HistoryEntry>() ~
			{
				for (var entry in _)
					entry.Deref();
				delete _;
			};
        public int32 mHistoryIdx = -1;

        public HistoryEntry CreateHistory(SourceViewPanel sourceViewPanel, String fileName, int wantLineNum, int wantColumn, bool ignoreIfClose)
        {
            // If we are backwards in history, try to update the current entry if we're close to it.
            //  Otherwise we truncate the history from the current location and append a new entry
            HistoryEntry historyEntry;
            if (mHistoryIdx >= 0)
            {
                historyEntry = mHistoryList[mHistoryIdx];
                if ((!historyEntry.mNoMerge) && (historyEntry.mFileName == fileName) && (Math.Abs(historyEntry.mLineNum - wantLineNum) <= LINE_MERGE_MAX_DELTA))
                {
                    if (ignoreIfClose)
                        return null;

                    historyEntry.mMoveIdx++;
                    historyEntry.mLineNum = (int32)wantLineNum;
                    historyEntry.mColumn = (int32)wantColumn;

                    bool foundExistingTrackedElement = false;
                    if ((sourceViewPanel != null) && (sourceViewPanel.mTrackedTextElementViewList != null))
                    {
                        for (var trackedElement in sourceViewPanel.mTrackedTextElementViewList)
                        {
                            if (trackedElement.mTrackedElement == historyEntry)
                            {
                                sourceViewPanel.UpdateTrackedElementView(trackedElement);
                                foundExistingTrackedElement = true;
                                break;
                            }
                        }
                    }

                    if (!foundExistingTrackedElement)
                        IDEApp.sApp.mDebugger.mBreakpointsChangedDelegate();
                    return historyEntry;
                }
            }

            int32 removeIdx = mHistoryIdx + 1;
            while (removeIdx < mHistoryList.Count)
                DeleteHistory(mHistoryList[removeIdx]);

            while (mHistoryList.Count > MAX_ENTRIES)
            {
                var entry = mHistoryList.PopFront();
				entry.Kill();
            }

            mHistoryIdx = (int32)mHistoryList.Count;

            historyEntry = new HistoryEntry();
            historyEntry.mFileName = new String(fileName);
            historyEntry.mLineNum = (int32)wantLineNum;
            historyEntry.mColumn = (int32)wantColumn;
            mHistoryList.Add(historyEntry);

            IDEApp.sApp.mDebugger.mBreakpointsChangedDelegate();
            return historyEntry;
        }

        public void DeleteHistory(HistoryEntry history)
        {
            mHistoryList.Remove(history);
            if (mHistoryIdx >= mHistoryList.Count)
                mHistoryIdx = (int32)mHistoryList.Count - 1;
            IDEApp.sApp.mDebugger.mBreakpointsChangedDelegate();
			history.Kill();
        }

        public bool IsAtCurrentHistory(SourceViewPanel sourceViewPanel, String fileName, int wantLineNum, int wantColumn, bool ignoreIfClose)
        {
            if (mHistoryIdx >= 0)
            {
                HistoryEntry historyEntry = mHistoryList[mHistoryIdx];
                if ((Path.Equals(historyEntry.mFileName, fileName)) && (Math.Abs(historyEntry.mLineNum - wantLineNum) == 0))
                {
                    return true;
                }
            }
            return false;
        }

        public void GoToCurrentHistory()
        {
            if (mHistoryIdx == -1)
                return;

            var History = mHistoryList[mHistoryIdx];
            IDEApp.sApp.ShowSourceFileLocation(History.mFileName, -1, -1, History.mLineNum, History.mColumn, LocatorType.Smart);
        }

        public void PrevHistory()
        {
            if (mHistoryList.Count == 0)
                return;

            mHistoryIdx--;
            if (mHistoryIdx < 0)
                mHistoryIdx = 0;

            GoToCurrentHistory();
        }

        public void NextHistory()
        {
            if (mHistoryList.Count == 0)
                return;

            mHistoryIdx++;
            if (mHistoryIdx >= mHistoryList.Count)            
                mHistoryIdx = (int32)mHistoryList.Count - 1;

            GoToCurrentHistory();
        }
    }
}
