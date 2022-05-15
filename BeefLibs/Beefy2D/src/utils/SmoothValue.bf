using System;
using System.Collections;
using System.Text;

namespace Beefy.utils
{
    public class SmoothValue
    {
        public double mSrc;
        public double mDest;
        public float mPct;
        public float mSpeed = 1.0f;
        public float mSpeedScale = 1.0f;

        public double v
        {
            get
            {
                if (mPct == 1.0f)
                    return mDest;
                return mSrc + (mDest - mSrc) * EasedPct;
            }
        }

        public double EasedPct
        {
            get
            {
                if (mPct == 1.0f)
                    return 1.0f;
                return Utils.EaseInAndOut(mPct);
            }
        }

        public bool IsMoving
        {
            get { return mPct != 1.0f; }
        }

        public void Update(float updatePct = 1.0f)
        {
            mPct = Math.Min(1.0f, mPct + mSpeed * mSpeedScale * updatePct);
        }

        public void Set(double val, bool immediate = false)
        {
            if ((!immediate) && (val == mDest))
                return;

            if ((mPct != 1.0f) && (mPct != 0.0f))
            {                
                double cur = v;

                if (mPct > 0.80f)
                    mPct = 0.80f;

                mDest = val;
                mSrc = -(cur - mDest * EasedPct) / (EasedPct - 1);
                mSpeedScale = (1.0f - mPct);
            }
            else
            {
                mSrc = v;
                mPct = 0.0f;
                mSpeedScale = 1.0f;
                mDest = val;
            }


            if (immediate)
                mPct = 1.0f;
        }
    }
}
