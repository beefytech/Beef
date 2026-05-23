using System;
using Beefy.events;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy;

namespace IDE.ui
{
	class RemoteDebugDialog : DarkDialog
	{
		public static String sLastHost = new String("localhost") ~ delete _;
		public static int32 sLastPort = 1234;
		public static String sLastElf = new String("") ~ delete _;

		EditWidget mHostEdit;
		EditWidget mPortEdit;
		PathEditWidget mElfEdit;

		public this()
		{
			mWindowFlags = BFWindow.Flags.ClientSized | .TopMost | .Caption |
				.Border | .SysMenu | .PopupPosition;

			AddOkCancelButtons(new (evt) => { Connect(); }, null, 0, 1);
			Title = "Connect to Remote Target";

			mHostEdit = AddEdit(sLastHost);
			var portStr = scope String();
			sLastPort.ToString(portStr);
			mPortEdit = AddEdit(portStr);

			mElfEdit = new PathEditWidget(.File);
			mElfEdit.SetText(sLastElf);
			AddWidget(mElfEdit);
		}

		void Connect()
		{
			var host = scope String();
			mHostEdit.GetText(host);
			host.Trim();

			var portStr = scope String();
			mPortEdit.GetText(portStr);
			portStr.Trim();

			int32 port = sLastPort;
			if (int32.Parse(portStr) case .Ok(let p))
				port = p;

			var elfPath = scope String();
			mElfEdit.GetText(elfPath);
			elfPath.Trim();

			sLastHost.Set(host);
			sLastPort = port;
			sLastElf.Set(elfPath);

			IDEApp.sApp.ConnectRemoteDebugger(host, port, elfPath);
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);
			using (g.PushColor(DarkTheme.COLOR_TEXT))
			{
				g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
				g.DrawString("Host:", mHostEdit.mX, mHostEdit.mY - 18);
				g.DrawString("Port:", mPortEdit.mX, mPortEdit.mY - 18);
				g.DrawString("ELF Binary:", mElfEdit.mX, mElfEdit.mY - 18);
			}
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();
			float editW = mWidth - 12;
			mHostEdit.Resize(6, 40, editW, 20);
			mPortEdit.Resize(6, 100, editW, 20);
			mElfEdit.Resize(6, 160, editW, 20);
		}

		public override void CalcSize()
		{
			mWidth = 320;
			mHeight = 240;
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}
	}
}
