using SDL2;
using System;
using System.IO;
using System.Diagnostics;
using System.Threading;

namespace SDL2
{
	class Image
	{
		public SDL.Surface* mSurface;
		public SDL.Texture* mTexture;
		public int32 mWidth;
		public int32 mHeight;

		public ~this()
		{
			if (mTexture != null)
				SDL.DestroyTexture(mTexture);
			if (mSurface != null)
				SDL.FreeSurface(mSurface);
		}

		public Result<void> Load(StringView fileName)
		{
			let origSurface = SDLImage.Load(fileName.ToScopeCStr!());
			if (origSurface == null)
				return .Err;

			mSurface = origSurface;

			SDL.Rect rect = .(0, 0, mSurface.w, mSurface.h);
			mTexture = SDL.CreateTexture(gApp.mRenderer, SDL.PIXELFORMAT_ABGR8888, (.)SDL.TextureAccess.Static, mSurface.w, mSurface.h);

			uint32* data = new uint32[mSurface.w*mSurface.h]*;
			defer delete data;

			int res = SDL.ConvertPixels(mSurface.w, mSurface.h, mSurface.format.format, mSurface.pixels, mSurface.pitch, SDL.PIXELFORMAT_ABGR8888, data, mSurface.w * 4);
			if (res == -1)
			{
				for (int y = 0; y < mSurface.h; y++)
					Internal.MemCpy(data + y*mSurface.w, (uint8*)mSurface.pixels + y*mSurface.pitch, mSurface.w*4);
			}

			SDL.UpdateTexture(mTexture, &rect, data, mSurface.w * 4);
			SDL.SetTextureBlendMode(mTexture, .Blend);

			mWidth = mSurface.w;
			mHeight = mSurface.h;

			return .Ok;
		}
	}

	class Sound
	{
		public SDLMixer.Chunk* mChunk;

		public ~this()
		{
			if (mChunk != null)
				SDLMixer.FreeChunk(mChunk);
		}

		public Result<void> Load(StringView fileName)
		{
			mChunk = SDLMixer.LoadWAV(fileName);
			if (mChunk == null)
				return .Err;
			return .Ok;
		}
	}

	class Font
	{
		public SDLTTF.Font* mFont;
		public BitmapFont mBMFont;

		public this()
		{

		}

		public ~this()
		{
			if (mFont != null)
				SDLTTF.CloseFont(mFont);
			delete mBMFont;
		}

		public Result<void> Load(StringView fileName, int32 pointSize)
		{
			if (fileName.EndsWith(".fnt"))
			{
				mBMFont = new BitmapFont();

				var contents = File.ReadAllText(fileName, .. scope .());
				contents.Replace("\r", "");
				for (var line in contents.Split('\n', .RemoveEmptyEntries))
				{
					bool CheckVal(StringView text, StringView key, ref int32 val)
					{
						if (!text.StartsWith(key))
							return false;
						if (text[key.Length] != '=')
							return false;
						val = int32.Parse(text.Substring(key.Length + 1));
						return true;
					}

					bool CheckVal(StringView text, String key, String val)
					{
						if (!text.StartsWith(key))
							return false;
						if (text[key.Length] != '=')
							return false;
						StringView sv = text.Substring(key.Length + 1);
						if (sv.StartsWith('"'))
							sv.RemoveFromStart(1);
						if (sv.EndsWith('"'))
							sv.RemoveFromEnd(1);
						val.Append(sv);
						return true;
					}

					if (line.StartsWith("page"))
					{
						String imageFileName = scope .();

						for (var text in line.Split(' ', .RemoveEmptyEntries))
							CheckVal(text, "file", imageFileName);

						var dir = Path.GetDirectoryPath(fileName, .. scope .());
						var imagePath = scope $"{dir}/{imageFileName}";

						BitmapFont.Page page = new BitmapFont.Page();
						page.mImage = new Image()..Load(imagePath);
						mBMFont.mPages.Add(page);
					}
					
					if (line.StartsWith("char"))
					{
						BitmapFont.CharData* charData = null;

						for (var text in line.Split(' ', .RemoveEmptyEntries))
						{
							int32 id = 0;
							if (CheckVal(text, "id", ref id))
							{
								while (id >= mBMFont.mCharData.Count)
									mBMFont.mCharData.Add(default);
								charData = &mBMFont.mCharData[id];
							}

							if (charData != null)
							{
								CheckVal(text, "x", ref charData.mX);
								CheckVal(text, "y", ref charData.mY);
								CheckVal(text, "width", ref charData.mWidth);
								CheckVal(text, "height", ref charData.mHeight);
								CheckVal(text, "xoffset", ref charData.mXOfs);
								CheckVal(text, "yoffset", ref charData.mYOfs);
								CheckVal(text, "xadvance", ref charData.mXAdvance);
								CheckVal(text, "page", ref charData.mPage);
							}
						}
					}
				}
				return .Ok;
			}

			mFont = SDLTTF.OpenFont(fileName.ToScopeCStr!(), pointSize);
			if (mFont == null)
				return .Err;
			return .Ok;
		}
	}

	public class SDLApp
	{
		public SDL.Window* mWindow;
		public SDL.Renderer* mRenderer;
		public SDL.Surface* mScreen;
		public int32 mUpdateCnt;
		public String mTitle = new .("Beef Sample") ~ delete _;
		public int32 mWidth = 1024;
		public int32 mHeight = 768;
		public bool* mKeyboardState;
		public bool mHasAudio;
		public bool mDidInit;

		private Stopwatch mFPSStopwatch = new .() ~ delete _;
		private int32 mFPSCount;
		private Stopwatch mStopwatch = new .() ~ delete _;
		private int mCurPhysTickCount = 0;

		public this()
		{
			gApp = this;
		}

		public ~this()
		{
			if (mRenderer != null)
				SDL.DestroyRenderer(mRenderer);
			if (mWindow != null)
				SDL.DestroyWindow(mWindow);
		}

		public void PreInit()
		{
#if BF_PLATFORM_WASM
			emscripten_set_main_loop(=> EmscriptenMainLoop, 0, 1);
#endif
		}

		public virtual void Init()
		{
			mDidInit = true;

			String exePath = scope .();
			Environment.GetExecutableFilePath(exePath);
			String exeDir = scope .();
			if (Path.GetDirectoryPath(exePath, exeDir) case .Ok)
				Directory.SetCurrentDirectory(exeDir);

			SDL.Init(.Video | .Events | .Audio);
			SDL.EventState(.JoyAxisMotion, .Disable);
			SDL.EventState(.JoyBallMotion, .Disable);
			SDL.EventState(.JoyHatMotion, .Disable);
			SDL.EventState(.JoyButtonDown, .Disable);
			SDL.EventState(.JoyButtonUp, .Disable);
			SDL.EventState(.JoyDeviceAdded, .Disable);
			SDL.EventState(.JoyDeviceRemoved, .Disable);

			//mWindow = SDL.CreateWindow(mTitle, .Undefined, .Undefined, mWidth, mHeight, .Hidden); // Initially hide window
			mWindow = SDL.CreateWindow(mTitle, .Undefined, .Undefined, mWidth, mHeight, .Shown); // Initially hide window

			mRenderer = SDL.CreateRenderer(mWindow, -1, .Accelerated);
			mScreen = SDL.GetWindowSurface(mWindow);
			SDLImage.Init(.PNG);
			mHasAudio = SDLMixer.OpenAudio(44100, SDLMixer.MIX_DEFAULT_FORMAT, 2, 4096) >= 0;

			SDL.SetRenderDrawBlendMode(mRenderer, .Blend);

			SDLTTF.Init();
		}

		public bool IsKeyDown(SDL.Scancode scancode)
		{
			if (mKeyboardState == null)
				return false;
			return mKeyboardState[(int)scancode];
		}

		public virtual void Update()
		{
			
		}

		public virtual void Draw()
		{
			
		}

		public virtual void KeyDown(SDL.KeyboardEvent evt)
		{
			if (evt.keysym.scancode == .Grave)
			{
				GC.Report();
			}
		}

		public virtual void KeyUp(SDL.KeyboardEvent evt)
		{

		}

		public virtual void MouseDown(SDL.MouseButtonEvent evt)
		{

		}

		public virtual void MouseUp(SDL.MouseButtonEvent evt)
		{

		}

		public virtual void HandleEvent(SDL.Event evt)
		{

		}

		public void Draw(Image image, float x, float y)
		{
			SDL.Rect srcRect = .(0, 0, image.mWidth, image.mHeight);
			SDL.Rect destRect = .((int32)x, (int32)y, image.mWidth, image.mHeight);
			SDL.RenderCopy(mRenderer, image.mTexture, &srcRect, &destRect);
		}

		public void Draw(Image image, float x, float y, float rot, float centerX, float centerY)
		{
			SDL.Rect srcRect = .(0, 0, image.mWidth, image.mHeight);
			SDL.Rect destRect = .((int32)x, (int32)y, image.mWidth, image.mHeight);
			SDL.Point centerPoint = .((.)centerX, (.)centerY);
			SDL.RenderCopyEx(mRenderer, image.mTexture, &srcRect, &destRect, rot, &centerPoint, .None);
		}

		public void DrawString(Font font, float x, float y, String str, SDL.Color color, bool centerX = false)
		{
			var x;
			if (font.mBMFont != null)
			{
				for (var page in font.mBMFont.mPages)
					SDL.SetTextureColorMod(page.mImage.mTexture, color.r, color.g, color.b);
				
				var drawX = x;
				var drawY = y;

				if (centerX)
				{
					float width = 0;

					for (var c in str.RawChars)
					{
						if ((int32)c >= font.mBMFont.mCharData.Count)
							continue;
						var charData = ref font.mBMFont.mCharData[(int32)c];
						width += charData.mXAdvance;
					}

					drawX -= width / 2;
				}

				for (var c in str.RawChars)
				{
					if ((int32)c >= font.mBMFont.mCharData.Count)
						continue;
					var charData = ref font.mBMFont.mCharData[(int32)c];

					SDL.Rect srcRect = .(charData.mX, charData.mY, charData.mWidth, charData.mHeight);
					SDL.Rect destRect = .((int32)drawX + charData.mXOfs, (int32)drawY + charData.mYOfs, charData.mWidth, charData.mHeight);
					SDL.RenderCopy(mRenderer, font.mBMFont.mPages[charData.mPage].mImage.mTexture, &srcRect, &destRect);

					drawX += charData.mXAdvance;
				}
			}
			else
			{
#if !NOTTF
				SDL.SetRenderDrawColor(mRenderer, 255, 255, 255, 255);
				let surface = SDLTTF.RenderUTF8_Blended(font.mFont, str, color);
				let texture = SDL.CreateTextureFromSurface(mRenderer, surface);
				SDL.Rect srcRect = .(0, 0, surface.w, surface.h);

				if (centerX)
					x -= surface.w / 2;

				SDL.Rect destRect = .((int32)x, (int32)y, surface.w, surface.h);
				SDL.RenderCopy(mRenderer, texture, &srcRect, &destRect);
				SDL.FreeSurface(surface);
				SDL.DestroyTexture(texture);
#endif
			}
		}

		public void PlaySound(Sound sound, float volume = 1.0f, float pan = 0.5f)
		{
			if (sound == null)
				return;

			int32 channel = SDLMixer.PlayChannel(-1, sound.mChunk, 0);
			if (channel < 0)
				return;
			SDLMixer.Volume(channel, (int32)(volume * 128));
		}

		public void Render()
		{
			SDL.SetRenderDrawColor(mRenderer, 0, 0, 0, 255);
			SDL.RenderClear(mRenderer);
			Draw();
			SDL.RenderPresent(mRenderer);
		}

#if BF_PLATFORM_WASM
		private function void em_callback_func();

		[CLink, CallingConvention(.Stdcall)]
		private static extern void emscripten_set_main_loop(em_callback_func func, int32 fps, int32 simulateInfinteLoop);

		[CLink, CallingConvention(.Stdcall)]
		private static extern int32 emscripten_set_main_loop_timing(int32 mode, int32 value);

		[CLink, CallingConvention(.Stdcall)]
		private static extern double emscripten_get_now();

		private static void EmscriptenMainLoop()
		{
			if (!gApp.mDidInit)
			{
				gApp.Init();
				gApp.mStopwatch.Start();
			}
			gApp.RunOneFrame();
		}
#endif

		public void Run()
		{
			mStopwatch.Start();

#if BF_PLATFORM_WASM
			emscripten_set_main_loop(=> EmscriptenMainLoop, 0, 1);
#else
			while (RunOneFrame()) {}
				
#endif
		}

		private bool RunOneFrame()
		{
			if (!mFPSStopwatch.IsRunning)
				mFPSStopwatch.Start();
			mFPSCount++;
			if (mFPSStopwatch.ElapsedMilliseconds > 1000)
			{
				//Debug.WriteLine($"FPS: {mFPSCount} @ {mStopwatch.Elapsed} now: {emscripten_get_now()}");
				mFPSCount = 0;
				mFPSStopwatch.Restart();
			}

			int32 waitTime = 1;
			SDL.Event event;

			while (SDL.PollEvent(out event) != 0)
			{
				switch (event.type)
				{
				case .Quit:
					return false;
				case .KeyDown:
					KeyDown(event.key);
				case .KeyUp:
					KeyUp(event.key);
				case .MouseButtonDown:
					MouseDown(event.button);
				case .MouseButtonUp:
					MouseUp(event.button);
				default:
				}

				HandleEvent(event);
				
				waitTime = 0;
			}

			// Fixed 60 Hz update
			double msPerTick = 1000 / 60.0;
			int newPhysTickCount = (int)(mStopwatch.ElapsedMilliseconds / msPerTick);

			int addTicks = newPhysTickCount - mCurPhysTickCount;
			if (mCurPhysTickCount == 0)
			{
				// Initial render
				Render();                
				// Show initially hidden window, mitigates white flash on slow startups
				SDL.ShowWindow(mWindow); 
			}
			else
			{
				mKeyboardState = SDL.GetKeyboardState(null);

				addTicks = Math.Min(addTicks, 20); // Limit catchup
				if (addTicks > 0)
				{
					for (int i < addTicks)
					{
						mUpdateCnt++;
						Update();
					}
					Render();
				}
				else
				{
					Thread.Sleep(1);
				}
			}

			mCurPhysTickCount = newPhysTickCount;
			return true;
		}

	}

	static
	{
		public static SDLApp gApp;
	}
}
