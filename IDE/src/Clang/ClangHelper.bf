#if false

using System;
using System.Collections;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using IDE.Compiler;

namespace IDE.Clang
{
    public class ClangHelper : CompilerBase
    {
        [StdCall, CLink]
        static extern IntPtr ClangHelper_Create();

        [StdCall, CLink]
        static extern void ClangHelper_Delete(IntPtr clangHelper);

        [StdCall, CLink]
        static extern IntPtr ClangHelper_Classify(IntPtr clangHelper, char* fileName, void* elementTypeArray, int charLen, int cursorIdx);

        [StdCall, CLink]
        static extern IntPtr ClangHelper_FindDefinition(IntPtr clangHelper, char* fileName, int line, int column, out int outDefLine, out int outDefColumn);

        [StdCall, CLink]
        static extern IntPtr ClangHelper_Autocomplete(IntPtr clangHelper, char* fileName, int cursorIdx);

        IntPtr mNativeClangHelper;

        public this()
        {
            mNativeClangHelper = ClangHelper_Create();
        }

        public ~this()
        {
            ClangHelper_Delete(mNativeClangHelper);
            mNativeClangHelper = IntPtr.Zero;
        }

        public string Classify(string fileName, EditWidgetContent.CharData[] charData, int cursorIdx)
        {
            fixed (EditWidgetContent.CharData* charDataPtr = charData)
            {
                IntPtr returnStringPtr = ClangHelper_Classify(mNativeClangHelper, fileName, charDataPtr, charData.Length, cursorIdx);
                if (returnStringPtr == IntPtr.Zero)
                    return null;
                return Marshal.PtrToStringAnsi(returnStringPtr);
            }
        }

        public void FindDefinition(string fileName, int line, int column, out string defFileName, out int defLine, out int defColumn)
        {
            IntPtr fileNamePtr = ClangHelper_FindDefinition(mNativeClangHelper, fileName, line, column, out defLine, out defColumn);
            if (fileNamePtr == null)
            {
                defFileName = null;
                defLine = 0;
                defColumn = 0;
                return;
            }
            defFileName = Marshal.PtrToStringAnsi(fileNamePtr);
        }

        public string Autocomplete(string fileName, int cursorIdx)
        {
            IntPtr fileNamePtr = ClangHelper_Autocomplete(mNativeClangHelper, fileName, cursorIdx);
            if (fileNamePtr == IntPtr.Zero)
                return null;
            return Marshal.PtrToStringAnsi(fileNamePtr);
        }

        protected override void ProcessQueue()
        {            
            while (true)
            {
                Command command = null;
                using (mMonitor.Enter())
                {
                    if (mCommandQueue.Count == 0)
                        break;
                    command = mCommandQueue[0];
                }
            }
        }
    }
}

#endif