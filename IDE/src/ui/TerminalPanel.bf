#pragma warning disable 168

using System;
using Beefy.geom;
using Beefy.gfx;
using System.Text;
using Beefy.theme.dark;
using System.Security.Cryptography;
using Beefy.widgets;
using Beefy.events;
using System.Diagnostics;
using Beefy.utils;

namespace IDE.ui;

class TerminalPanel : Panel
{
	public override void Serialize(StructuredData data)
	{
		base.Serialize(data);

		data.Add("Type", "TerminalPanel");
	}
}