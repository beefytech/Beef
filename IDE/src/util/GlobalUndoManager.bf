using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using Beefy.utils;

namespace IDE.Util
{
    public class GlobalUndoAction : UndoAction
    {        
        public GlobalUndoData mUndoData;
        public bool mPerforming;
        public FileEditData mFileEditData;

        public this(FileEditData fileEditData, GlobalUndoData undoData)
        {
            mFileEditData = fileEditData;
            mUndoData = undoData;
        }

		public ~this()
		{
			Clear();
		}

        public void Detach()
        {
            if (mUndoData == null)
                return;
            if (mUndoData.mFileEditDatas.Count == 0)
            {
                Clear();
            }
            else
            {
                mUndoData.mFileEditDatas.Remove(mFileEditData);
            }
            mUndoData = null;
            mFileEditData = null;
        }

        public void Clear()
        {            
            mUndoData.mFileEditDatas.Remove(mFileEditData);
            if (mUndoData.mFileEditDatas.Count == 0)
            {
				mUndoData.mDeleted = true;
				IDEApp.sApp.mGlobalUndoManager.mDeletingGlobalUndoList.Add(mUndoData);
			}
        }

        public override bool Undo()
        {
			Debug.Assert(!mUndoData.mDeleted);
			if (mUndoData.mDeleted)
			{
				return true;
			}

            mUndoData.mUndoCount++;
            if (mUndoData.mPerforming)
                return true;

            mUndoData.mPerforming = true;
            for (var editData in mUndoData.mFileEditDatas)
            {
                if (editData == mFileEditData)
                    continue;

                var editWidgetContent = editData.mEditWidget.Content;
                while (true)
                {
                    int32 prevUndoCount = mUndoData.mUndoCount;
                    bool success = editWidgetContent.mData.mUndoManager.Undo();
                    Debug.Assert(success);
                    if (mUndoData.mUndoCount != prevUndoCount)
                        break;
                }

				if (!editData.HasEditPanel())
				{
					gApp.SaveFile(editData);
				}

				if (IDEApp.IsBeefFile(editData.mFilePath))
				{
					for (var projectSource in editData.mProjectSources)
					{
						projectSource.HasChangedSinceLastCompile = true;
						IDEApp.sApp.mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
					}
				}
            }
            mUndoData.mPerforming = false;
			IDEApp.sApp.mBfResolveCompiler.QueueDeferredResolveAll();

            return base.Undo();
        }

        public override bool Redo()
        {
            if (mUndoData.mDeleted)
            {
                // Someone has already overwritten this location in the redo buffer
                //  so this is no longer valid
                return false;
            }

            mUndoData.mRedoCount++;
            if (mUndoData.mPerforming)
                return true;            

            mUndoData.mPerforming = true;
            for (var editData in mUndoData.mFileEditDatas)
            {
                if (editData == mFileEditData)
                    continue;

                var editWidgetContent = editData.mEditWidget.Content;
                while (true)
                {
                    int32 prevRedoCount = mUndoData.mRedoCount;
                    bool success = editWidgetContent.mData.mUndoManager.Redo();
                    Debug.Assert(success);
                    if (mUndoData.mRedoCount != prevRedoCount)
                        break;
                }

				if (!editData.HasEditPanel())
				{
					gApp.SaveFile(editData);
				}

				if (IDEApp.IsBeefFile(editData.mFilePath))
				{
					for (var projectSource in editData.mProjectSources)
					{
						projectSource.HasChangedSinceLastCompile = true;
						IDEApp.sApp.mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
					}
				}
            }
            mUndoData.mPerforming = false;
			IDEApp.sApp.mBfResolveCompiler.QueueDeferredResolveAll();

            return base.Redo();
        }
    }

    public class GlobalUndoData
    {
        public bool mPerforming;
        public bool mDeleted;
        public int32 mUndoCount;
        public int32 mRedoCount;
        public HashSet<FileEditData> mFileEditDatas = new HashSet<FileEditData>() ~ delete _;

		public ~this()
		{			
		}
    }

    public class GlobalUndoManager
    {
        public List<GlobalUndoData> mGlobalUndoDataList = new List<GlobalUndoData>() ~ delete _;
        public List<GlobalUndoData> mDeletingGlobalUndoList = new List<GlobalUndoData>() ~ DeleteContainerAndItems!(_);

		public ~this()
		{			
		}

        public GlobalUndoData CreateUndoData()
        {
            var globalUndoData = new GlobalUndoData();
            mGlobalUndoDataList.Add(globalUndoData);            
            return globalUndoData;
        }

        public void Update()
        {
            /*foreach (var globalUndoData in mDeletingGlobalUndoList)
            {
                mGlobalUndoDataList.Remove(globalUndoData);
            }
            mDeletingGlobalUndoList.Clear();*/
			ClearAndDeleteItems(mDeletingGlobalUndoList);
        }
		
		public void Clear()
		{
			mGlobalUndoDataList.Clear();
			ClearAndDeleteItems(mDeletingGlobalUndoList);
		}
    }
}
