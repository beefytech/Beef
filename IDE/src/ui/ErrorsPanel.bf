using Beefy.widgets;
using Beefy.theme.dark;
using IDE.Compiler;
using System.Collections;
using System.Threading;
using System;
using Beefy.gfx;
using System.IO;
using Beefy.utils;

namespace IDE.ui
{
	class ErrorsPanel : Panel
	{
		public class ErrorsListView : IDEListView
		{
			protected override ListViewItem CreateListViewItem()
			{
				return new ErrorsListViewItem();
			}
		}

		public class ErrorsListViewItem : IDEListViewItem
		{
			public String mFilePath ~ delete _;
			public int mLine;
			public int mColumn;

			public override void DrawSelect(Graphics g)
			{
				bool hasFocus = mListView.mHasFocus;
				if ((mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(mListView)))
					hasFocus = true;
			    using (g.PushColor(hasFocus ? 0xFFFFFFFF : 0x80FFFFFF))
			        base.DrawSelect(g);
			}
			
			public bool Goto()
			{
				if (mFilePath == null)
					return false;
				gApp.ShowSourceFileLocation(mFilePath, -1, -1, mLine, mColumn, .Always);
				return false;
			}
		}

		public ErrorsListView mErrorLV;

		public bool mNeedsResolveAll;
		public int mDataId;
		public int mErrorListId;
		public int mErrorRefreshId;
		public Monitor mMonitor = new .() ~ delete _;
		public Dictionary<String, List<BfPassInstance.BfError>> mParseErrors = new .() ~ delete _;
		public List<BfPassInstance.BfError> mResolveErrors = new .() ~ DeleteContainerAndItems!(_);
		public int mDirtyTicks;

		public int mErrorCount;
		public int mWarningCount;

		public this()
		{
			mErrorLV = new .();
			//mErrorLV.mPanel = this;
			//mErrorLV.SetShowHeader(false);
			mErrorLV.InitScrollbars(true, true);
			mErrorLV.mLabelX = GS!(6);
			//mErrorLV.mOnItemMouseDown.Add(new => ItemMouseDown);
			mErrorLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mErrorLV.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);
			mErrorLV.AddColumn(100, "Code");
			mErrorLV.AddColumn(400, "Description");
			mErrorLV.AddColumn(100, "Project");
			mErrorLV.AddColumn(120, "File");
			mErrorLV.AddColumn(40, "Line");
			mErrorLV.mOnItemMouseDown.Add(new (item, x, y, btnNum, btnCount) =>
				{
					ListViewItemMouseDown(item, x, y, btnNum, btnCount);

					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (ErrorsListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}

					//mErrorLV.GetRoot().SelectItemExclusively()
				});
			//let newItem = mErrorLV.GetRoot().CreateChildItem();
			//newItem.Label = "Hey";

			AddWidget(mErrorLV);
		}

		public ~this()
		{
			ClearParserErrors(null);
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "ErrorsPanel");
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			mErrorLV.Resize(0, 0, width, height);
		}

		public enum ResolveKind
		{
			None,
			Parse,
			Resolve
		}

		public void ProcessPassInstance(BfPassInstance passInstance, BfPassInstance.PassKind passKind)
		{
			using (mMonitor.Enter())
			{
				int32 errorCount = passInstance.GetErrorCount();
				if (passKind != .Parse)
				{
					if (!mResolveErrors.IsEmpty)
						mDataId++;

					for (let error in mResolveErrors)
					{
						if (error.mIsWarning)
							mWarningCount--;
						else
							mErrorCount--;
					}

					ClearAndDeleteItems(mResolveErrors);
					mResolveErrors.Capacity = mResolveErrors.Count;
				}
				var bfl = scope:: List<BfPassInstance.BfError>();
				for (int32 errorIdx = 0; errorIdx < errorCount; errorIdx++)
				{
				    BfPassInstance.BfError bfError = new BfPassInstance.BfError();
				    passInstance.GetErrorData(errorIdx, bfError, true);
					if (bfError.mFilePath == null)
						bfError.mFilePath = new String(""); //for sort below

					bfl.Add(bfError);
					for (int32 moreInfoIdx < bfError.mMoreInfoCount)
					{
						BfPassInstance.BfError moreInfo = new BfPassInstance.BfError();
						passInstance.GetMoreInfoErrorData(errorIdx, moreInfoIdx, moreInfo, true);
						if (bfError.mMoreInfo == null)
							bfError.mMoreInfo = new List<BfPassInstance.BfError>();
						bfError.mMoreInfo.Add(moreInfo);
					}
				}

				function int(int lhs, int rhs) ascLambda = (lhs, rhs) => lhs <=> rhs;
				bfl.Sort(scope (lhs, rhs) => ascLambda(lhs.mFilePath.GetHashCode()+lhs.mSrcStart, rhs.mFilePath.GetHashCode()+rhs.mSrcStart));
				
				for (int32 errorIdx = 0; errorIdx < bfl.Count; errorIdx++)
				{
					var bfError = bfl[errorIdx];

					if (bfError.mIsWarning)
					{
						mWarningCount++;
					}
					else
						mErrorCount++;

					if (passKind == .Parse)
					{
						if (bfError.mFilePath == null)
							bfError.mFilePath = new String("");
						bool added = mParseErrors.TryAdd(bfError.mFilePath, var keyPtr, var valuePtr);
						if (added)
						{
							*keyPtr = new .(bfError.mFilePath);
							*valuePtr = new .();
						}
						(*valuePtr).Add(bfError);
					}
					else
						mResolveErrors.Add(bfError);

					mDataId++;
				}
			}
		}

		public void ClearParserErrors(String filePath)
		{
			using (mMonitor.Enter())
			{
				void DeleteErrorList(List<BfPassInstance.BfError> list)
				{
					for (let error in list)
					{
						if (error.mIsWarning)
							mWarningCount--;
						else
							mErrorCount--;
						delete error;
					}
					delete list;
				}

				if (filePath == null)
				{
					for (var kv in mParseErrors)
					{
						delete kv.key;
						DeleteErrorList(kv.value);
						mDataId++;
					}
					mParseErrors.Clear();
				}
				else
				{
					if (mParseErrors.GetAndRemove(filePath) case .Ok((let key, let list)))
					{
						delete key;
						DeleteErrorList(list);
						mDataId++;
					}
				}
			}
		}

		public void Clear()
		{
			using (mMonitor.Enter())
			{
				ClearParserErrors(null);
				ClearAndDeleteItems(mResolveErrors);
				mDataId++;
				mErrorCount = 0;
				mWarningCount = 0;
			}
		}

		void ProcessErrors()
		{
			using (mMonitor.Enter())
			{
				if (mDataId != mErrorListId)
				{
					let root = mErrorLV.GetRoot();

					int idx = 0;
					void HandleError(BfPassInstance.BfError error)
					{
						ErrorsListViewItem item;

						bool changed = false;
						void SetLabel(ListViewItem item, StringView str)
						{
							if (item.Label == str)
								return;
							changed = true;
							item.Label = str;
						}

						if (idx >= root.GetChildCount())
						{
							item = (.)root.CreateChildItem();
							item.CreateSubItem(1);
							item.CreateSubItem(2);
							item.CreateSubItem(3);
							item.CreateSubItem(4);
						}
						else
							item = (.)root.GetChildAtIndex(idx);

						if (error.mFilePath == null)
							DeleteAndNullify!(error.mFilePath);
						else
							String.NewOrSet!(item.mFilePath, error.mFilePath);
						item.mLine = error.mLine;
						item.mColumn = error.mColumn;

						String codeStr = scope String(32);
						codeStr.AppendF(error.mIsWarning ? "{}Warning" : "{}Error", Font.EncodeColor(error.mIsWarning ? 0xFFFFFF80 : 0xFFFF8080));
						if (error.mCode != 0)
							codeStr.AppendF(" {}", error.mCode);
						codeStr.AppendF("{}", Font.EncodePopColor());
						SetLabel(item, codeStr);

						let descItem = item.GetSubItem(1);
						String errStr = scope String(32);
						int maxLen = 4*1024;
						if (error.mError.Length > maxLen)
						{
							errStr.Append(error.mError.Substring(0, maxLen));
							errStr.Append("...");
						}
						else
							errStr.Append(error.mError);
						errStr.Replace('\n', ' ');

						SetLabel(descItem, errStr);

						let projectItem = item.GetSubItem(2);
						SetLabel(projectItem, error.mProject);

						let fileNameItem = item.GetSubItem(3);
						let fileName = scope String(128);
						Path.GetFileName(error.mFilePath, fileName);
						SetLabel(fileNameItem, fileName);
						let lineNumberItem = item.GetSubItem(4);
						if (error.mLine != -1)
							SetLabel(lineNumberItem, scope String(16)..AppendF("{}", error.mLine + 1));
						else
							SetLabel(lineNumberItem, "");

						if (changed)
							item.Focused = false;

						idx++;
					}

					if (!mParseErrors.IsEmpty)
					{
						List<String> paths = scope .();
						for (var path in mParseErrors.Keys)
							paths.Add(path);
						paths.Sort();

						for (var path in paths)
						{
							for (var error in mParseErrors[path])
								HandleError(error);
						}
					}

					for (let error in mResolveErrors)
						HandleError(error);

					while (root.GetChildCount() > idx)
						root.RemoveChildItemAt(root.GetChildCount() - 1);

					mErrorListId = mDataId;
					MarkDirty();
				}
			}
		}

		public void UpdateAlways()
		{
			if (mErrorRefreshId != mDataId)
			{
				gApp.MarkDirty();
				mErrorRefreshId = mDataId;
			}

			let compiler = gApp.mBfResolveCompiler;
			if ((mNeedsResolveAll) && (compiler != null) && (!compiler.IsPerformingBackgroundOperation()))
			{
				if (compiler.mResolveAllWait == 0)
					compiler.QueueDeferredResolveAll();
				mNeedsResolveAll = false;
			}
		}

		public override void Update()
		{
			base.Update();
			
			if (!mVisible)
			{
				// Very dirty
				mDirtyTicks = Math.Max(100, mDirtyTicks);
				return;
			}

			let compiler = gApp.mBfResolveCompiler;
			if ((compiler == null) ||
				((!compiler.IsPerformingBackgroundOperation()) && (compiler.mResolveAllWait == 0)))
				mDirtyTicks = 0;
			else
				mDirtyTicks++;

			if(mDirtyTicks==0)
				ProcessErrors();
		}
		
		public void SetNeedsResolveAll()
		{
			mNeedsResolveAll = true;
		}
		
		public void ShowErrorNext()
		{
			if(mDirtyTicks==0)
				ProcessErrors();

			bool foundFocused = false;
			let root = mErrorLV.GetRoot();
			if (root.GetChildCount() == 0)
				return;
			for (let lvItem in root.mChildItems)
			{
				if (lvItem.Focused)
				{
					lvItem.Focused = false;
					foundFocused = true;
				}
				else if (foundFocused)
				{
					lvItem.Focused = true;
					((ErrorsListViewItem)lvItem).Goto();
					return;
				}
			}

			let lvItem = (ErrorsListViewItem)root.GetChildAtIndex(0);
			lvItem.Focused = true;
			lvItem.Goto();
		}
	}
}
