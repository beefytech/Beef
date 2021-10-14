using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using IDE.Debugger;
using Beefy.theme;

namespace IDE.ui
{   
    public class NewBreakpointDialog : IDEDialog
    {

		public enum BreakpointKind
		{
			Memory,
			Symbol
		}

        EditWidget mAddressEdit;
		String mExpr ~ delete _;
		public bool mIsPending;
		BreakpointKind mBreakpointKind;

        public this(BreakpointKind breakpointKind)
        {
			mBreakpointKind = breakpointKind;
			if (mBreakpointKind == .Memory)
            	Title = "New Memory Breakpoint";
			else
				Title = "New Symbol Breakpoint";
        }

        public override void CalcSize()
        {
            mWidth = GS!(320);
            mHeight = GS!(100);
        }

		void DoCreateMemoryBreakpoint(int addr, int byteCount, String addrType, bool showOptions)
		{
			if (addr == 0)
				return;
			var breakpoint = IDEApp.sApp.mDebugger.CreateMemoryBreakpoint(mExpr, addr, byteCount, addrType);
			if (!breakpoint.IsBound())
			    IDEApp.sApp.mBreakpointPanel.ShowMemoryBreakpointError();
			Close();
			gApp.RefreshWatches();
			if (showOptions)
			{
				var widgetWindow = mWidgetWindow.mParent as WidgetWindow;
				ConditionDialog dialog = new ConditionDialog();
				dialog.Init(breakpoint);
				dialog.PopupWindow(widgetWindow);
			}
		}

        public bool CreateBreakpoint(bool showOptions)
        {
            if (var sourceEditWidgetContent = mAddressEdit.Content as SourceEditWidgetContent)
			{
	            if (sourceEditWidgetContent.mAutoComplete != null)            
	                sourceEditWidgetContent.KeyChar('\t');
			}

			delete mExpr;
            mExpr = new String();
            mAddressEdit.GetText(mExpr);
            mExpr.Trim();

			if (mBreakpointKind == .Memory)
			{
	            int addr = 0;
	            int32 byteCount = 0;
				String addrType = scope String();
	            switch (gApp.mBreakpointPanel.TryCreateMemoryBreakpoint(mExpr, out addr, out byteCount, addrType,
					new (addr, byteCount, addrType) => { DoCreateMemoryBreakpoint(addr, byteCount, addrType, showOptions); }))
				{
				case .Pending:
					SetPending(true);
					return false;
				case .Failure:
					return false;
				case .Success:
				}
				DoCreateMemoryBreakpoint(addr, byteCount, addrType, showOptions);
			}
			else
			{
				gApp.mDebugger.CreateSymbolBreakpoint(mExpr);
				Close();
			}
			return true;
        }

        public void Init()
        {
            mDefaultButton = AddButton("Create", new (evt) => { CreateBreakpoint(false); evt.mCloseDialog = false; });
			AddButton("Create...", new (evt) => { CreateBreakpoint(true); evt.mCloseDialog = false; });
            mEscButton = AddButton("Cancel", new (evt) => Close());
            //mNameEdit = AddEdit("NewProject");
            //mDirectoryEdit = AddEdit(IDEApp.sApp.mWorkspace.mDir);

			let addressEdit = new ExpressionEditWidget();
			if (mBreakpointKind == .Memory)
				addressEdit.mIsAddress = true;
			else
				addressEdit.mIsSymbol = true;
            mAddressEdit = addressEdit;

			mAddressEdit = addressEdit;
            AddWidget(mAddressEdit);            
            mTabWidgets.Add(mAddressEdit);

            //mAddressEdit.

            mAddressEdit.mOnSubmit.Add(new => EditSubmitHandler);
            mAddressEdit.mOnCancel.Add(new => EditCancelHandler);
        }

        public override void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
        {
            base.PopupWindow(parentWindow, offsetX, offsetY);            
            mAddressEdit.SetFocus();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float curY = mHeight - GS!(20) - mButtonBottomMargin;
            mAddressEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));

            //curY -= 50;
            //mAddressEdit.Resize(16, curY - 36, mWidth - 16 * 2, 20);
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

					using (g.PushColor(ThemeColors.Theme.Text.Color))
            g.DrawString((mBreakpointKind == .Memory) ? "Breakpoint Address" : "Symbol Name", mAddressEdit.mX, mAddressEdit.mY - GS!(20));
            //g.DrawString("Project Directory", mDialogEditWidget.mX, mDialogEditWidget.mY - 20);
        }

		void SetPending(bool isPending)
		{
			mIsPending = isPending;
			if (isPending)
			{
				SetFocus();
				mAddressEdit.Content.mIsReadOnly = true;
				mDefaultButton.mDisabled = true;
			}
			else
			{
				mAddressEdit.Content.mIsReadOnly = false;
				mDefaultButton.mDisabled = false;
			}
		}

		public override void Close()
		{
			base.Close();

			if (mIsPending)
				gApp.mBreakpointPanel.CancelPending();
		}

		public override void Update()
		{
			base.Update();

			if ((mIsPending) && (gApp.mPendingDebugExprHandler == null))
			{
				SetPending(false);
			}
		}
    }
}
