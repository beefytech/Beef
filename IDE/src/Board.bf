#if false

using System;
using System.Collections;
using System.Linq;
using System.Text;
using System.Diagnostics;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;

namespace IDE
{    
    public class Board : Widget
    {
        Image mImage;
        Image mSegment;
        Font mFont;        

        float mMouseX;
        float mMouseY;

        ButtonWidget mButton;

        EditWidget mEditWidget;

        public Board()
        {
            mImage = Image.LoadFromFile(BFApp.sApp.mInstallDir + "images/tgaTest.tga");
            mSegment = mImage.CreateImageSegment(20, 20, 40, 40);

            mFont = Font.LoadFromFile(BFApp.sApp.mInstallDir + "fonts/SegoeUI8.fnt");

            mButton = ThemeFactory.mDefault.CreateButton(this, "Test", 50, 50, 200, 30);
            ThemeFactory.mDefault.CreateCheckbox(this, 50, 100, 20, 20);

            /*mEditWidget = ThemeFactory.mDefault.CreateEditWidget(this, 50, 90, 200, 30);
            mEditWidget.SetText("Hi");*/

            mEditWidget = ThemeFactory.mDefault.CreateEditWidget(this, 50, 90, 500, 480);
            mEditWidget.InitScrollbars(true, true);
            mEditWidget.Content.mIsMultiline = true;
            mEditWidget.Content.mWordWrap = false;
            mEditWidget.SetText("Hi");            

            /*mEditWidget = ThemeFactory.mDefault.CreateEditWidget(this, 50, 90, 200, 20);            
            mEditWidget.Content.mMultiline = false;
            mEditWidget.Content.mWordWrap = false;
            mEditWidget.SetText("Hi");*/
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);
            mMouseX = x;
            mMouseY = y;
        }

        public override void MouseLeave()
        {
            base.MouseLeave();
            mMouseX = -100;
            mMouseY = -100;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            g.SetFont(mFont);

            using (g.PushColor(ThemeColors.Theme.Background.Color))
                g.FillRect(0, 0, mWidth, mHeight);

            using (g.PushColor(Color.Black))
                g.FillRect(0, 60, 500, 60);

            //g.mColor = 0xFF000000;
            g.mColor = 0xFFFFFFFF;
            //g.mColor = 0xD080F080;
                        
            //g.Draw(mSegment, 0, 0);

            Matrix matrix = Matrix.IdentityMatrix;
            matrix.Rotate(0.2f);

            
            using (g.PushColor(Color.Black))
                g.DrawString(String.Format("{0} This is a test of the font system, pretty cool!", mUpdateCnt), 30, 300);
                        
            g.mFont = mFont;
            using (g.PushMatrix(ref matrix))
                g.DrawString("This is a test of the font system, pretty cool!\nI think!", 30, 62);

            using (g.PushColor(0xFFFF0000))
                g.FillRect(mMouseX - 1, mMouseY - 1, 3, 3);

            g.FillRectGradient(20, 20, 40, 40, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF);

            //g.Draw(mTexture, 20 * (mCount % 3), 20);

            //++mCount;            
        }
    }
}

#endif