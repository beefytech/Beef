using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using Beefy.utils;

namespace IDE
{
    public class SpellChecker : CommandQueueManager
    {
        [StdCall, CLink]
        static extern void* SpellChecker_Create(char8* langPath);

        [StdCall, CLink]
        static extern void SpellChecker_Delete(void* nativeSpellchecker);

        [StdCall, CLink]
        static extern bool SpellChecker_IsWord(void* nativeSpellchecker, char8* word);

        [StdCall, CLink]        
        static extern void SpellChecker_AddWord(void* nativeSpellchecker, char8* word);

        [StdCall, CLink]
        static extern char8* SpellChecker_GetSuggestions(void* spellChecker, char8* word);

        void* mNativeSpellChecker;

        String[] mLangWordList = new String[] {
            "int", "uint", "struct", "bool", "enum", "int", "proj", "newbox", "params", "typeof",  "var"
        } ~ delete _;

		public String mLangPath ~ delete _;
        public HashSet<String> mIgnoreWordList = new HashSet<String>() ~ DeleteContainerAndItems!(_);
        public HashSet<String> mCustomDictionaryWordList = new HashSet<String>() ~ DeleteContainerAndItems!(_);

		public this()
		{

		}

        public Result<void> Init(String langPath)
        {
			scope AutoBeefPerf("SpellChecker.Init");

			mLangPath = new String(langPath);
            mNativeSpellChecker = SpellChecker_Create(langPath);
			if (mNativeSpellChecker == null)
				return .Err;
            
			String fileName = scope String();
			GetUserDirectFileName(fileName);
			//if (File.ReadLines(fileName) case .Ok(var lineEnumeratorVal))

			let streamReader = scope StreamReader();
			if (streamReader.Open(fileName) case .Ok)
			{
				for (var wordResult in streamReader.Lines)
				{
		            if (wordResult case .Err)
						break;
					AddWord(wordResult);
					mCustomDictionaryWordList.Add(new String(wordResult));
				}
			}
            
            for (var word in mLangWordList)
                AddWord(word);

			return .Ok;
        }

		public ~this()
		{
			CancelBackground();
		    SpellChecker_Delete(mNativeSpellChecker);
		    SaveWordList();
		}

        static void GetUserDirectFileName(String path)
        {
            path.Append(IDEApp.sApp.mInstallDir, "userdict.txt");
        }

		public static void ResetWordList()
		{
			String fileName = scope String();
			GetUserDirectFileName(fileName);
			File.Delete(fileName).IgnoreError();
		}

        protected override void DoProcessQueue()
        {
            
        }

        public void SaveWordList()
        {
			String fileName = scope String();
			GetUserDirectFileName(fileName);
            File.WriteAllLines(fileName, mCustomDictionaryWordList.GetEnumerator());
        }


        public bool IsWord(String word)
        {
            return SpellChecker_IsWord(mNativeSpellChecker, word);
        }

        public void AddWord(StringView word)
        {
            SpellChecker_AddWord(mNativeSpellChecker, word.ToScopeCStr!());
        }

        public void GetSuggestions(String word, List<String> suggestions)
        {
            char8* result = SpellChecker_GetSuggestions(mNativeSpellChecker, word);
            String resultStr = scope String(result);
			var stringViews = scope List<StringView>(resultStr.Split('\n'));
			for (var view in stringViews)
				suggestions.Add(new String(view));
        }
    }
}
