using Beefy.theme.dark;
using System;
using System.IO;
using System.Threading.Tasks;
using System.Threading;
using System.Collections.Generic;
using Beefy.gfx;

namespace IDE.ui
{
	class TypeWildcardEditWidget : ExpressionEditWidget
	{
		uint32[] mColors = new .(0xFFFFFFFF, 0xFFFF8080) ~ delete _;
		int mLastEvalIdx = -1;

		public this()
		{
			var darkEditWidgetContent = (DarkEditWidgetContent)mEditWidgetContent;
			darkEditWidgetContent.mTextColors = mColors;
		}

		public override void UpdateText(char32 keyChar, bool doAutoComplete)
		{
			var editText = scope String();
			GetText(editText);

			int cursorPos = doAutoComplete ? mEditWidgetContent.CursorTextPos - 1 : -1;
#unwarn

			int editOffset = 0;
			if (cursorPos > 0)
			{
				int semiPos = StringView(editText, 0, cursorPos).LastIndexOf(';');
				if (semiPos != -1)
					editOffset = semiPos + 1;
			}

			editText.Remove(0, editOffset);
			cursorPos -= editOffset;
			//
			{
				int semiPos = editText.IndexOf(';');
				if (semiPos != -1)
					editText.RemoveToEnd(semiPos);
			}

			bool isValid;
			if (editText.StartsWith("@"))
				isValid = true;
			else
				isValid = gApp.mBfResolveCompiler.VerifyTypeName(editText, cursorPos);

			for (int ofs < editText.Length)
			{
				mEditWidgetContent.mData.mText[editOffset + ofs].mDisplayTypeId = isValid ? 0 : 1;
			}
			//mColors[0] = isValid ? 0xFFFFFFFF : 0xFFFF8080;

			if (doAutoComplete)
			{
				String autocompleteInfo = scope String();
				gApp.mBfResolveCompiler.GetAutocompleteInfo(autocompleteInfo);

				if (!autocompleteInfo.IsEmpty)
				{
					GetAutoComplete();
					SetAutoCompleteInfo(autocompleteInfo, editOffset);
				}
			}
		}

		public void CheckEval()
		{
			if (mLastEvalIdx == mEditWidgetContent.mData.mCurTextVersionId)
				return;
			mLastEvalIdx = mEditWidgetContent.mData.mCurTextVersionId;

			var editText = scope String();
			GetText(editText);

			var text = mEditWidgetContent.mData.mText;
			for (int i < mEditWidgetContent.mData.mTextLength)
			{
				text[i].mDisplayTypeId = 0;
			}

			for (let typeName in editText.Split(';'))
			{
				bool isValid = gApp.mBfResolveCompiler.VerifyTypeName(scope String(typeName), -1);
				if (!isValid)
				{
					for (int ofs < typeName.Length)
					{
						text[@typeName.Pos + ofs].mDisplayTypeId = 1;
					}
				}	
			}
		}

		public override void Update()
		{
			base.Update();
			CheckEval();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			if (mEditWidgetContent.mData.mTextLength == 0)
			{
				let drawLayer = gApp.GetOverlayLayer(mWidgetWindow);
				using (g.PushDrawLayer(drawLayer))
				{
					String str = "Enter wildcard to apply to. Examples:\n\x01\xE0\xE0\xE0\xFF  System.String; System.Collections.*\n\x01\xE0\xE0\xE0\xFF  [System.Optimize]";
					float width = mWidth - GS!(16);
					float height = g.mFont.GetWrapHeight(str, width);

					using (g.PushColor(0xFFA0A0A0))
						g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Window), -GS!(1), -height - GS!(12), mWidth + GS!(2), height + GS!(13));

					g.DrawString(str, GS!(8), -height - GS!(7), .Left, width, .Wrap);
				}
			}
		}
	}
}
