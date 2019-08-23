/*#include <stdio.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <iostream>
#include <assert.h>
#include <algorithm>
#include <fstream>
#include <consoleapi.h>*/

#include "Beef/BfCommon.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/Array.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

static FILE* gLogFP;

/*static void Log(const char* fmt ...)
{		
	if (gLogFP == NULL)
		gLogFP = fopen("diff_dbg.txt", "wb");

	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

	fwrite(aResult.c_str(), 1, aResult.length(), gLogFP);
	fflush(gLogFP);
}

static void LogClose()
{
	if (gLogFP != NULL)
	{
		fclose(gLogFP);
		gLogFP = NULL;
	}
}*/

struct DuplicateLineData
{
	Array<int> mRefs[2];
	Array<int> mDeleteEditCommandIndices;
};

struct LineData
{
public:
	//String mString;
	int mBufStart;
	int mBufLength;
	DuplicateLineData* mDuplicateLineData;
};

bool operator!=(const LineData &a, const LineData& b)
{
	return a.mDuplicateLineData != b.mDuplicateLineData;
}

typedef Dictionary<int64, DuplicateLineData*> DuplicateLineDataMap;
static DuplicateLineDataMap gDuplicateLineDataMap;

struct LineMapping
{
	int mAIdx;
	int mBIdx;	
	LineMapping* mPrevStackTop;
};


enum EditCommandKind
{
	EditCommandKind_None,	

	EditCommandKind_InsertLine,
	EditCommandKind_DeleteLine,
	EditCommandKind_InsertChar,
	EditCommandKind_DeleteChar,

	// Forward moves:
	EditCommandKind_MoveLineFrom, // Moves line down
	EditCommandKind_MoveLineTo, // Moves line up into us

								// Reverse moves:
	EditCommandKind_MoveLine, // For reverse moves
};

typedef unsigned char uint8;

struct EditCommand
{
public:		
	int mAIdx;	
	int mBIdx;
	short mCount;
	uint8 mCmd;

public:
	EditCommand()
	{
		mAIdx = -1;
		mBIdx = -1;
		mCount = 1;
		mCmd = 0;
	}
};

class BfDiff
{
public:
	Array<EditCommand> mEditCommands;

	const char* mAText;
	const char* mBText;

	Array<LineData> mALineDatas;
	Array<LineData> mBLineDatas;	

public:	
	int GetInsertPosition(int lineNum);
	void ProcessText(const char* bufferData, Array<LineData>& lines, int fileNum);
	int AddEditCommand(int cmdInsertPos, const EditCommand& editCommand);

public:
	void Compare(const char* text1, const char* text2);
	void GenerateMoveCommands();
};

int BfDiff::AddEditCommand(int cmdInsertPos, const EditCommand& editCommand)
{
	//Log("AddEditCommand Pos:%d Cmd:%d A:%d B:%d Count:%d\n", cmdInsertPos, editCommand.mCmd, editCommand.mAIdx, editCommand.mBIdx, editCommand.mCount);

	if (cmdInsertPos == -1)
		cmdInsertPos = (int)mEditCommands.size();
	if ((cmdInsertPos > 0) && (cmdInsertPos - 1 < mEditCommands.size()) && (editCommand.mCount == 1))
	{
		EditCommand& prevEditCommand = mEditCommands[cmdInsertPos - 1];
		if ((editCommand.mCmd == EditCommandKind_InsertLine) || (editCommand.mCmd == EditCommandKind_InsertChar))
		{
			if ((prevEditCommand.mCmd == editCommand.mCmd) &&
				(prevEditCommand.mCount < 0x7FFF) &&
				(editCommand.mAIdx == prevEditCommand.mAIdx) &&
				(editCommand.mBIdx == prevEditCommand.mBIdx + prevEditCommand.mCount))
			{
				prevEditCommand.mCount += editCommand.mCount;
				return cmdInsertPos;
			}
		}
		else if ((editCommand.mCmd == EditCommandKind_DeleteLine) || (editCommand.mCmd == EditCommandKind_DeleteChar))
		{
			if ((prevEditCommand.mCmd == editCommand.mCmd) &&
				(prevEditCommand.mCount < 0x7FFF) &&
				(editCommand.mAIdx == prevEditCommand.mAIdx + prevEditCommand.mCount))
			{
				prevEditCommand.mCount += editCommand.mCount;
				return cmdInsertPos;
			}
		}		
	}

	mEditCommands.Insert(cmdInsertPos, editCommand);
	return cmdInsertPos + 1;
}

class DiffEngineBase
{
public:
	virtual void EmitEntryEqual(int aIdx, int bIdx) {}
	virtual void EmitEntryDelete(int aIdx) {}
	virtual void EmitEntryInsert(int aIdx, int bIdx) {}
};

template <typename TElement, typename TContainer>
class DiffEngine : public DiffEngineBase
{
public:
	TContainer mA;
	TContainer mB;

public:
	DiffEngine(TContainer a, TContainer b) :
		mA(a), mB(b)
	{
	}	

	void CalcMiddleSnake(int aOfs, int aLen, int bOfs, int bLen, int& xStart, int& yStart, int& xEnd, int& yEnd, int& totalDiff)
	{
		int maxD = (bLen + aLen + 1) / 2;

		int delta = aLen - bLen;

		int vOfs = maxD + abs(delta);

		// v stores x values for k lines
		Array<int> v;
		v.Resize(vOfs * 2 + 1);

		Array<int> rv;
		rv.Resize(vOfs * 2 + 1);
		rv[vOfs + delta] = aLen;	

		for (int d = 0; d <= maxD; d++)
		{
			// Forward
			for (int k = -d; k <= d; k += 2)
			{
				int newX;
				if ((k == -d) || ((k != d) && (v[vOfs + k + 1] > v[vOfs + k - 1])))
				{
					// 'k' right of us moves down
					xStart = v[vOfs + k + 1];								
					yStart = xStart - (k + 1);
					newX = xStart;
				}
				else
				{
					// 'k' below of us moves right
					xStart = v[vOfs + k - 1];								
					yStart = xStart - (k - 1);
					newX = xStart + 1;
				}

				int beforeDiagX = newX;
				int newY = newX - k;
				while (newX < aLen)
				{					
					if (newY >= bLen)
						break;
					if (mA[aOfs + newX] != mB[bOfs + newY])
						break;				
					newX++;
					newY++;
				}

				// This is a complicated text, but see Myers paper-
				//  But basically this is checking for overlap, where the forward reaches the reverse
				if ((delta % 2 != 0) && (k >= delta - (d - 1)) && (k <= delta + (d - 1)) && (newX >= rv[vOfs + k]))
				{				
					xEnd = newX;
					yEnd = newY;
					totalDiff = 2 * d - 1;
					return;
				}

				v[vOfs + k] = newX;
			}

			// Reverse
			for (int k = -d + delta; k <= d + delta; k += 2)
			{
				int newX;
				if (d == 0)
				{
					newX = aLen;
					xEnd = aLen;
					yEnd = xEnd - k;
				}
				else if ((k == -d + delta) || ((k != d + delta) && (rv[vOfs + k + 1] < rv[vOfs + k - 1])))
				{
					// 'k' right of us moves left
					xEnd = rv[vOfs + k + 1];
					yEnd = xEnd - (k + 1);
					newX = xEnd - 1;
				}
				else
				{
					// 'k' below of us moves up
					xEnd = rv[vOfs + k - 1];
					yEnd = xEnd - (k - 1);
					newX = xEnd;
				}

				int beforeDiagX = newX;
				int newY = newX - k;
				while (newX > 0)
				{				
					if (newY <= 0)
						break;
					if (mA[aOfs + newX - 1] != mB[bOfs + newY - 1])
						break;				
					newX--;
					newY--;				
				}

				// This is a complicated text, but see Myers paper-
				//  But basically this is checking for overlap, where the forward reaches the reverse
				if ((delta % 2 == 0) && (k >= -d) && (k <= d) && ((newX <= v[vOfs + k])))			
				{				
					xStart = newX;
					yStart = newY;
					totalDiff = 2 * d;
					return;
				}

				rv[vOfs + k] = newX;
			}
		}

		assert(0 == "Middle snake not found");
	}	

	void AddDiffSpan(int aOfs, int bOfs, int xStart, int yStart, int xEnd, int yEnd, bool isReverse = false)
	{	
		if (isReverse)
		{
			// Do diagonal first
			while ((xStart < xEnd) && (yStart < yEnd))
			{			
				//std::cout << aOfs + xStart << " = " << bOfs + yStart << std::endl;
				int aIdx = aOfs + xStart;
				int bIdx = bOfs + yStart;
				EmitEntryEqual(aIdx, bIdx);			
				xStart++;
				yStart++;			
			}
		}

		int xDelta = xEnd - xStart;
		int yDelta = yEnd - yStart;

		if ((xDelta == 0) && (yDelta == 0))
			return;

		int deltaDiff = xDelta - yDelta;	
		if (deltaDiff > 0)
		{			
			for (int i = 0; i < deltaDiff; i++)
			{	
				int aIdx = aOfs + xStart + i;
				EmitEntryDelete(aIdx);				
			}			
		}
		else if (deltaDiff < 0)
		{			
			int aIdx = aOfs + xStart;			
			for (int i = 0; i < -deltaDiff; i++)
			{
				int bIdx = bOfs + yStart + i;
				EmitEntryInsert(aIdx, bIdx);
			}					
		}

		if (!isReverse)
		{		
			// Do diagonal last
			int diagLen = (xEnd - xStart) - BF_MAX(0, deltaDiff);
			for (int sub = diagLen; sub > 0; sub--)
			{
				//std::cout <<  aOfs + xEnd - sub << " = " << bOfs + yEnd - sub << std::endl;
				int aIdx = aOfs + xEnd - sub;
				int bIdx = bOfs + yEnd - sub;
				EmitEntryEqual(aIdx, bIdx);
				//std::cout << aIdx << "=" << bIdx << "\t" << mB[bIdx].mString.c_str();
			}
		}
	}

	void MyersDiff(int aOfs, int aLen, int bOfs, int bLen)
	{	
		//Log("MyersDiff AOfs:%d ALen:%d BOfs:%d BLen:%d\n", aOfs, aLen, bOfs, bLen);

		if ((aLen == 0) || (bLen == 0))
		{
			AddDiffSpan(aOfs, bOfs, 0, 0, aLen, bLen);
			return;
		}

		int xStart = 0;
		int yStart = 0;
		int xEnd = 0;
		int yEnd = 0;
		int totalDiff = 0;
		CalcMiddleSnake(aOfs, aLen, bOfs, bLen, xStart, yStart, xEnd, yEnd, totalDiff);
		if (totalDiff > 1)
		{
			MyersDiff(aOfs, xStart, bOfs, yStart); // Top left
			AddDiffSpan(aOfs, bOfs, xStart, yStart, xEnd, yEnd, (totalDiff % 2) == 0);
			MyersDiff(aOfs + xEnd, aLen - xEnd, bOfs + yEnd, bLen - yEnd); // Bottom right
		}
		else 
		{
			if (totalDiff == 1) // Forward snake
				AddDiffSpan(aOfs, bOfs, 0, 0, xStart, yStart);
			AddDiffSpan(aOfs, bOfs, xStart, yStart, xEnd, yEnd, (totalDiff % 2) == 0);
		}
	}	
};

class CharDiffEngine : public DiffEngine<char, const char*>
{
public:
	BfDiff* mBfDiff;
	int mCmdInsertPos;
	int mAOffset;

	CharDiffEngine(BfDiff* bfDiff, const char* a, const char* b) : DiffEngine(a, b)
	{
		mBfDiff = bfDiff;
		mCmdInsertPos = (int)mBfDiff->mEditCommands.size();
		mAOffset = 0;
	}

	virtual void EmitEntryDelete(int aIdx) override;
	virtual void EmitEntryInsert(int aIdx, int bIdx) override;
};

///
void CharDiffEngine::EmitEntryDelete(int aIdx)
{
	EditCommand editCommand;
	editCommand.mCmd = EditCommandKind_DeleteChar;
	editCommand.mAIdx = aIdx + mAOffset;	
	mCmdInsertPos = mBfDiff->AddEditCommand(mCmdInsertPos, editCommand);
}

void CharDiffEngine::EmitEntryInsert(int aIdx, int bIdx)
{	
	EditCommand editCommand;
	editCommand.mCmd = EditCommandKind_InsertChar;
	editCommand.mAIdx = aIdx + mAOffset;
	editCommand.mBIdx = bIdx;	
	mCmdInsertPos = mBfDiff->AddEditCommand(mCmdInsertPos, editCommand);
}

//

class PatienceDiffEngine : public DiffEngine<LineData, Array<LineData> >
{
public:
	BfDiff* mBfDiff;

public:
	PatienceDiffEngine(BfDiff* bfDiff, Array<LineData>& a, Array<LineData>& b) : DiffEngine(a, b)
	{
		mBfDiff = bfDiff;
	}

	virtual void EmitEntryEqual(int aIdx, int bIdx);
	virtual void EmitEntryDelete(int aIdx);
	virtual void EmitEntryInsert(int aIdx, int bIdx);

	void PatienceDiff(int aOfs, int aLen, int bOfs, int bLen)
	{
		//Log("Patience AOfs:%d ALen:%d BOfs:%d BLen:%d\n", aOfs, aLen, bOfs, bLen);

		assert(aLen >= 0);
		assert(bLen >= 0);

		// Reduce head
		while ((aLen > 0) && (bLen > 0))
		{
			/*if (aOfs == 1559)
			{
				Log("-Head AOfs:%d BOfs:%d APtr:%p BPtr:%p\n", aOfs, bOfs, mA[aOfs].mDuplicateLineData, mB[aOfs].mDuplicateLineData);
			}*/

			if (mA[aOfs] != mB[bOfs])
			{				
				break;		
			}
			//Log("1> ");
			EmitEntryEqual(aOfs, bOfs);
			aOfs++;
			aLen--;
			bOfs++;
			bLen--;		
		}

		if ((aLen == 0) || (bLen == 0))
		{
			AddDiffSpan(aOfs, bOfs, 0, 0, aLen, bLen);
			return;
		}

		int matchTailLength = 0;
		// Reduce tail
		while ((aLen > 0) && (bLen > 0))
		{
			if (mA[aOfs + aLen - 1] != mB[bOfs + bLen - 1])
				break;		
			matchTailLength++;
			aLen--;			
			bLen--;		
		}

		int rangeOfs[2] = {aOfs, bOfs};
		int rangeLens[2] = {aLen, bLen};

		Array<LineMapping> sharedUniqueMapping;

		for (int aIdx = aOfs; aIdx < aOfs + aLen; aIdx++)
		{
			LineData* lineDataA = &mA[aIdx];
			auto duplicateLineData = lineDataA->mDuplicateLineData;

			int lineInstanceCounts[2] = {0, 0};
			int matchIndices[2] = {-1, -1};

			for (int bufIdx = 0; bufIdx < 2; bufIdx++)
			{
				for (int i = 0; i < (int)duplicateLineData->mRefs[bufIdx].size(); i++)
				{
					int lineNum = duplicateLineData->mRefs[bufIdx][i];
					if ((lineNum >= rangeOfs[bufIdx]) && (lineNum < rangeOfs[bufIdx] + rangeLens[bufIdx]))
					{	
						//Log("lineInstanceCounts match %d %d\n", bufIdx, lineNum);

						lineInstanceCounts[bufIdx]++;
						if (lineInstanceCounts[bufIdx] > 1)
							break;
						matchIndices[bufIdx] = lineNum;
					}
				}
			}			

			if ((lineInstanceCounts[0] == 1) && (lineInstanceCounts[1] == 1))
			{			
				// There's a single instance of this line 
				LineMapping lineMapping;
				lineMapping.mAIdx = matchIndices[0];
				lineMapping.mBIdx = matchIndices[1];
				lineMapping.mPrevStackTop = NULL;
				sharedUniqueMapping.push_back(lineMapping);
			}
		}		

		if (sharedUniqueMapping.size() > 0)
		{
			if ((sharedUniqueMapping.size() == 1) && (aLen == 1) && (bLen == 1))
			{
				//Log("2> ");
				EmitEntryEqual(aOfs, bOfs);
				return;
			}

			// Perform "Patience sort"
			//  This generates the longest span possible where the line numbers are ordered within both
			//  the 'A' buffer and the 'B' buffer
			Array<LineMapping*> sortStacks;
			for (int mappingIdx = 0; mappingIdx < (int)sharedUniqueMapping.size(); mappingIdx++)
			{
				LineMapping* lineMapping = &sharedUniqueMapping[mappingIdx];
				int newBIdx = lineMapping->mBIdx;

				bool found = false;
				for (int stackIdx = 0; stackIdx < (int)sortStacks.size(); stackIdx++)
				{
					if (newBIdx < sortStacks[stackIdx]->mBIdx)
					{
						found = true;
						sortStacks[stackIdx] = lineMapping;
						if (stackIdx > 0)
							lineMapping->mPrevStackTop = sortStacks[stackIdx - 1];
						break;
					}
				}

				if (!found)
				{
					if (sortStacks.size() != 0)
						lineMapping->mPrevStackTop = sortStacks.back();
					sortStacks.push_back(lineMapping);
				}
			}

			Array<LineMapping*> sortedLineMapping;
			sortedLineMapping.Resize(sortStacks.size());
			int curSortedIdx = (int)sortedLineMapping.size() - 1;
			LineMapping* checkLineMapping = sortStacks.back();
			while (checkLineMapping != NULL)
			{
				sortedLineMapping[curSortedIdx] = checkLineMapping;
				checkLineMapping = checkLineMapping->mPrevStackTop;
				curSortedIdx--;
			}
			assert(curSortedIdx == -1);			

			int curAIdx = aOfs;
			int curBIdx = bOfs;
			for (int sortedIdx = 0; sortedIdx < (int)sortedLineMapping.size(); sortedIdx++)
			{
				LineMapping* lineMapping = sortedLineMapping[sortedIdx];

				//PatienceDiff(curAIdx, lineMapping->mAIdx - curAIdx + 1, curBIdx, lineMapping->mBIdx - curBIdx + 1);
				PatienceDiff(curAIdx, lineMapping->mAIdx - curAIdx, curBIdx, lineMapping->mBIdx - curBIdx);
				//Log("3> ");
				EmitEntryEqual(lineMapping->mAIdx, lineMapping->mBIdx);

				curAIdx = lineMapping->mAIdx + 1;
				curBIdx = lineMapping->mBIdx + 1;
			}

			PatienceDiff(curAIdx, aOfs + aLen - curAIdx, curBIdx, bOfs + bLen - curBIdx);
		}
		else
		{
			MyersDiff(aOfs, aLen, bOfs, bLen);
		}

		//Log("Patience EqSpan AOfs:%d BOfs:%d %d\n", aOfs + aLen, bOfs + bLen, matchTailLength);
		for (int tailIdx = 0; tailIdx < matchTailLength; tailIdx++)	
		{
			//Log("4> ");
			EmitEntryEqual(aOfs + aLen + tailIdx, bOfs + bLen + tailIdx);	
		}
	}
};

void PatienceDiffEngine::EmitEntryEqual(int aIdx, int bIdx)
{
	LineData* aLineData = &mA[aIdx];
	LineData* bLineData = &mB[bIdx];

	bool matches = aLineData->mBufLength == bLineData->mBufLength;
	if (matches)
	{
		for (int i = 0; i < aLineData->mBufLength; i++)
		{
			if (mBfDiff->mAText[aLineData->mBufStart + i] != mBfDiff->mBText[bLineData->mBufStart + i])
			{
				matches = false;
				break;
			}
		}
	}

	//Log("Patience EmitEntryEqual A:%d B:%d Matches:%d\n", aIdx, bIdx, matches);
	/*if (!matches)
	{
		Log(" ALen:%d BLen: %d\n", aLineData->mBufLength, bLineData->mBufLength);
	}*/

	if (!matches)
	{
		CharDiffEngine strDiffEngine(mBfDiff, mBfDiff->mAText, mBfDiff->mBText);				
		strDiffEngine.MyersDiff(aLineData->mBufStart, aLineData->mBufLength, bLineData->mBufStart, bLineData->mBufLength);
		//SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}

	/*std::cout << aIdx << "=" << bIdx << "\t" << mB[bIdx].mString.c_str();
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE);*/
}

void PatienceDiffEngine::EmitEntryDelete(int aIdx)
{
	/*SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
	std::cout << "x" << aIdx << " \t" << mA[aIdx].mString.c_str();				
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE);*/

	EditCommand editCommand;
	editCommand.mCmd = EditCommandKind_DeleteLine;
	editCommand.mAIdx = aIdx;	
	mBfDiff->AddEditCommand(-1, editCommand);
	//mA[aIdx].mDuplicateLineData->mDeleteEditCommandIndices.push_back((int)mBfDiff->mEditCommands.size() - 1);
}

void PatienceDiffEngine::EmitEntryInsert(int aIdx, int bIdx)
{
	/*SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN);				
	std::cout << aIdx << "i" << bIdx << "\t" << mB[bIdx].mString.c_str();
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE);*/

	EditCommand editCommand;
	editCommand.mCmd = EditCommandKind_InsertLine;
	editCommand.mAIdx = aIdx;
	editCommand.mBIdx = bIdx;
	mBfDiff->AddEditCommand(-1, editCommand);
}

// The line hash is used to determine whether a line appears only once in a given file.
//  A hash collision has a potential to generate a slightly less ideal diff, but still
//  a valid diff.  Since we hash out the whitespace, we already handle the case where
//  that have whitespace difference need to be modified to be exact matches, that code
//  will also transform a compltely wrong hash collision string into a correct string 
//  as well
#define MIXIN_CHAR_HASH(c) hash = ((hash ^ c) << 5) - hash
int64 GetLineHash(const char* str, int strStart, int strLen)
{
	//String outStr;

	bool hadWS = false;
	bool hadNonWS = false;

	int64 hash = 0;

	for (int i = strStart; i <= strStart + strLen; i++)
	{
		bool remove = false;
		char c = str[i];
		bool isWS = iswspace((unsigned char)c) != 0;
		if (!isWS)
		{
			if ((hadWS) && (hadNonWS))
			{
				//MIXIN_CHAR_HASH(' ');
				//outStr += ' ';				
			}
			MIXIN_CHAR_HASH(c);
			//outStr += c;
			hadNonWS = true;
			hadWS = false;
		}
		else
		{
			hadWS = true;
		}
	}
	return hash;
}

void BfDiff::ProcessText(const char* bufferData, Array<LineData>& lines, int fileNum)
{	
	//Log("BfDiff::ProcessText %d\n", fileNum);

	int textHash = 0;

	int lineNum = 0;
	int lineStart = 0;
	for (int i = 0; true; i++)
	{
		char c = bufferData[i];
		textHash = ((textHash ^ c) << 5) - textHash;
		if (c == 0)
			break;
		int lineEnd = -1;
		
		if (c == '\r')
			continue;
		if (c == '\n')
			lineEnd = i;
		else if (bufferData[i + 1] == '\0')
			lineEnd = i;

		if (lineEnd != -1)
		{
			int lineEnd = i;
			//if ((lineEnd > 0) && (bufferData[lineEnd - 1] == '\r'))
			//lineEnd--;

			//String strippedStr = StripWhitespace(line);
			int64 lineHash = GetLineHash(bufferData, lineStart, lineEnd - lineStart);
			
			//Log(" Line:%d Start: %d Len: %d Hash: %d\n", lineNum, lineStart, lineEnd - lineStart, lineHash);

			/*auto insertPair = gDuplicateLineDataMap.insert(DuplicateLineDataMap::value_type(lineHash, DuplicateLineData()));
			DuplicateLineData* duplicateLineData = &insertPair.first->second;*/

			DuplicateLineData* duplicateLineData = NULL;
			DuplicateLineData** duplicateLineDataPtr = NULL;
			if (gDuplicateLineDataMap.TryAdd(lineHash, NULL, &duplicateLineDataPtr))
			{
				duplicateLineData = new	DuplicateLineData();
				*duplicateLineDataPtr = duplicateLineData;
			}
			else
				duplicateLineData = *duplicateLineDataPtr;
			duplicateLineData->mRefs[fileNum].push_back(lineNum);

			lineNum++;

			LineData lineData;
			//lineData.mString = String(bufferData + lineStart, bufferData + lineEnd + 1);
			lineData.mBufStart = lineStart;
			lineData.mBufLength = lineEnd - lineStart + 1;
			lineData.mDuplicateLineData = duplicateLineData;			
			lines.push_back(lineData);

			lineStart = i + 1;
		}
	}

	//Log("Text Hash: %d\n", textHash);
}

void BfDiff::GenerateMoveCommands()
{	
#if 0
	for (int editCmdIdx = 0; editCmdIdx < (int)mEditCommands.size(); editCmdIdx++)
	{
		EditCommand* editCommand = &mEditCommands[editCmdIdx];
		if (editCommand->mCmd == EditCommandKind_InsertLine)
		{
			LineData* bLineData = &mBLineDatas[editCommand->mBIdx];
			if (bLineData->mDuplicateLineData->mDeleteEditCommandIndices.size() > 0)
			{
				int deleteCmdIdx = bLineData->mDuplicateLineData->mDeleteEditCommandIndices[0];
				bLineData->mDuplicateLineData->mDeleteEditCommandIndices.erase(bLineData->mDuplicateLineData->mDeleteEditCommandIndices.begin());

				// Delete followed by insert
				if (deleteCmdIdx < editCmdIdx)
				{
					EditCommand* deleteCommand = &mEditCommands[deleteCmdIdx];

					// With 'MoveLine', mAIdx is the src within A, and mBIdx is the dest within A
					deleteCommand->mCmd = EditCommandKind_MoveLineFrom;										

					editCommand->mCmd = EditCommandKind_MoveLineTo;
					editCommand->mAltParam = deleteCmdIdx;
				}
				else // Insert followed by a delete
				{
					EditCommand* deleteCommand = &mEditCommands[deleteCmdIdx];

					LineData* aLineData = &mALineDatas[deleteCommand->mAIdx];
					LineData* aDestLineData = &mALineDatas[editCommand->mAIdx];
					LineData* bLineData = &mBLineDatas[editCommand->mBIdx];

					editCommand->mCmd = EditCommandKind_MoveLine;					
					editCommand->mAltParam = deleteCommand->mAIdx;

					deleteCommand->mCmd = EditCommandKind_None;

					//TODO: FIgure out how to insert this.  These edits cause our deleteEditCommandIndices to get borked
					/*CharDiffEngine strDiffEngine(mAText, mBText);
					strDiffEngine.mCmdInsertPos = editCmdIdx + 1;
					strDiffEngine.mAOffset = aDestLineData->mBufStart - aLineData->mBufStart;
					strDiffEngine.MyersDiff(aLineData->mBufStart, aLineData->mBufLength, bLineData->mBufStart, bLineData->mBufLength);*/
				}
			}
		}
	}
#endif
}

int BfDiff::GetInsertPosition(int lineNum)
{
	if (lineNum < (int)mALineDatas.size())
		return mALineDatas[lineNum].mBufStart;
	if (lineNum == 0)
		return 0;
	return mALineDatas.back().mBufStart + mALineDatas.back().mBufLength;
}

#if 0
void ApplyEditCommands()
{	
	outA = mAText;
	int aOffset = 0;	

	Array<int> deferredFromSources;

	for (int i = 0; i < (int)outA.size(); i++)
	{
		outA[i] = toupper(outA[i]);
	}

	for (int editCmdIdx = 0; editCmdIdx < (int)editCommands.size(); editCmdIdx++)
	{
		EditCommand* editCommand = &editCommands[editCmdIdx];
		if (editCommand->mAIdx == -1)
			continue;

		if (editCommand->mCmd == EditCommandKind_DeleteLine)
		{
			// Delete line
			//outA.erase(outA.begin() + (editCommand->mAIdx + aOffset));
			LineData* lineData = &mALineDatas[editCommand->mAIdx];
			outA.erase(outA.begin() + (lineData->mBufStart + aOffset), outA.begin() + (lineData->mBufStart + lineData->mBufLength + aOffset));
			aOffset -= lineData->mBufLength;			
		}
		else if (editCommand->mCmd == EditCommandKind_DeleteChar)
		{			
			outA.erase(
				outA.begin() + (editCommand->mAIdx + aOffset), 
				outA.begin() + (editCommand->mAIdx + 1 + aOffset));
			aOffset--;
		}
		else if (editCommand->mCmd == EditCommandKind_InsertChar)
		{
			outA.insert(outA.begin() + (editCommand->mAIdx + aOffset), 
				mBText.begin() + editCommand->mBIdx, 
				mBText.begin() + editCommand->mBIdx + 1);
			aOffset++;
		}
		else if (editCommand->mCmd == EditCommandKind_InsertLine)
		{
			// Insert line
			int insertPosition = GetInsertPosition(editCommand->mAIdx);
			LineData* bLineData = &mBLineDatas[editCommand->mBIdx];			

			outA.insert(outA.begin() + (insertPosition + aOffset), 
				mBText.begin() + bLineData->mBufStart, mBText.begin() + bLineData->mBufStart + bLineData->mBufLength);
			aOffset += bLineData->mBufLength;			
		}
		else if (editCommand->mCmd == EditCommandKind_MoveLineFrom)
		{
			LineData* aLineDataSrc = &mALineDatas[editCommand->mAIdx];
			editCommand->mAltParam = aLineDataSrc->mBufStart + aOffset;
			deferredFromSources.push_back(editCmdIdx);
			//editC

			// Move line			
			/*LineData* aLineDataSrc = &mALineDatas[editCommand->mAIdx];			
			//LineData* aLineDataDest = &mALineDatas[editCommand->mBIdx];			
			int aOrigDestBufStart = GetInsertPosition(editCommand->mBIdx);	
			int aDestBufStart = RemapForwardIndex(aOrigDestBufStart);
			outA.insert(outA.begin() + (aDestBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aOffset) + aLineDataSrc->mBufLength);

			outA.erase(
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aLineDataSrc->mBufLength + aOffset));						

			deferredOffsets.insert(IntIntMultimap::value_type(aOrigDestBufStart, aLineDataSrc->mBufLength));
			aOffset -= aLineDataSrc->mBufLength;		*/	
		}
		else if (editCommand->mCmd == EditCommandKind_MoveLineTo)
		{
			EditCommand* moveLineFromCmd = &editCommands[editCommand->mAltParam];

			int insertPosition = GetInsertPosition(editCommand->mAIdx);
			LineData* aLineDataSrc = &mALineDatas[moveLineFromCmd->mAIdx];

			int aSrcAddr = moveLineFromCmd->mAltParam;

			outA.insert(outA.begin() + (insertPosition + aOffset), 
				outA.begin() + aSrcAddr, outA.begin() + aSrcAddr + aLineDataSrc->mBufLength);

			outA.erase(
				outA.begin() + (aSrcAddr), 
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength));

			for (int deferredIdx = 0; deferredIdx < (int)deferredFromSources.size(); deferredIdx++)
			{
				int deferredCmdIdx = deferredFromSources[deferredIdx];
				if (deferredCmdIdx == editCommand->mAltParam)
				{
					deferredFromSources.erase(deferredFromSources.begin() + deferredIdx);
					deferredIdx--;
				}
				else				
				{
					EditCommand* deferredCmd = &editCommands[deferredCmdIdx];
					// Did we erase a block that comes before another deferred move block?  If so, modify addr
					if (aSrcAddr < deferredCmd->mAltParam)
						deferredCmd->mAltParam -= aLineDataSrc->mBufLength;
				}				
			}			

			//editCommand->mAltParam = aLineDataSrc->mBufStart + aOffset;

			// Move line			
			/*LineData* aLineDataDest = &mALineDatas[editCommand->mAIdx];
			LineData* aLineDataSrc = &mALineDatas[editCommand->mBIdx];

			outA.erase(
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aLineDataSrc->mBufLength + aOffset));

			outA.insert(outA.begin() + (aLineDataDest->mBufStart + aOffset), 
			mAText.begin() + aLineDataSrc->mBufStart, mAText.begin() + aLineDataSrc->mBufStart + aLineDataSrc->mBufLength);*/
			//aOffset += bLineData->mBufLength;			

		}
		else if (editCommand->mCmd == EditCommandKind_MoveLine)
		{
			int insertPosition = GetInsertPosition(editCommand->mAIdx);
			LineData* aLineDataSrc = &mALineDatas[editCommand->mAltParam];			

			int aSrcAddr = aLineDataSrc->mBufStart + aOffset;

			outA.insert(outA.begin() + (insertPosition + aOffset), 
				outA.begin() + aSrcAddr, 
				outA.begin() + aSrcAddr + aLineDataSrc->mBufLength);

			outA.erase(
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength), 
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength * 2));
		}
	}

	std::cout << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	/*for (auto& lineData : outA)
	{
	std::cout << lineData.mString.c_str() << std::endl;		
	}*/

	std::cout << outA.c_str() << std::endl;
	//
}
#endif

void BfDiff::Compare(const char* text1, const char* text2)
{
	mAText = text1;
	mBText = text2;

	ProcessText(mAText, mALineDatas, 0);
	ProcessText(mBText, mBLineDatas, 1);

	PatienceDiffEngine diffEngine(this, mALineDatas, mBLineDatas);
	diffEngine.PatienceDiff(0, (int)mALineDatas.size(), 0, (int)mBLineDatas.size());

	//GenerateMoveCommands();
}

/////////

BF_EXPORT const char* BF_CALLTYPE BfDiff_DiffText(const char* text1, const char* text2)
{
	gDuplicateLineDataMap.Clear();

	BfDiff bfDiff;
	bfDiff.Compare(text1, text2);
		
	int aOffset = 0;	

	Array<int> deferredFromSources;

	String& outString = *gTLStrReturn.Get();
	outString.clear();
	for (int editCmdIdx = 0; editCmdIdx < (int)bfDiff.mEditCommands.size(); editCmdIdx++)
	{
		EditCommand* editCommand = &bfDiff.mEditCommands[editCmdIdx];
		if (editCommand->mAIdx == -1)
			continue;

		if (editCommand->mCmd == EditCommandKind_DeleteLine)
		{
			// Delete line
			//outA.erase(outA.begin() + (editCommand->mAIdx + aOffset));
			LineData* lineDataStart = &bfDiff.mALineDatas[editCommand->mAIdx];
			LineData* lineDataEnd = &bfDiff.mALineDatas[editCommand->mAIdx + editCommand->mCount - 1];
			//outA.erase(outA.begin() + (lineData->mBufStart + aOffset), outA.begin() + (lineData->mBufStart + lineData->mBufLength + aOffset));
			int len = lineDataEnd->mBufStart + lineDataEnd->mBufLength - lineDataStart->mBufStart;

			outString += StrFormat("- %d %d %d\n", lineDataStart->mBufStart + aOffset, len, editCommand->mAIdx);
			aOffset -= len;
		}
		else if (editCommand->mCmd == EditCommandKind_DeleteChar)
		{			
			/*outA.erase(
				outA.begin() + (editCommand->mAIdx + aOffset), 
				outA.begin() + (editCommand->mAIdx + 1 + aOffset));*/
			outString += StrFormat("- %d %d\n", editCommand->mAIdx + aOffset, editCommand->mCount);
			aOffset -= editCommand->mCount;
		}
		else if (editCommand->mCmd == EditCommandKind_InsertChar)
		{
			/*outA.insert(outA.begin() + (editCommand->mAIdx + aOffset), 
				mBText.begin() + editCommand->mBIdx, 
				mBText.begin() + editCommand->mBIdx + 1);*/
			outString += StrFormat("+ %d %d %d\n", editCommand->mAIdx + aOffset, editCommand->mBIdx, editCommand->mCount);
			aOffset += editCommand->mCount;
		}
		else if (editCommand->mCmd == EditCommandKind_InsertLine)
		{
			// Insert line
			int insertPosition = bfDiff.GetInsertPosition(editCommand->mAIdx);
			LineData* bLineDataStart = &bfDiff.mBLineDatas[editCommand->mBIdx];
			LineData* bLineDataEnd = &bfDiff.mBLineDatas[editCommand->mBIdx + editCommand->mCount - 1];
			int len = bLineDataEnd->mBufStart + bLineDataEnd->mBufLength - bLineDataStart->mBufStart;

			/*outA.insert(outA.begin() + (insertPosition + aOffset), 
				mBText.begin() + bLineData->mBufStart, mBText.begin() + bLineData->mBufStart + bLineData->mBufLength);*/
			outString += StrFormat("+ %d %d %d %d\n", insertPosition + aOffset, bLineDataStart->mBufStart, len, editCommand->mAIdx);
			aOffset += len;			
		}
		else if (editCommand->mCmd == EditCommandKind_MoveLineFrom)
		{
			BF_FATAL("Not implemented");

			/*LineData* aLineDataSrc = &mALineDatas[editCommand->mAIdx];
			editCommand->mAltParam = aLineDataSrc->mBufStart + aOffset;
			deferredFromSources.push_back(editCmdIdx);*/
			//editC

			// Move line			
			/*LineData* aLineDataSrc = &mALineDatas[editCommand->mAIdx];			
			//LineData* aLineDataDest = &mALineDatas[editCommand->mBIdx];			
			int aOrigDestBufStart = GetInsertPosition(editCommand->mBIdx);	
			int aDestBufStart = RemapForwardIndex(aOrigDestBufStart);
			outA.insert(outA.begin() + (aDestBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aOffset) + aLineDataSrc->mBufLength);

			outA.erase(
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aLineDataSrc->mBufLength + aOffset));						

			deferredOffsets.insert(IntIntMultimap::value_type(aOrigDestBufStart, aLineDataSrc->mBufLength));
			aOffset -= aLineDataSrc->mBufLength;		*/	
		}
		else if (editCommand->mCmd == EditCommandKind_MoveLineTo)
		{
			BF_FATAL("Not implemented");

			/*EditCommand* moveLineFromCmd = &editCommands[editCommand->mAltParam];

			int insertPosition = bfDiff.GetInsertPosition(editCommand->mAIdx);
			LineData* aLineDataSrc = &bfDiff.mALineDatas[moveLineFromCmd->mAIdx];

			int aSrcAddr = moveLineFromCmd->mAltParam;

			outA.insert(outA.begin() + (insertPosition + aOffset), 
				outA.begin() + aSrcAddr, outA.begin() + aSrcAddr + aLineDataSrc->mBufLength);

			outA.erase(
				outA.begin() + (aSrcAddr), 
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength));

			for (int deferredIdx = 0; deferredIdx < (int)deferredFromSources.size(); deferredIdx++)
			{
				int deferredCmdIdx = deferredFromSources[deferredIdx];
				if (deferredCmdIdx == editCommand->mAltParam)
				{
					deferredFromSources.erase(deferredFromSources.begin() + deferredIdx);
					deferredIdx--;
				}
				else				
				{
					EditCommand* deferredCmd = &editCommands[deferredCmdIdx];
					// Did we erase a block that comes before another deferred move block?  If so, modify addr
					if (aSrcAddr < deferredCmd->mAltParam)
						deferredCmd->mAltParam -= aLineDataSrc->mBufLength;
				}				
			}			*/

			//editCommand->mAltParam = aLineDataSrc->mBufStart + aOffset;

			// Move line			
			/*LineData* aLineDataDest = &mALineDatas[editCommand->mAIdx];
			LineData* aLineDataSrc = &mALineDatas[editCommand->mBIdx];

			outA.erase(
			outA.begin() + (aLineDataSrc->mBufStart + aOffset), 
			outA.begin() + (aLineDataSrc->mBufStart + aLineDataSrc->mBufLength + aOffset));

			outA.insert(outA.begin() + (aLineDataDest->mBufStart + aOffset), 
			mAText.begin() + aLineDataSrc->mBufStart, mAText.begin() + aLineDataSrc->mBufStart + aLineDataSrc->mBufLength);*/
			//aOffset += bLineData->mBufLength;			

		}
		else if (editCommand->mCmd == EditCommandKind_MoveLine)
		{
			BF_FATAL("Not implemented");

			/*int insertPosition = GetInsertPosition(editCommand->mAIdx);
			LineData* aLineDataSrc = &mALineDatas[editCommand->mAltParam];			

			int aSrcAddr = aLineDataSrc->mBufStart + aOffset;

			outA.insert(outA.begin() + (insertPosition + aOffset), 
				outA.begin() + aSrcAddr, 
				outA.begin() + aSrcAddr + aLineDataSrc->mBufLength);

			outA.erase(
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength), 
				outA.begin() + (aSrcAddr + aLineDataSrc->mBufLength * 2));*/
		}
	}

	for (auto& kv : gDuplicateLineDataMap)
		delete kv.mValue;

	//LogClose();
	gDuplicateLineDataMap.Clear();	

	return outString.c_str();
}