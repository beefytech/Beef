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
			mTexture = SDL.CreateTextureFromSurface(gApp.mRenderer, mSurface);

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

		public this()
		{

		}

		public ~this()
		{
			if (mFont != null)
				SDLTTF.CloseFont(mFont);
		}

		public Result<void> Load(StringView fileName, int32 pointSize)
		{
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

		public void Init()
		{
			String exePath = scope .();
			Environment.GetExecutableFilePath(exePath);
			String exeDir = scope .();
			Path.GetDirectoryPath(exePath, exeDir);
			Directory.SetCurrentDirectory(exeDir);

			SDL.Init(.Video | .Events | .Audio);
			SDL.EventState(.JoyAxisMotion, .Disable);
			SDL.EventState(.JoyBallMotion, .Disable);
			SDL.EventState(.JoyHatMotion, .Disable);
			SDL.EventState(.JoyButtonDown, .Disable);
			SDL.EventState(.JoyButtonUp, .Disable);
			SDL.EventState(.JoyDeviceAdded, .Disable);
			SDL.EventState(.JoyDeviceRemoved, .Disable);

			mWindow = SDL.CreateWindow(mTitle, .Undefined, .Undefined, mWidth, mHeight, .Hidden); // Initially hide window
			mRenderer = SDL.CreateRenderer(mWindow, -1, .Accelerated);
			mScreen = SDL.GetWindowSurface(mWindow);
			SDLImage.Init(.PNG | .JPG);
			mHasAudio = SDLMixer.OpenAudio(44100, SDLMixer.MIX_DEFAULT_FORMAT, 2, 4096) >= 0;

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
			SDL.Rect srcRect = .(0, 0, image.mSurface.w, image.mSurface.h);
			SDL.Rect destRect = .((int32)x, (int32)y, image.mSurface.w, image.mSurface.h);
			SDL.RenderCopy(mRenderer, image.mTexture, &srcRect, &destRect);
		}

		public void Draw(Image image, float x, float y, float rot, float centerX, float centerY)
		{
			SDL.Rect srcRect = .(0, 0, image.mSurface.w, image.mSurface.h);
			SDL.Rect destRect = .((int32)x, (int32)y, image.mSurface.w, image.mSurface.h);
			SDL.Point centerPoint = .((.)centerX, (.)centerY);
			SDL.RenderCopyEx(mRenderer, image.mTexture, &srcRect, &destRect, rot, &centerPoint, .None);
		}

		public void PlaySound(Sound sound, float volume = 1.0f, float pan = 0.5f)
		{
			if (sound == null)
				return;

			int32 channel = SDLMixer.PlayChannel(-1, sound.mChunk, 0);
			//SDLMixer.SetPanning()
			SDLMixer.Volume(channel, (int32)(volume * 128));
		}

		public void Render()
		{
			SDL.SetRenderDrawColor(mRenderer, 0, 0, 0, 255);
			SDL.RenderClear(mRenderer);
			Draw();
			SDL.RenderPresent(mRenderer);
		}

		public void Run()
		{
			Stopwatch sw = scope .();
			sw.Start();
			int curPhysTickCount = 0;

			while (true)
			{
				int32 waitTime = 1;
				SDL.Event event;

				while (SDL.PollEvent(out event) != 0)
				{
					switch (event.type)
					{
					case .Quit:
						return;
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
				int newPhysTickCount = (int)(sw.ElapsedMilliseconds / msPerTick);

				int addTicks = newPhysTickCount - curPhysTickCount;
				if (curPhysTickCount == 0)
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

				curPhysTickCount = newPhysTickCount;
			}
		}

	}

	static
	{
		public static SDLApp gApp;
	}
}
