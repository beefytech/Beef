using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.gfx;
using System;
using System.Collections.Generic;
using IDE.Debugger;

namespace IDE.ui
{
	class GoToAddressDialog : IDEDialog
	{
		MemoryPanel mMemoryPanel;
		ExpressionEditWidget mAddressEdit;
		String mExpr ~ delete _;
		public bool mIsPending;

		public this()
		{
		    Title = "Go To Address";
		}

		public override void CalcSize()
		{
		    mWidth = GS!(320);
		    mHeight = GS!(100);
		}

		void HandleResult(String val)
		{
			//String evalStr = mExpr;

			if (val.StartsWith("!", StringComparison.Ordinal))
			{
				String errorString = scope String();
				DebugManager.GetFailString(val, mExpr, errorString);
			    IDEApp.sApp.Fail(errorString);
				return;
			}

			var vals = scope List<StringView>(val.Split('\n'));
			let addr = (int)int64.Parse(scope String(vals[0]), System.Globalization.NumberStyles.HexNumber).GetValueOrDefault();
			let byteCount = int32.Parse(scope String(vals[1])).GetValueOrDefault();
			if (addr != 0)
				mMemoryPanel.mBinaryDataWidget.SelectRange(addr, byteCount);
			Close();
		}

		void GetAddress()
		{
			var sourceEditWidgetContent = (SourceEditWidgetContent)mAddressEdit.Content;
			if (sourceEditWidgetContent.mAutoComplete != null)            
			    sourceEditWidgetContent.KeyChar('\t');

			delete mExpr;
			mExpr = new String();
			mAddressEdit.GetText(mExpr);
			mExpr.Trim();
			
			String val = scope String();
			let result = gApp.DebugEvaluate(null, mExpr, val, -1, .NotSet, .MemoryAddress | .AllowSideEffects | .AllowCalls, new => HandleResult);
			if (result == .Pending)
			{
				mIsPending = true;
				mDefaultButton.mDisabled = true;
			}
			else
			{
				HandleResult(val);
			}	
		}

		public void Init(MemoryPanel memoryPanel)
		{
			mMemoryPanel = memoryPanel;

		    mDefaultButton = AddButton("Go", new (evt) => { GetAddress(); evt.mCloseDialog = false; } );
		    mEscButton = AddButton("Cancel", new (evt) => Close());
		    //mNameEdit = AddEdit("NewProject");
		    //mDirectoryEdit = AddEdit(IDEApp.sApp.mWorkspace.mDir);

		    mAddressEdit = new ExpressionEditWidget();
		    mAddressEdit.mIsAddress = true;
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
		    //mAddressEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));
			mAddressEdit.ResizeAround(GS!(16), curY - GS!(36), mWidth - GS!(16)*2);

		    //curY -= 50;
		    //mAddressEdit.Resize(16, curY - 36, mWidth - 16 * 2, 20);
		}

		public override void Draw(Graphics g)
		{
		    base.Draw(g);

		    g.DrawString("Memory Address", mAddressEdit.mX, mAddressEdit.mY - GS!(20));
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
