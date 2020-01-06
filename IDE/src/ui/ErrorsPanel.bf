using Beefy.widgets;
using Beefy.theme.dark;
using IDE.Compiler;
using System.Collections.Generic;
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

		public DarkDockingFrame mDockingFrame;
		public ErrorsListView mErrorLV;
		public OutputWidget mOutputWidget;

		public bool mNeedsResolveAll;
		public bool mErrorsDirty;
		public Monitor mMonitor = new .() ~ delete _;
		public List<BfPassInstance.BfError> mErrorList = new .() ~ DeleteContainerAndItems!(_);
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
					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (ErrorsListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}

					ListViewItemMouseDown(item, x, y, btnNum, btnCount);
					//mErrorLV.GetRoot().SelectItemExclusively()
				});
			//let newItem = mErrorLV.GetRoot().CreateChildItem();
			//newItem.Label = "Hey";

			mOutputWidget = new .();
			
			var errorDock = new DockingProxy(mErrorLV);
			var detailsDock = new DockingProxy(mOutputWidget);

			mDockingFrame = new DarkDockingFrame();
			mDockingFrame.mDrawBkg = false;
			mDockingFrame.AddDockedWidget(errorDock, null, .Top);
			mDockingFrame.AddDockedWidget(detailsDock, errorDock, .Bottom);
			AddWidget(mDockingFrame);
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "ErrorsPanel");
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			mDockingFrame.Resize(0, 0, width, height);
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
				mErrorCount = 0;
				mWarningCount = 0;

				int32 errorCount = passInstance.GetErrorCount();
				ClearAndDeleteItems(mErrorList);
				mErrorList.Capacity = mErrorList.Count;

				for (int32 errorIdx = 0; errorIdx < errorCount; errorIdx++)
				{
				    BfPassInstance.BfError bfError = new BfPassInstance.BfError();
				    passInstance.GetErrorData(errorIdx, bfError, true);

					if (bfError.mIsWarning)
						mWarningCount++;
					else
						mErrorCount++;

					for (int32 moreInfoIdx < bfError.mMoreInfoCount)
					{
						BfPassInstance.BfError moreInfo = new BfPassInstance.BfError();
						passInstance.GetMoreInfoErrorData(errorIdx, moreInfoIdx, moreInfo, true);
						if (bfError.mMoreInfo == null)
							bfError.mMoreInfo = new List<BfPassInstance.BfError>();
						bfError.mMoreInfo.Add(moreInfo);
					}

				    mErrorList.Add(bfError);
				}

				mErrorsDirty = true;
			}
		}

		public void ClearParserErrors(StringView filePath)
		{

		}

		void ProcessErrors()
		{
			using (mMonitor.Enter())
			{
				if (mErrorsDirty)
				{
					let root = mErrorLV.GetRoot();

					for (let error in mErrorList)
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

						int idx = @error.Index;
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
						SetLabel(descItem, scope String(32)..AppendF(error.mError));

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
					}

					while (root.GetChildCount() > mErrorList.Count)
						root.RemoveChildItemAt(root.GetChildCount() - 1);

					mErrorsDirty = false;
				}
			}
		}

		public void CheckResolveAll()
		{
			let compiler = gApp.mBfResolveCompiler;
			if ((mNeedsResolveAll) && (!compiler.IsPerformingBackgroundOperation()))
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
			if ((!compiler.IsPerformingBackgroundOperation()) && (compiler.mResolveAllWait == 0))
				mDirtyTicks = 0;
			else
				mDirtyTicks++;

			ProcessErrors();
		}
		
		public void SetNeedsResolveAll()
		{
			mNeedsResolveAll = true;
		}
		
		public void ShowErrorNext()
		{
			ProcessErrors();

			bool foundFocused = false;
			let root = mErrorLV.GetRoot();
			if (root.mChildItems == null)
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
