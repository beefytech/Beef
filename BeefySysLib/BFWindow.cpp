#include "BFWindow.h"
#include "gfx/RenderDevice.h"

USING_NS_BF;

BFMenu::BFMenu()
{
	mParent = NULL;
	mKeyCode = 0;
	mKeyCtrl = 0;
	mKeyAlt = 0;
	mKeyShift = 0;	
}

struct BFNamedVirtKey
{
public:
	const char* mName;
	int mKeyCode;
};

#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_SELECT         0x29
#define VK_PRINT          0x2A
#define VK_EXECUTE        0x2B
#define VK_SNAPSHOT       0x2C
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_HELP           0x2F
#define VK_F1             0x70
#define VK_F2             0x71
#define VK_F3             0x72
#define VK_F4             0x73
#define VK_F5             0x74
#define VK_F6             0x75
#define VK_F7             0x76
#define VK_F8             0x77
#define VK_F9             0x78
#define VK_F10            0x79
#define VK_F11            0x7A
#define VK_F12            0x7B
#define VK_SEMICOLON	  0xBA
#define VK_EQUALS         0xBB
#define VK_COMMA          0xBC
#define VK_MINUS          0xBD
#define VK_PERIOD         0xBE
#define VK_SLASH          0xBF
#define VK_GRAVE          0xC0
#define VK_LBRACKET       0xDB
#define VK_BACKSLASH      0xDC
#define VK_RBRACKET       0xDD
#define VK_APOSTROPHE     0xDE
#define VK_BACKTICK       0xDF
#define VK_PAUSE		  0x13
#define VK_CANCEL		  0x03

static BFNamedVirtKey gNamedKeys[] = 
{{"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6},
{"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12}, 
{"INSERT", VK_INSERT}, {"INSERT", VK_INSERT}, {"DE", VK_DELETE}, {"DELETE", VK_DELETE},
{"BREAK", VK_PAUSE},
{"HOME", VK_HOME}, {"END", VK_END}, {"PGUP", VK_PRIOR}, {"PAGEUP", VK_PRIOR}, {"PGDN", VK_NEXT}, {"PAGEDN", VK_NEXT}, {"PAGEDOWN", VK_NEXT},
{"UP", VK_UP}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"DOWN", VK_DOWN}
};

bool BFMenu::ParseHotKey(const StringImpl& hotKey)
{
	String aHotKey = ToUpper(hotKey);
	if (aHotKey.StartsWith("#"))
		return false;
	
	while (true)
	{
		int idx = (int)aHotKey.IndexOf('+');
		if (idx == -1)
			idx = (int)aHotKey.IndexOf('-');
		if (idx == -1)
			break;

		String aModifier = Trim(aHotKey.Substring(0, idx));
		aHotKey = Trim(aHotKey.Substring(idx + 1));

		if (aModifier == "SHIFT")
			mKeyShift = true;
		else if (aModifier == "CTRL")
			mKeyCtrl = true;
		else if (aModifier == "ALT")
			mKeyAlt = true;
		else
		{
			BF_FATAL("Unknown hotkey modifier");
			return false;
		}
	}

	if (aHotKey.length() == 1)
	{
		if (aHotKey == ",")
			mKeyCode = /*VK_COMMA*/0xBC;
		else
			mKeyCode = (int) aHotKey[0];
		return true;
	}

	int count = sizeof(gNamedKeys) / sizeof(BFNamedVirtKey);
	for (int i = 0; i < count; i++)
	{
		if (aHotKey == gNamedKeys[i].mName)
		{
			mKeyCode = gNamedKeys[i].mKeyCode;
			if ((mKeyCode == VK_PAUSE) && (mKeyCtrl))
			{
				// Ctrl-Pause is really "Cancel"
				mKeyCode = VK_CANCEL;				
			}
			return true;
		}
	}	

	BF_FATAL("Unknown hotkey key name");
	return false;
}

///

BFWindow::BFWindow()
{
	mParent = NULL;
	mMenu = NULL;
	mRenderWindow = NULL;
	mNonExclusiveMouseCapture = false;

	mParent = NULL;
	mMovedFunc = NULL;
	mCloseQueryFunc = NULL;
	mClosedFunc = NULL;
	mGotFocusFunc = NULL;
	mLostFocusFunc = NULL;
	mKeyCharFunc = NULL;
	mKeyDownFunc = NULL;
	mKeyUpFunc = NULL;
	mMouseMoveFunc = NULL;
	mMouseProxyMoveFunc = NULL;
	mMouseDownFunc = NULL;
	mMouseUpFunc = NULL;
	mMouseWheelFunc = NULL;
	mMouseLeaveFunc = NULL;
	mMenuItemSelectedFunc = NULL;
	mDragDropFileFunc = NULL;
	mHitTestFunc = NULL;
	mFlags = 0;

	for (int i = 0; i < KEYCODE_MAX; i++)
		mIsKeyDown[i] = false;
	for (int i = 0; i < MOUSEBUTTON_MAX; i++)
	{
		mIsMouseDown[i] = false;
		mMouseClickCount[i] = 0;
		mMouseDownTicks[i] = 0;
		mMouseDownCoords[i] = { -1, -1 };
	}
}

BFWindow::~BFWindow()
{
	delete mRenderWindow;
	delete mMenu;
}
