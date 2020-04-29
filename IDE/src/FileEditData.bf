using IDE.ui;
using System;
using System.IO;
using Beefy.utils;
using System.Collections;
using System.Security.Cryptography;
using IDE.util;

namespace IDE
{
	[AllowDuplicates]
	public enum LineEndingKind
	{
		Unknown,
		Lf,   // \n
		CrLf, // \r\n
		Cr,   // \r

#if BF_PLATFORM_WINDOWS
		Default = CrLf,
#else
		Default = Lf,
#endif
	}

	public class FileEditData
	{
		public int mRefCount = 1;

		public String mSavedContent ~ delete _;
		public IdSpan mSavedCharIdData = IdSpan() ~ _.Dispose();
		public List<ProjectSource> mProjectSources = new List<ProjectSource>() ~ delete _;
		public Event<Action> mEditWidgetCreatedEvent ~ _.Dispose();
		public LineEndingKind mLineEndingKind;

	    public SourceEditWidget mEditWidget;
	    public int32 mLastFileTextVersion;
		public bool mOwnsEditWidget;
		public String mFilePath ~ delete _;
		public String mQueuedContent ~ delete _;
		public bool mHadRefusedFileChange;
		public bool mFileDeleted;

		public MD5Hash mMD5Hash;
		public SHA256Hash mSHA256Hash;

		public this()
		{
		}

		public void SetSavedData(String savedContent, IdSpan savedIdSpan)
		{
			if (savedContent != null)
			{
				if (mSavedContent == null)
					mSavedContent = new String();
	            mSavedContent.Set(savedContent);
			}
			else
			{
				delete mSavedContent;
				mSavedContent = null;
			}
			mSavedCharIdData.Dispose();
	        mSavedCharIdData = savedIdSpan.Duplicate();
		}

	    public bool HasTextChanged()
	    {
			if (mEditWidget == null)
				return false;
	        return mLastFileTextVersion != mEditWidget.Content.mData.mCurTextVersionId;
	    }
	
		public bool Reload()
		{
			mHadRefusedFileChange = false;

			if (mEditWidget == null)
			{
				if (mQueuedContent != null)
				{
					var span = IdSpan.GetDefault((int32)mQueuedContent.Length);
					SetSavedData(mQueuedContent, span);
					span.Dispose();
					DeleteAndNullify!(mQueuedContent);
				}
			}
			else
			{
			    var editWidgetContent = (SourceEditWidgetContent)mEditWidget.mEditWidgetContent;
				mFileDeleted = !editWidgetContent.Reload(mFilePath, mQueuedContent);
				/*if (editWidgetContent.mSourceViewPanel.LoadedHash.GetKind() == mLoadedHash.GetKind())
					editWidgetContent.mSourceViewPanel.LoadedHash = mLoadedHash;*/
				mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
			}
			return true;
		}
	
		public bool IsFileDeleted()
		{				
			if (mFileDeleted) // Double check
				mFileDeleted = !File.Exists(mFilePath);
			return mFileDeleted;
		}

		public bool IsLocked()
		{
			if (mEditWidget != null)
			{
				if (let sourceEditWidgetContent = mEditWidget.mEditWidgetContent as SourceEditWidgetContent)
				{
					if (sourceEditWidgetContent.mSourceViewPanel != null)
					{
						if (sourceEditWidgetContent.mSourceViewPanel.IsReadOnly)
							return true;
					}
				}
			}

			for (var projectSource in mProjectSources)
			{
				if (projectSource.mProject.mLocked)
					return true;
			}
			return false;
		}
	
		public ~this()
		{
			if (mOwnsEditWidget)
				delete mEditWidget;
		}

		public void Ref()
		{
			mRefCount++;
		}

		public void Deref()
		{
			if (--mRefCount == 0)
				delete this;
		}
		
		public void BuildHash(StringView contents)
		{
			mMD5Hash = Security.Cryptography.MD5.Hash(.((uint8*)contents.Ptr, contents.Length));
			mSHA256Hash = Security.Cryptography.SHA256.Hash(.((uint8*)contents.Ptr, contents.Length));
		}

		public bool CheckHash(SourceHash sourceHash)
		{
			switch (sourceHash)
			{
			case .MD5(let hash): return hash == mMD5Hash;
			case .SHA256(let hash): return hash == mSHA256Hash;
			default: return false;
			}
		}
	}
}
