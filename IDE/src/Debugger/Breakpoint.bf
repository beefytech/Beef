using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.theme.dark;
using System.IO;

namespace IDE.Debugger
{
    public class Breakpoint : TrackedTextElement
    {
		[Reflect]
		public enum HitCountBreakKind
		{
			None,
			Equals,
			GreaterEquals,
			MultipleOf
		};

		public enum SetKind
		{
			Toggle,
			Force,
			EnsureExists,
			MustExist
		}

		public enum SetFlags
		{
			None,
			Configure,
			Disable
		}

        [CallingConvention(.Stdcall), CLink]
        static extern void Breakpoint_Delete(void* nativeBreakpoint);
        
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Breakpoint_GetPendingHotBindIdx(void* nativeBreakpoint);

		[CallingConvention(.Stdcall), CLink]
		static extern void Breakpoint_HotBindBreakpoint(void* breakpoint, int32 lineNum, int32 hotIdx);

        [CallingConvention(.Stdcall), CLink]
        static extern int Breakpoint_GetAddress(void* nativeBreakpoint, out void* linkedSibling);

		[CallingConvention(.Stdcall), CLink]
		static extern bool Breakpoint_SetCondition(void* nativeBreakpoint, char8* condition);

		[CallingConvention(.Stdcall), CLink]
		static extern bool Breakpoint_SetLogging(void* nativeBreakpoint, char8* logging, bool breakAfterLogging);

        [CallingConvention(.Stdcall), CLink]
        static extern bool Breakpoint_IsMemoryBreakpointBound(void* nativeBreakpoint);

        [CallingConvention(.Stdcall), CLink]
        static extern int32 Breakpoint_GetLineNum(void* nativeBreakpoint);

        [CallingConvention(.Stdcall), CLink]
        static extern void Breakpoint_Move(void* nativeBreakpoint, int32 wantLineNum, int32 wantColumn, bool rebindNow);

        [CallingConvention(.Stdcall), CLink]
        static extern void Breakpoint_MoveMemoryBreakpoint(void* nativeBreakpoint, int addr, int32 byteCount);

        [CallingConvention(.Stdcall), CLink]
        static extern void Breakpoint_Disable(void* nativeBreakpoint);        

        [CallingConvention(.Stdcall), CLink]
        static extern void* Debugger_CreateBreakpoint(char8* fileName, int32 wantLineNum, int32 wantColumn, int32 instrOffset);

		[CallingConvention(.Stdcall),CLink]
		static extern void* Debugger_CreateSymbolBreakpoint(char8* symbolName);

		[CallingConvention(.Stdcall), CLink]
		static extern void* Breakpoint_Check(void* nativeBreakpoint);

		[CallingConvention(.Stdcall), CLink]
		static extern void Breakpoint_SetThreadId(void* nativeBreakpoint, int threadId);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 Breakpoint_GetHitCount(void* nativeBreakpoint);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 Breakpoint_ClearHitCount(void* nativeBreakpoint);

		[CallingConvention(.Stdcall), CLink]
		static extern void Breakpoint_SetHitCountTarget(void* nativeBreakpoint, int hitCountTarget, HitCountBreakKind breakKind);

		[CallingConvention(.Stdcall), CLink]
		static extern void* Debugger_GetActiveBreakpoint();

		[CallingConvention(.Stdcall), CLink]
		static extern void* Debugger_CreateMemoryBreakpoint(int addr, int32 byteCount);

        public void* mNativeBreakpoint;
       	public String mSymbol ~ delete _;
        public bool mIsDisposed;
        public int32 mInstrOffset;
        public bool mAddressRequested;
        public bool mIsMemoryBreakpoint;
        public uint8 mByteCount;
        public int mMemoryAddress;
		public HitCountBreakKind mHitCountBreakKind;
		public int mHitCountTarget;

		public String mAddrType ~ delete _;
        public String mMemoryWatchExpression ~ delete _;
		public String mCondition ~ delete _;
		public String mLogging ~ delete _;
		public bool mBreakAfterLogging;
		public int mThreadId = -1;
		public bool mDisabled;
		public bool mDeleteOnUnbind;

		public Event<Action> mOnDelete;

        public ~this()
        {
			mOnDelete();
            mIsDisposed = true;
            DisposeNative();            
        }

		void RehupBreakpointSettings()
		{
			if (!String.IsNullOrEmpty(mCondition))
				SetCondition();
			if (!String.IsNullOrEmpty(mLogging))
				SetLogging();
			if (mHitCountBreakKind != .None)
				Breakpoint_SetHitCountTarget(mNativeBreakpoint, mHitCountTarget, mHitCountBreakKind);
			if (mThreadId != -1)
				Breakpoint_SetThreadId(mNativeBreakpoint, mThreadId);
		}

        public void CreateNative(bool bindNow = true, bool force = false)
        {
            //var debugger = IDEApp.sApp.mDebugger;
            if (mNativeBreakpoint == null)
            {
                if (mIsMemoryBreakpoint)
                {
					if (force)
						mNativeBreakpoint = Debugger_CreateMemoryBreakpoint(mMemoryAddress, mByteCount);
					else
					{
						// Wait for a 'rehup'
					}
                }
                else if (mAddressRequested)
                {
					if (force)
					{
						//mNativeBreakpoint = Debugger_CreateAddressBreakpoint(mMemoryAddress);
					}
					else
					{
						// Wait for a 'rehup'
					}
                    
                }
                else if (mFileName != null)
                {
                    mNativeBreakpoint = Debugger_CreateBreakpoint(mFileName, mLineNum, mColumn,  mInstrOffset);
					RehupBreakpointSettings();
					if (bindNow)
						Breakpoint_Check(mNativeBreakpoint);
					CheckBreakpointHotBinding();
                }
				else if (mSymbol != null)
				{
					mNativeBreakpoint = Debugger_CreateSymbolBreakpoint(mSymbol);
					RehupBreakpointSettings();
					if (bindNow)
						Breakpoint_Check(mNativeBreakpoint);
					CheckBreakpointHotBinding();
				}
            }
        }

		public override void Kill()
		{
			DisposeNative();
			base.Kill();
		}

        public void DisposeNative()
        {
            if (mNativeBreakpoint != null)
            {
                Breakpoint_Delete(mNativeBreakpoint);
                mNativeBreakpoint = null;
            }
        }

        public bool IsBound()
        {
            if (mNativeBreakpoint == null)
                return false;

            void* linkedSibling;
            if (mIsMemoryBreakpoint)
                return Breakpoint_IsMemoryBreakpointBound(mNativeBreakpoint);
            else
                return Breakpoint_GetAddress(mNativeBreakpoint, out linkedSibling) != (int)0;
        }

		public bool IsActiveBreakpoint()
		{
			if (mNativeBreakpoint == null)
				return false;
			return mNativeBreakpoint == Debugger_GetActiveBreakpoint();
		}

        public int GetAddress(ref void* curBreakpoint)
        {
            if (curBreakpoint == null)
                curBreakpoint = mNativeBreakpoint;
            if (curBreakpoint == null)
                return 0;
            return Breakpoint_GetAddress(curBreakpoint, out curBreakpoint);
        }

        public bool ContainsAddress(int addr)
        {
            void* breakItr = mNativeBreakpoint;
            while (true)
            {
                if (breakItr == null)
                    return false;
                int breakAddr = Breakpoint_GetAddress(breakItr, out breakItr);
                if (breakAddr == addr)
                    return true;                
            }
        }

		void SetCondition()
		{
			if (mNativeBreakpoint == null)
				return;

			if (String.IsNullOrWhiteSpace(mCondition))
			{
				Breakpoint_SetCondition(mNativeBreakpoint, "");
				return;
			}

			String condition = scope .();
			GetEvalStrWithSubject(mCondition, condition);
			Breakpoint_SetCondition(mNativeBreakpoint, condition);
		}

		public void SetCondition(String condition)
		{
			String.NewOrSet!(mCondition, condition);
			mCondition.Trim();
			SetCondition();
			if (mCondition.Length == 0)
			{
				delete mCondition;
				mCondition = null;
			}
		}

		public void SetThreadId(int threadId)
		{
			mThreadId = threadId;
			if (mNativeBreakpoint != null)
				Breakpoint_SetThreadId(mNativeBreakpoint, mThreadId);
		}

		void GetEvalStrWithSubject(String exprStr, String outStr)
		{
			if (mAddrType == null)
			{
				outStr.Append(exprStr);
				return;
			}

			var enumerator = mAddrType.Split('\t');
			int langVal = int.Parse(enumerator.GetNext().Get()).Get();
			StringView addrVal = enumerator.GetNext().Get();
			if (langVal == (.)DebugManager.Language.C)
				outStr.Append("@C:");
			else
				outStr.Append("@Beef:");
			outStr.Append(exprStr);
			outStr.AppendF("\n({0}*)0x", addrVal);
			mMemoryAddress.ToString(outStr, "X", null);
			outStr.Append("L");
		}

		void SetLogging()
		{
			if (mNativeBreakpoint == null)
				return;

			if (String.IsNullOrWhiteSpace(mLogging))
			{
				Breakpoint_SetLogging(mNativeBreakpoint, "", false);
				return;
			}

			String logging = scope .();
			GetEvalStrWithSubject(mLogging, logging);
			Breakpoint_SetLogging(mNativeBreakpoint, logging, mBreakAfterLogging);
		}

		public void SetLogging(String logging, bool breakAfterLogging)
		{
			String.NewOrSet!(mLogging, logging);
			mLogging.Trim();
			mBreakAfterLogging = breakAfterLogging;
			SetLogging();
			if (mLogging.Length == 0)
			{
				delete mLogging;
				mLogging = null;
			}
		}

		public void SetHitCountTarget(int hitCountTarget, HitCountBreakKind hitCountBreakKind)
		{
			mHitCountTarget = hitCountTarget;
			mHitCountBreakKind = hitCountBreakKind;
			if (mNativeBreakpoint != null)
				Breakpoint_SetHitCountTarget(mNativeBreakpoint, hitCountTarget, hitCountBreakKind);
		}

        public bool HasNativeBreakpoint()
        {
            return mNativeBreakpoint != null;
        }

        public int32 GetLineNum()
        {
            /*if (mNativeBreakpoint == null)
                return mLineNum;*/
			if (mNativeBreakpoint == null)
				return mLineNum;
            int32 lineNum = Breakpoint_GetLineNum(mNativeBreakpoint);            
			if (lineNum != -1)
				return lineNum;
			return mLineNum;
        }

        public override void Move(int wantLineNum, int wantColumn)
        {            
            //Breakpoint_Move(mNativeBreakpoint, wantLineNum, wantColumn);
            mLineNum = (int32)wantLineNum;
            mColumn = (int32)wantColumn;
        }

        public void MoveMemoryBreakpoint(int addr, int byteCount, String addrType)
        {			
            mMemoryAddress = addr;
			mByteCount = (uint8)byteCount;
			if (addrType != null)
				String.NewOrSet!(mAddrType, addrType);
			if (mNativeBreakpoint == null)			
			{
				mNativeBreakpoint = Debugger_CreateMemoryBreakpoint(addr, (.)byteCount);
				RehupBreakpointSettings();
			}
			else
            	Breakpoint_MoveMemoryBreakpoint(mNativeBreakpoint, addr, (.)byteCount);
        }

        public void Rehup(bool rebindNow)
        {
            Breakpoint_Move(mNativeBreakpoint, mLineNum, mColumn, rebindNow);            
			CheckBreakpointHotBinding();
        }

        public void Disable()
        {
            if (mNativeBreakpoint != null)
            {
				//Breakpoint_Disable(mNativeBreakpoint);
				Breakpoint_Delete(mNativeBreakpoint);
				mNativeBreakpoint = null;
			}
        }

		public void CheckBreakpointHotBinding()
		{
			if (mNativeBreakpoint == null)
				return;

			ProjectSource projectSource;

			while (true)
			{
				int32 hotIdx = Breakpoint_GetPendingHotBindIdx(mNativeBreakpoint);
				if (hotIdx == -1)
					break;

				projectSource = gApp.FindProjectSourceItem(mFileName);
				if (projectSource == null)
					break;

				var editData = gApp.GetEditData(projectSource, true);
				int char8Id = editData.mEditWidget.Content.GetSourceCharIdAtLineChar(mLineNum, mColumn);
				if (char8Id == 0)
				    break;
				int lineNum = mLineNum;
				int column = mColumn;
				if (!gApp.mWorkspace.GetProjectSourceCharIdPosition(projectSource, hotIdx, char8Id, ref lineNum, ref column))
					lineNum = -1; // Don't bind
				Breakpoint_HotBindBreakpoint(mNativeBreakpoint, (int32)lineNum, hotIdx);
			}
		}

		public void Draw(Graphics g, bool isOldCompiledVersion)
		{
			uint32 color = Color.White;
			bool isBreakEx = (mCondition != null) || (mLogging != null) || (mHitCountBreakKind != .None);
			Image image = DarkTheme.sDarkTheme.GetImage(isBreakEx ? .RedDotEx : .RedDot);
			Image threadImage = null;

			if (this == gApp.mDebugger.mRunToCursorBreakpoint)
			{
				image = DarkTheme.sDarkTheme.GetImage(.RedDotRunToCursor);
			}
			else
			{
				if (mDisabled)
				{
					image = DarkTheme.sDarkTheme.GetImage(isBreakEx ? DarkTheme.ImageIdx.RedDotExDisabled : DarkTheme.ImageIdx.RedDotDisabled);
				}
				else if (!isOldCompiledVersion)
				{
				    //color = (breakpoint.IsBound()) ? Color.White : 0x8080FFFF;
				    if ((!IsBound()) || (mThreadId == 0))
				        image = DarkTheme.sDarkTheme.GetImage(isBreakEx ? DarkTheme.ImageIdx.RedDotExUnbound : DarkTheme.ImageIdx.RedDotUnbound);
				}
				else
				    color = 0x8080FFFF;

				if (mThreadId == -1)
				{
					// Nothing
				}
				else if (mThreadId == 0)
					threadImage = DarkTheme.sDarkTheme.GetImage(.ThreadBreakpointUnbound);
				else if ((!gApp.mDebugger.IsPaused()) || (mThreadId == gApp.mDebugger.GetActiveThread()))
					threadImage = DarkTheme.sDarkTheme.GetImage(.ThreadBreakpointMatch);
				else
					threadImage = DarkTheme.sDarkTheme.GetImage(.ThreadBreakpointNoMatch);
			}

			using (g.PushColor(color))
			{                                
			    g.Draw(image, 0, 0);
				if (threadImage != null)
					g.Draw(threadImage, GS!(-11), 0);
			}
		}
		
		public int GetHitCount()
		{
			if (mNativeBreakpoint == null)
				return 0;
			return Breakpoint_GetHitCount(mNativeBreakpoint);
		}

		public void ClearHitCount()
		{
			if (mNativeBreakpoint == null)
				return;
			Breakpoint_ClearHitCount(mNativeBreakpoint);
		}

		public void ToString_Location(String str)
		{
			if (mFileName != null)
			{
				Path.GetFileName(mFileName, str);
				str.AppendF(":{0}", mLineNum + 1);
				if (mInstrOffset >= 0)
					str.AppendF("+{0}", mInstrOffset);
			}
			else if (mAddressRequested)
			{
			    void* curSubBreakpoint = null;
			    int addr = GetAddress(ref curSubBreakpoint);
			    str.AppendF("0x{0:X08}", addr);
			}                
			else if (mIsMemoryBreakpoint)
			{
			    void* curSubBreakpoint = null;
			    if (IsBound())
			    {
#unwarn
			        int addr = GetAddress(ref curSubBreakpoint);
			        str.AppendF("When '0x{0:X08}' ({1}) changes", mMemoryAddress, mMemoryWatchExpression);
			    }
			    else
			    {
			        str.AppendF("Unbound: When ({0}) changes", mMemoryWatchExpression);
			    }
			}
			else if (mSymbol != null)
			{
				str.Append(mSymbol);
			}
		}

		public void ToString_HitCount(String str)
		{
			str.AppendF("{0}", GetHitCount());
			switch (mHitCountBreakKind)
			{
			case .Equals: str.AppendF(", Break={0}", mHitCountTarget);
			case .GreaterEquals: str.AppendF(", Break>={0}", mHitCountTarget);
			case .MultipleOf: str.AppendF(", Break%{0}", mHitCountTarget);
			default:
			}
		}
    }
}
