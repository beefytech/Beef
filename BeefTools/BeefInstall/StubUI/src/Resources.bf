using System;
using Beefy.gfx;
using System.Collections.Generic;
using Beefy.sound;

namespace BIStubUI
{
	class Images
	{
		public static Image sBody;
		public static Image sHead;
		public static Image sEyesOpen;
		public static Image sEyesClosed;
		public static Image sButton;
		public static Image sButtonHi;
		public static Image sSmButton;
		public static Image sClose;
		public static Image sCloseHi;
		public static Image sChecked;
		public static Image sUnchecked;
		public static Image sTextBox;
		public static Image sBrowse;
		public static Image sBrowseDown;
		public static Image sPBBarBottom;
		public static Image sPBBarEmpty;
		public static Image sPBBarHilite;
		public static Image sPBFrameGlow;
		public static Image sPBFrameTop;

		static List<Image> sImages = new .() ~ delete _;

		public static Result<Image> Load(String fileName, bool additive = false)
		{
			let image = Image.LoadFromFile(scope String(gApp.mInstallDir, "/", fileName), additive);
			if (image == null)
			{
				delete image;
				return .Err;
			}
			sImages.Add(image);
			return image;
		}

		public static void Dispose()
		{
			ClearAndDeleteItems(sImages);
		}

		public static Result<void> Init()
		{
			sBody = Try!(Load("images/body.png"));
			sHead = Try!(Load("images/head.png"));
			sEyesOpen = Try!(Load("images/eyesopen.png"));
			sEyesClosed = Try!(Load("images/eyesclosed.png"));
			sButton = Try!(Load("images/button.png"));
			sButtonHi = Try!(Load("images/button_hi.png"));
			sSmButton = Try!(Load("images/smbutton.png"));
			sClose = Try!(Load("images/close.png"));
			sCloseHi = Try!(Load("images/close_hi.png"));
			sChecked = Try!(Load("images/checked.png"));
			sUnchecked = Try!(Load("images/unchecked.png"));
			sTextBox = Try!(Load("images/textbox.png"));
			sBrowse = Try!(Load("images/browse.png"));
			sBrowseDown = Try!(Load("images/browsedown.png"));
			sPBBarBottom = Try!(Load("images/pb_barbottom.png"));
			sPBBarEmpty = Try!(Load("images/pb_barempty.png"));
			sPBBarHilite = Try!(Load("images/pb_barhilite.png", true));
			sPBFrameGlow = Try!(Load("images/pb_frameglow.png"));
			sPBFrameTop = Try!(Load("images/pb_frametop.png"));
			return .Ok;
		}
	}

	class Sounds
	{
		public static SoundSource sBoing;
		public static SoundSource sEating;
		public static SoundSource sButtonPress;
		public static SoundSource sMouseOver;
		public static SoundSource sAbort;
		public static SoundSource sChecked;

		public static Result<SoundSource> Load(String fileName)
		{
			let source = gApp.mSoundManager.LoadSound(scope String(gApp.mInstallDir, "/", fileName));
			if (source.IsInvalid)
				return .Err;
			return source;
		}

		public static Result<void> Init()
		{
			sBoing = Try!(Load("sounds/boing.wav"));
			sEating = Try!(Load("sounds/eating.wav"));
			sButtonPress = Try!(Load("sounds/buttonpress.wav"));
			sMouseOver = Try!(Load("sounds/mouseover.wav"));
			sAbort = Try!(Load("sounds/abort.wav"));
			sChecked = Try!(Load("sounds/checked.wav"));
			return .Ok;
		}
	}
}
