#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;

namespace SDL
{
	struct SDL_Cursor
	{
		int mA;
	}
}

struct ImGui
{
	public enum MouseCursor
	{
		A,
		B,
		C,
		D,
		COUNT
	}
}

struct Blurg
{
	//private static SDL.SDL_Cursor*[(.)ImGui.MouseCursor.COUNT] g_MouseCursors = .(null,);

	private static SDL.SDL_Cursor*[(.)ImGui.MouseCursor.COUNT] g_MouseCursors = .(null,);

	public static void Hey()
	{
		let cur = new SDL.SDL_Cursor();

		g_MouseCursors[0] = cur;
		g_MouseCursors[1] = cur;
		g_MouseCursors[2] = cur;
	}

}


class TestClass
{
	public void GetIt(ref TestClass tc)
	{

	}

	public this()
	{
		/*let a = &this;

		GetIt(ref this);*/
	}
}