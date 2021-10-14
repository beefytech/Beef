using Beefy.theme.dark; using Beefy.theme;
using System;
using System.IO;
using System.Threading.Tasks;
using System.Threading;
using System.Collections;
using Beefy.gfx;

namespace IDE.ui
{
	class TypeWildcardEditWidget : ExpressionEditWidget
	{
		uint32[] mColors = new .(ThemeColors.Panel.WorkspaceProperties002.Color, ThemeColors.Panel.TypeWildcardEditWidget008.Color) ~ delete _;
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

			int cursorPos = doAutoComplete ? mEditWidgetContent.CursorTextPos : -1;

			int editOffset = 0;
			if (cursorPos > 0)
			{
				int semiPos = StringView(editText, 0, cursorPos).LastIndexOf(';');
				if (semiPos != -1)
					editOffset = semiPos + 1;
			}

			while (editOffset < editText.Length)
			{
				char8 c = editText[editOffset];
				if ((c != ':') && (!c.IsWhiteSpace))
					break;
				editOffset++;
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
			{
				isValid = gApp.mBfResolveCompiler.VerifyTypeName(editText, cursorPos);
			}

			for (int ofs < editText.Length)
			{
				mEditWidgetContent.mData.mText[editOffset + ofs].mDisplayTypeId = isValid ? 0 : 1;
			}
			
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

			for (var typeName in editText.Split(';'))
			{
				int startOfs = 0;
				while (!typeName.IsEmpty)
				{
					if ((typeName[0] != ':') && (!typeName[0].IsWhiteSpace))
						break;
					typeName.RemoveFromStart(1);
					startOfs++;
				}

				bool isValid = gApp.mBfResolveCompiler.VerifyTypeName(scope String(typeName), -1);
				if (!isValid)
				{
					for (int ofs < typeName.Length)
					{
						text[@typeName.Pos + startOfs + ofs].mDisplayTypeId = 1;
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

					using (g.PushColor(ThemeColors.Panel.WelcomePanel007.Color))
						g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Window), -GS!(1), -height - GS!(12), mWidth + GS!(2), height + GS!(13));

					using (g.PushColor(ThemeColors.Theme.Text.Color))
					g.DrawString(str, GS!(8), -height - GS!(7), .Left, width, .Wrap);
				}
			}
		}
	}
}
