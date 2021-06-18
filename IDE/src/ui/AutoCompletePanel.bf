using Beefy.utils;
using Beefy.theme.dark;
using Beefy.widgets;
using System;

namespace IDE.ui
{
	class AutoCompletePanel : Panel
	{
		public class DataArea : DockedWidget
		{
			public AutoCompletePanel mPanel;

			public void ResizeContent()
			{
				var autoComplete = mPanel.mAutoComplete;

				if (autoComplete == null)
					return;

				float yOfs = 0;
				if (autoComplete.mInvokeWidget != null)
				{
					/*float height = GS!(20);
					mAutoComplete.mInvokeWidget.Resize(0, 2, mWidth, height);
					yOfs += mAutoComplete.mInvokeWidget.mHeight;*/

					autoComplete.mInvokeWidget.ResizeContent(false);
					float height = autoComplete.mInvokeWidget.mHeight - GS!(12);
					autoComplete.mInvokeWidget.Resize(GS!(0), GS!(2), mWidth, height);
					yOfs += height;
				}

				if (autoComplete.mAutoCompleteListWidget != null)
				{
					autoComplete.mAutoCompleteListWidget.Resize(0, yOfs, mWidth, mHeight - yOfs);
					autoComplete.mAutoCompleteListWidget.mScrollContent.mWidth = mWidth;
				}
			}

			public override void Resize(float x, float y, float width, float height)
			{
				base.Resize(x, y, width, height);
				ResizeContent();
			}
		}

		public class CommentArea : DockedWidget
		{
			public AutoCompletePanel mPanel;

			public override void Draw(Beefy.gfx.Graphics g)
			{
				base.Draw(g);
				
				var autoComplete = mPanel.mAutoComplete;

				if (autoComplete == null)
					return;
				if (autoComplete.mAutoCompleteListWidget == null)
					return;
				if (autoComplete.mAutoCompleteListWidget.mSelectIdx == -1)
					return;
				if (autoComplete.mAutoCompleteListWidget.mDocumentationDelay > 0)
					return;
				var selectedEntry = autoComplete.mAutoCompleteListWidget.mEntryList[autoComplete.mAutoCompleteListWidget.mSelectIdx];
				if (selectedEntry.mDocumentation == null)
					return;

				DocumentationParser docParser = scope .(selectedEntry.mDocumentation);
				float drawX = GS!(6);
				float drawY = GS!(4);
				using (g.PushColor(0xFFC0C0C0))
					g.DrawString(docParser.ShowDocString, drawX, drawY, .Left, mWidth - drawX - GS!(8), .Wrap);
			}

			public override void DrawAll(Beefy.gfx.Graphics g)
			{
				base.DrawAll(g);
				using (g.PushColor(0x80FFFFFF))
					g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Bkg), 0, -GS!(6), mWidth, GS!(6));
			}
		}

		public AutoComplete mAutoComplete;
		public DarkDockingFrame mDockingFrame;
		public DataArea mDataArea;
		public CommentArea mCommentArea;

		public override String SerializationType
		{
			get { return "AutoCompletePanel"; }
		}

		public this()
		{
			mDataArea = new .();
			mDataArea.mPanel = this;
			mDataArea.mIsFillWidget = true;
			mCommentArea = new .();
			mCommentArea.mPanel = this;
			mCommentArea.mRequestedHeight = GS!(60);

			mDockingFrame = new DarkDockingFrame();
			mDockingFrame.mDrawBkg = false;
			mDockingFrame.AddDockedWidget(mDataArea, null, .Top);
			mDockingFrame.AddDockedWidget(mCommentArea, mDataArea, .Bottom);
			AddWidget(mDockingFrame);
		}

		public bool Unbind(AutoComplete autoComplete)
		{
			if (mAutoComplete != autoComplete)
				return false;

			if ((mAutoComplete.mInvokeWidget != null) && (mAutoComplete.mInvokeWidget.mParent == mDataArea))
				mDataArea.RemoveWidget(mAutoComplete.mInvokeWidget);
			if ((mAutoComplete.mAutoCompleteListWidget != null) && (mAutoComplete.mAutoCompleteListWidget.mParent == mDataArea))
				mDataArea.RemoveWidget(mAutoComplete.mAutoCompleteListWidget);
			mAutoComplete = null;
			return true;
		}

		public bool StartBind(AutoComplete autoComplete)
		{
			bool showInPanel = gApp.mSettings.mEditorSettings.mAutoCompleteShowKind == .Panel;
			if (gApp.mSettings.mEditorSettings.mAutoCompleteShowKind == .PanelIfVisible)
				showInPanel = mWidgetWindow != null;

			if (!showInPanel)
			{
				if (mAutoComplete != null)
					Unbind(mAutoComplete);
				return false;
			}

			if (mAutoComplete == autoComplete)
				return true;

			if (mAutoComplete != null)
				Unbind(mAutoComplete);

			if (mWidgetWindow == null)
				return false;

			mAutoComplete = autoComplete;
			
			return true;
		}

		public void FinishBind()
		{
			if (mAutoComplete == null)
				return;

			if ((mAutoComplete.mInvokeWidget != null) && (mAutoComplete.mInvokeWidget.mParent == null))
				mDataArea.AddWidget(mAutoComplete.mInvokeWidget);
			if ((mAutoComplete.mAutoCompleteListWidget != null) && (mAutoComplete.mAutoCompleteListWidget.mParent == null))
				mDataArea.AddWidget(mAutoComplete.mAutoCompleteListWidget);
			ResizeComponents();

			if (mAutoComplete.mAutoCompleteListWidget != null)
				mAutoComplete.mAutoCompleteListWidget.CenterSelection();
		}

		public void ResizeComponents()
		{
			mDockingFrame.Resize(0, 0, mWidth, mHeight);

			
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void Serialize(StructuredData data)
		{            
		    data.Add("Type", "AutoCompletePanel");
		}
	}
}
