using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.events;

namespace IDE.ui
{
    class GoToLineDialog : DarkDialog
    {
        SourceViewPanel mSourceViewPanel;
        int mCursorPos;
        double mVertPos;

        public this(String title = null, String text = null, Image icon = null) :
            base(title, text, icon)
        {

        }

        public void Init(SourceViewPanel sourceViewPanel)
        {
            mSourceViewPanel = sourceViewPanel;

            int line;
            int lineChar;
            mSourceViewPanel.mEditWidget.Content.GetCursorLineChar(out line, out lineChar);

            mCursorPos = sourceViewPanel.mEditWidget.Content.CursorTextPos;
            mVertPos = sourceViewPanel.mEditWidget.mVertPos.mDest;

            mDefaultButton = AddButton("OK", new (evt) => GotoLineSubmit(true));
            mEscButton = AddButton("Cancel", new (evt) => Cancel());
            var editWidget = AddEdit(StackStringFormat!("{0}", line + 1));
            editWidget.mOnContentChanged.Add(new (evt) => GotoLineSubmit(false));
        }

        void GotoLineSubmit(bool isFinal)
        {
            var editWidgetContent = mSourceViewPanel.mEditWidget;

			var text = scope String();
			mDialogEditWidget.GetText(text);
			var lineResult = int32.Parse(text);
            //if (!lineResult.Failed(true))
			if (lineResult case .Ok(var line))
            {
                line--;
                if ((line < 0) || (line >= editWidgetContent.Content.GetLineCount()))
                {
                    IDEApp.Beep(IDEApp.MessageBeepType.Error);
                    return;
                }
                
                int column = ((SourceEditWidgetContent)editWidgetContent.Content).GetLineEndColumn(line, false, true, true);
                editWidgetContent.Content.CursorLineAndColumn = EditWidgetContent.LineAndColumn(line, column);
                editWidgetContent.Content.CursorMoved();
                editWidgetContent.Content.EnsureCursorVisible(true, true);

                if (isFinal)
                {
                    editWidgetContent.Content.mSelection = null;
                    mSourceViewPanel.RecordHistoryLocation();
                }
                else
                {
                    int lineStart;
                    int lineEnd;
                    editWidgetContent.Content.GetLinePosition(line, out lineStart, out lineEnd);
                    editWidgetContent.Content.mSelection = EditSelection(lineStart, lineEnd + 1);
                }
            }
        }

        void Cancel()
        {
            var editWidgetContent = mSourceViewPanel.mEditWidget;
            editWidgetContent.Content.mSelection = null;

            mSourceViewPanel.mEditWidget.Content.CursorTextPos = mCursorPos;
            mSourceViewPanel.mEditWidget.mVertPos.Set(mVertPos);
        }
    }
}
