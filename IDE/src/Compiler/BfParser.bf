using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy.widgets;
using Beefy.utils;
using IDE.ui;
using System.IO;
using System.Threading;
using Beefy;
using System.Security.Cryptography;

namespace IDE.Compiler
{
    public enum BfSourceElementType : uint8
    {
        Normal,
		Keyword,
		Literal,
		Identifier,	
		Comment,
		Method,
		Type,
		RefType,
		Interface,
		Namespace
    }

    public enum ResolveType
    {
        case None,
	        Classify,
	        ClassifyFullRefresh,
	        Autocomplete,
	        Autocomplete_HighPri,
	        GoToDefinition,
	        GetSymbolInfo,        
	        RenameSymbol,
	        ShowFileSymbolReferences,
	        GetNavigationData,
			GetCurrentLocation,
			GetFixits,
			GetTypeDefList,
			GetTypeDefInto,
			GetResultString;

		public bool IsClassify => (this == .Classify) || (this == .ClassifyFullRefresh);
    }

	public enum SourceEmbedKind
	{
		None,
		Type,
		Method
	}

    public class ResolveParams
    {
		public class Embed
		{
			public String mTypeName ~ delete _;
			public int32 mRevision = -1;
			public int32 mCursorIdx = -1;
			public EditWidgetContent.CharData[] mCharData ~ delete _;
		}

		public ResolveType mResolveType;
		public int32 mOverrideCursorPos = -1;
		public bool mInDeferredList;

		public EditWidgetContent.CharData[] mCharData ~ delete _;
		public IdSpan mCharIdSpan ~ _.Dispose();
		public BfParser mParser;

        public int32 mLocalId = -1;
        public String mReplaceStr ~ delete _;
        public String mTypeDef ~ delete _;
		public String mNamespace ~ delete _;
        public int32 mFieldIdx = -1;
        public int32 mMethodIdx = -1;
        public int32 mPropertyIdx = -1;
		public int32 mTypeGenericParamIdx = -1;
		public int32 mMethodGenericParamIdx = -1;

        public String mOutFileName ~ delete _;
        public int32 mOutLine;
        public int32 mOutLineChar;
        public String mNavigationData ~ delete _;
		public String mAutocompleteInfo ~ delete _;
		public String mResultString ~ delete _;
		public WaitEvent mWaitEvent ~ delete _;

		public BfPassInstance mPassInstance ~ delete _;
		public List<Embed> mEmitEmbeds = new .() ~ DeleteContainerAndItems!(_);
		public String mDocumentationName ~ delete _;
		public bool mCancelled;
		public int32 mTextVersion = -1;
		public bool mIsUserRequested;
		public bool mDoFuzzyAutoComplete;
		public Stopwatch mStopwatch ~ delete _;
		public ProfileInstance mProfileInstance ~ _.Dispose();
    }

    public class BfParser : ILeakIdentifiable
    {
		static int32 sIdx;
		public int32 mIdx = sIdx++;
		public void ToLeakString(String str)
		{
			str.AppendF("Idx:{0}", mIdx);
		}

		[CallingConvention(.Stdcall), CLink]
		static extern void BfSystem_DeleteParser(void* bfSystem, void* bfParser);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetSource(void* bfParser, char8* data, int32 length, char8* fileName);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetCharIdData(void* bfParser, uint8* data, int32 length);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfParser_SetHashMD5(void* bfParser, ref MD5Hash md5Hash);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetNextRevision(void* bfParser, void* nextParser);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetCursorIdx(void* bfParser, int32 cursorIdx);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetAutocomplete(void* bfParser, int32 cursorIdx);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfParser_SetEmbedKind(void* bfParser, SourceEmbedKind embedKind);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_SetIsClassifying(void* bfParser);

        [CallingConvention(.Stdcall), CLink]
        static extern bool BfParser_Parse(void* bfParser, void* bfPassInstance, bool compatMode);

        [CallingConvention(.Stdcall), CLink]
        static extern bool BfParser_Reduce(void* bfParser, void* bfPassInstance);

        [CallingConvention(.Stdcall), CLink]        
        static extern char8* BfParser_Format(void* bfParser, int32 formatEnd, int32 formatStart, out int32* outCharMapping, int32 maxCol);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* BfParser_GetDebugExpressionAt(void* bfParser, int32 cursorIdx);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfParser_CreateResolvePassData(void* bfSystem, int32 resolveType, bool doFuzzyAutoComplete);

        [CallingConvention(.Stdcall), CLink]
        static extern bool BfParser_BuildDefs(void* bfParser, void* bfPassInstance, void* bfResolvePassData, bool fullRefresh);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_RemoveDefs(void* bfParser);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_ClassifySource(void* bfParser, void* elementTypeArray, bool preserveFlags);

		[CallingConvention(.Stdcall), CLink]
        static extern void BfParser_CreateClassifier(void* bfParser, void* passInstance, void* resolvePassData, void* elementTypeArray);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfParser_FinishClassifier(void* bfParser, void* resolvePassData);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfParser_GenerateAutoCompletionFrom(void* bfParser, int32 srcPosition);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfParser_SetCompleteParse(void* bfParser);

		public BfSystem mSystem;
        public void* mNativeBfParser;
        public bool mIsUsed;
        public String mFileName ~ delete _;
        public ProjectSource mProjectSource;

        public this(void* nativeBfParser)
        {
            mNativeBfParser = nativeBfParser;
        }

		public ~this()
		{
			if (mNativeBfParser != null)
				BfSystem_DeleteParser(mSystem.mNativeBfSystem, mNativeBfParser);
		}

		public void Detach()
		{
			mNativeBfParser = null;
		}

        public void SetSource(StringView data, String fileName)
        {
            Debug.Assert(!mIsUsed);
            mIsUsed = true;
            BfParser_SetSource(mNativeBfParser, data.Ptr, (int32)data.Length, fileName);
        }

        public void SetCharIdData(ref IdSpan char8IdData)
        {
			char8IdData.Prepare();
            uint8* data = char8IdData.mData.CArray();
            BfParser_SetCharIdData(mNativeBfParser, data, char8IdData.mLength);
        }

        public void SetCursorIdx(int cursorIdx)
        {
            BfParser_SetCursorIdx(mNativeBfParser, (int32)cursorIdx);
        }

        public void SetAutocomplete(int cursorIdx)
        {
            BfParser_SetAutocomplete(mNativeBfParser, (int32)cursorIdx);
        }

		public void SetEmbedKind(SourceEmbedKind embedKind)
		{
			BfParser_SetEmbedKind(mNativeBfParser, embedKind);
		}

        public void SetIsClassifying()
        {
            BfParser_SetIsClassifying(mNativeBfParser);
        }

        public bool Parse(BfPassInstance passInstance, bool compatMode)
        {
            return BfParser_Parse(mNativeBfParser, passInstance.mNativeBfPassInstance, compatMode);
        }

        public bool Reduce(BfPassInstance passInstance)
        {
            return BfParser_Reduce(mNativeBfParser, passInstance.mNativeBfPassInstance);
        }

        public void Reformat(int formatStart, int formatEnd, out int32[] char8Mapping, String str)
        {                                    
            int32* char8MappingPtr;
            var maxCol = gApp.mSettings.mEditorSettings.mWrapCommentsAt;
            var stringPtr = BfParser_Format(mNativeBfParser, (int32)formatStart, (int32)formatEnd, out char8MappingPtr, maxCol);
            str.Append(stringPtr);

            char8Mapping = new int32[str.Length];
            for (int32 i = 0; i < char8Mapping.Count; i++)
                char8Mapping[i] = char8MappingPtr[i];
        }

        public bool GetDebugExpressionAt(int cursorIdx, String outExpr)
        {
            var stringPtr = BfParser_GetDebugExpressionAt(mNativeBfParser, (int32)cursorIdx);
			if (stringPtr == null)
				return false;
            outExpr.Append(stringPtr);
			return true;
        }

        public bool BuildDefs(BfPassInstance bfPassInstance, BfResolvePassData resolvePassData, bool fullRefresh)
        {
            void* nativeResolvePassData = null;
            if (resolvePassData != null)
                nativeResolvePassData = resolvePassData.mNativeResolvePassData;
            return BfParser_BuildDefs(mNativeBfParser, bfPassInstance.mNativeBfPassInstance, nativeResolvePassData, fullRefresh);
        }

        public void RemoveDefs()
        {
            BfParser_RemoveDefs(mNativeBfParser);
        }

        public void ClassifySource(EditWidgetContent.CharData[] char8Data, bool preserveFlags)
        {
            EditWidgetContent.CharData* char8DataPtr = char8Data.CArray();
            BfParser_ClassifySource(mNativeBfParser, char8DataPtr, preserveFlags);
        }

		public void CreateClassifier(BfPassInstance passInstance, BfResolvePassData bfResolvePassData, EditWidgetContent.CharData[] charDataArr)
		{
			EditWidgetContent.CharData* charDataPtr = charDataArr.CArray();
			BfParser_CreateClassifier(mNativeBfParser, passInstance.mNativeBfPassInstance, bfResolvePassData.mNativeResolvePassData, charDataPtr);
		}

		public void FinishClassifier(BfResolvePassData bfResolvePassData)
		{
			BfParser_FinishClassifier(mNativeBfParser, bfResolvePassData.mNativeResolvePassData);
		}

        public void SetNextRevision(BfParser nextRevision)
        {
            BfParser_SetNextRevision(mNativeBfParser, nextRevision.mNativeBfParser);
        }

        public void GenerateAutoCompletionFrom(int32 srcPosition)
        {
            BfParser_GenerateAutoCompletionFrom(mNativeBfParser, srcPosition);
        }

        public BfResolvePassData CreateResolvePassData(ResolveType resolveType = ResolveType.Autocomplete, bool doFuzzyAutoComplete = false)
        {
            var resolvePassData = new BfResolvePassData();
            resolvePassData.mNativeResolvePassData = BfParser_CreateResolvePassData(mNativeBfParser, (int32)resolveType, doFuzzyAutoComplete);
            return resolvePassData;
        }

        public void ReformatInto(SourceEditWidget editWidget, int formatStart, int formatEnd, uint8 orFlags = 0)
        {
            var editWidgetContent = (SourceEditWidgetContent)editWidget.Content;

			let oldText = scope String();
			editWidget.GetText(oldText);
			/*File.WriteAllText(@"c:\temp\orig.txt", origText);*/

            int32[] char8Mapping;
            String newText = scope String();
            Reformat(formatStart, formatEnd, out char8Mapping, newText);
			defer delete char8Mapping;

			//File.WriteAllText(@"c:\temp\new.txt", newText);

            SourceEditBatchHelper sourceEditBatchHelper = scope SourceEditBatchHelper(editWidget, "format");

            int32 insertCount = 0;
            int32 deleteCount = 0;

            int32 insertChars = 0;
            int32 deleteChars = 0;

            int32 insertStartIdx = -1;
            int32 expectedOldIdx = 0;
            for (int32 i = 0; i < char8Mapping.Count; i++)
            {
                int32 oldIdx = (int32)char8Mapping[i];

                if ((oldIdx != -1) && (insertStartIdx != -1))
                {
                    // Finish inserting
                    editWidgetContent.CursorTextPos = insertStartIdx;

                    String insertString = scope String();
                    //newText.Substring(insertStartIdx, i - insertStartIdx, insertString);
					insertString.Append(newText, insertStartIdx, i - insertStartIdx);
                    var insertTextAction = new EditWidgetContent.InsertTextAction(editWidgetContent, insertString, .None);
                    insertTextAction.mMoveCursor = false;
                    editWidgetContent.mData.mUndoManager.Add(insertTextAction);
                    
                    editWidgetContent.PhysInsertAtCursor(insertString, false);

                    if (orFlags != 0)
                    {
                        for (int32 idx = insertStartIdx; idx < i; idx++)
                            editWidgetContent.mData.mText[idx].mDisplayFlags |= orFlags;
                    }

                    insertStartIdx = -1;
                    insertCount++;
                    insertChars += (int32)insertString.Length;                    
                }

                if (oldIdx == expectedOldIdx)
                {
                    expectedOldIdx++;
                    continue;
                }
                else if (oldIdx == -1)
                {
                    if (insertStartIdx == -1)
                        insertStartIdx = i;
                }
                else
                {
					// This fails if we have the same ID occur multiple times, or we reorder nodes
					if (oldIdx <= expectedOldIdx)
					{
#if DEBUG
						Utils.WriteTextFile(@"c:\temp\old.txt", oldText).IgnoreError();
						Utils.WriteTextFile(@"c:\temp\new.txt", newText).IgnoreError();
						Debug.FatalError("Reformat failed");
#endif
						break; // Error - abort
					}
                    
                    int32 charsToDelete = oldIdx - expectedOldIdx;
                    editWidgetContent.CursorTextPos = i;
                    var deleteCharAction = new EditWidgetContent.DeleteCharAction(editWidgetContent, 0, charsToDelete);
                    deleteCharAction.mMoveCursor = false;
                    editWidgetContent.mData.mUndoManager.Add(deleteCharAction);
                    editWidgetContent.PhysDeleteChars(0, charsToDelete);
                    expectedOldIdx = oldIdx + 1;
                    deleteCount++;
                    deleteChars += charsToDelete;                    
                }
            }

            editWidgetContent.ContentChanged();
            Debug.WriteLine("Reformat {0} inserts, total of {1} chars. {2} deletes, total of {3} chars.", insertCount, insertChars, deleteCount, deleteChars);

            sourceEditBatchHelper.Finish();
        }
		
		public void SetCompleteParse()
		{
			BfParser_SetCompleteParse(mNativeBfParser);
		}

		public void SetHashMD5(MD5Hash md5Hash)
		{
			var md5Hash;
			BfParser_SetHashMD5(mNativeBfParser, ref md5Hash);
		}
    }
}
