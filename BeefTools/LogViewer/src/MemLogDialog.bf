using Beefy.theme.dark;
using Beefy.widgets;

namespace LogViewer;

class MemLogDialog : DarkDialog
{
	EditWidget mEditWidget;

	public this() : base("Open MemLog", "MemLog Name")
	{
		
		mDefaultButton = AddButton("OK", new (evt) =>
			{
				var name = mEditWidget.GetText(.. scope .());
				gApp.mBoard.LoadMemLog(name);
			});
		mEscButton = AddButton("Cancel", new (evt) => Close());
		mEditWidget = AddEdit("");
	}
}