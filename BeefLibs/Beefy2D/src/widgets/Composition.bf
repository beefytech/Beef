using System;
using System.Collections;
using System.Text;
using System.Reflection;
using System.Diagnostics;
using Beefy.utils;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy;

namespace Beefy.widgets
{
    public class TimelineData
    {
        /*void SetValueObject(float frame, object value, out object oldValue, out bool isNewKeyframe)
        {
        }*/
    }

    public class TimelineData<T> : TimelineData
    {
        public delegate T Interpolator(T from, T to, float pct);

        public struct Entry : IOpComparable
        {
            public float mFrame;
            public T mValue;

			public static bool operator==(Entry val1, Entry val2)
			{
				return ((val1.mFrame == val2.mFrame) && (val1.mValue == val2.mValue));
			}

			public static int operator<=>(Entry lhs, Entry rhs)
			{
				return lhs.mFrame.CompareTo(rhs.mFrame);
			}
        }

        public List<Entry> mEntries;
        public Interpolator mInterpolator;
        public T mConstantValue;        

        public void SetValue(float frame, T value, out T oldValue, out bool isNewKeyframe)
        {
            oldValue = mConstantValue;

            if (mEntries == null)
                mEntries = new List<Entry>();

            if (mEntries.Count == 0)            
                mConstantValue = value;            

            Entry entry;
            entry.mFrame = frame;
            entry.mValue = value;

            int index = mEntries.BinarySearch(entry);
            if (index >= 0)
            {
                isNewKeyframe = false;
                oldValue = mEntries[index].mValue;
                mEntries[index] = entry;
                return;
            }

            isNewKeyframe = true;   

            index = ~index;
            mEntries.Insert(index, entry);
        }

        public void DeleteKeyframe(float frame)
        {
            Entry entry;
            entry.mFrame = frame;
            entry.mValue = default(T);

            int index = mEntries.BinarySearch(entry);
            mEntries.RemoveAt(index);
        }

        public void SetValue(float frame, T value)
        {
            T oldValue;
            bool isNewKeyframe;
            SetValue(frame, value, out oldValue, out isNewKeyframe);
        }

        public this(Interpolator interpolator = null)
        {
            mInterpolator = interpolator;
        }

		public ~this()
		{
			delete mInterpolator;
		}

        public T GetValue(float frame)
        {
            if ((mEntries == null) || (mEntries.Count == 0))
                return mConstantValue;
            
            Entry find;
            find.mValue = default(T);
            find.mFrame = frame;
            int index = mEntries.BinarySearch(find);
            if (index >= 0)
                return mEntries[index].mValue;

            index = ~index - 1;
            if (index >= mEntries.Count - 1)
                return mEntries[mEntries.Count - 1].mValue;
            if (index < 0)
                return mEntries[0].mValue;
            Entry entry1 = mEntries[index];
            Entry entry2 = mEntries[index + 1];

            float aPct = (frame - entry1.mFrame) / (entry2.mFrame - entry1.mFrame);
            return mInterpolator(entry1.mValue, entry2.mValue, aPct);
        }
    }

    public struct CompositionPos : IEquatable<CompositionPos>
    {
        public float mX;
        public float mY;

        public this(float x=0, float y=0)
        {
            mX = x;
            mY = y;
        }

		public bool Equals(CompositionPos other)
		{
			return (mX == other.mX) && (mY == other.mY);
		}

		public static bool operator==(CompositionPos val1, CompositionPos val2)
		{
			return (val1.mX == val2.mX) && (val1.mY == val2.mY);
		}

        static public CompositionPos Interpolator(CompositionPos from, CompositionPos to, float pct)
        {
            CompositionPos aPos;
            aPos.mX = from.mX + (to.mX - from.mX) * pct;
            aPos.mY = from.mY + (to.mY - from.mY) * pct;
            return aPos;
        }

        static public void WriteX(StructuredData data, CompositionPos pos)
        {
            data.Add(pos.mX);
        }

        static public void WriteY(StructuredData data, CompositionPos pos)
        {
            data.Add(pos.mY);
        }

        static public void ReadX(StructuredData data, int32 idx, ref CompositionPos pos)
        {
            pos.mX = data.GetCurFloat(idx);
        }

        static public void ReadY(StructuredData data, int32 idx, ref CompositionPos pos)
        {
            pos.mY = data.GetCurFloat(idx);
        }
    }

    public struct CompositionScale : IEquatable<CompositionScale>
    {
        public float mScaleX;
        public float mScaleYAspect; // ScaleY = ScaleX * mScaleYAspect

        public this(float scaleX = 1, float scaleYAspect = 1)
        {
            mScaleX = scaleX;
            mScaleYAspect = scaleYAspect;
        }

		public static bool operator==(CompositionScale val1, CompositionScale val2)
		{
			return (val1.mScaleX == val2.mScaleX) && (val1.mScaleYAspect == val2.mScaleYAspect);
		}

		public bool Equals(CompositionScale other)
		{
			return (mScaleX == other.mScaleX) && (mScaleYAspect == other.mScaleYAspect);
		}

        static public CompositionScale Interpolator(CompositionScale from, CompositionScale to, float pct)
        {
            CompositionScale aScale;
            aScale.mScaleX = from.mScaleX + (to.mScaleX - from.mScaleX) * pct;
            aScale.mScaleYAspect = from.mScaleYAspect + (to.mScaleYAspect - from.mScaleYAspect) * pct;
            return aScale;
        }

        static public void WriteX(StructuredData data, CompositionScale scale)
        {
            data.Add(scale.mScaleX);
        }

        static public void WriteY(StructuredData data, CompositionScale scale)
        {
            data.Add(scale.mScaleYAspect);
        }

        static public void ReadX(StructuredData data, int32 idx, ref CompositionScale scale)
        {
            scale.mScaleX = data.GetCurFloat(idx);
        }

        static public void ReadY(StructuredData data, int32 idx, ref CompositionScale scale)
        {
            scale.mScaleYAspect = data.GetCurFloat(idx);
        }
    }
    

    public class CompositionItemDef
    {
        public int32 mIdxInParent;
        public String mName;
        public StructuredData mData;
        public List<CompositionItemDef> mChildItemDefs;    
        public Type mObjectType;
        public CompositionItemDef mFollowingItemDef;
        public Guid mBoundResId;

        public TimelineData<CompositionPos> mTimelineAnchor = new TimelineData<CompositionPos>(new => CompositionPos.Interpolator);
        public TimelineData<CompositionPos> mTimelinePosition = new TimelineData<CompositionPos>(new => CompositionPos.Interpolator);
        public TimelineData<CompositionScale> mTimelineScale = new TimelineData<CompositionScale>(new => CompositionScale.Interpolator);
        public TimelineData<float> mTimelineRot = new TimelineData<float>(new => Utils.Lerp);
        public TimelineData<Color> mTimelineColor = new TimelineData<Color>(new => Color.Lerp);

        public this()
        {
            CompositionScale aScale;
            aScale.mScaleX = 1;
            aScale.mScaleYAspect = 1;
            mTimelineScale.mConstantValue = aScale;
        }

        protected delegate void ComponentReadAction<T>(StructuredData data, int32 pos, ref T refValue);

        protected bool DeserializeTimeline<T>(StructuredData data, TimelineData<T> timelineData, String name,
            String[] componentNames, ComponentReadAction<T>[] readActions)
        {
            /*using (data.Open(name))
            {                
                if (data.Count == 0)
                    return false;

                if (data.IsObject)
                {
                    using (data.Open("Times"))
                    {
                        timelineData.mEntries = new List<TimelineData<T>.Entry>(data.Count);

                        for (int32 valueIdx = 0; valueIdx < data.Count; valueIdx++)
                        {
                            TimelineData<T>.Entry entry;
                            entry.mFrame = data.GetInt(valueIdx);
                            entry.mValue = default(T);
                            timelineData.mEntries.Add(entry);
                        }
                    }

                    for (int32 componentIdx = 0; componentIdx < componentNames.Count; componentIdx++)
                    {
                        using (data.Open(componentNames[componentIdx]))
                        {
                            if (readActions != null)
                            {
                                for (int32 valueIdx = 0; valueIdx < data.Count; valueIdx++)
                                {
                                    var entry = timelineData.mEntries[valueIdx];
                                    readActions[componentIdx](data, valueIdx, ref entry.mValue);
                                    timelineData.mEntries[valueIdx] = entry;
                                }
                            }
                            else
                            {
                                for (int32 valueIdx = 0; valueIdx < data.Count; valueIdx++)
                                {
                                    var entry = timelineData.mEntries[valueIdx];
                                    entry.mValue = (T)data.Get(valueIdx);
                                    timelineData.mEntries[valueIdx] = entry;
                                }
                            }
                        }
                    }
                }
                else
                {
                    Debug.Assert(data.Count == componentNames.Count);
                    for (int32 componentIdx = 0; componentIdx < componentNames.Count; componentIdx++)
                    {
                        if (readActions != null)
                            readActions[componentIdx](data, componentIdx, ref timelineData.mConstantValue);
                        else
                            timelineData.mConstantValue = (T)data.Get(componentIdx);
                    }
                }
            }*/

            return true;
        }

        public void Deserialize(StructuredData data)
        {
            /*mData = data.Current;

            String resId = scope String();
            mData.GetString(resId, "ResId");
            if (resId != null)
                mBoundResId = Guid.Parse(resId);
			mName = new String();
            mData.GetString(mName, "Name");
            using (data.Open("Children"))
            {
                if (data.Count > 0)
                    mChildItemDefs = new List<CompositionItemDef>();

                for (int32 childIdx = 0; childIdx < data.Count; childIdx++)
                {
                    CompositionItemDef childItemDef = new CompositionItemDef();
                    childItemDef.mIdxInParent = childIdx;
                    mChildItemDefs.Add(childItemDef);
                }

                for (int32 childIdx = 0; childIdx < data.Count; childIdx++)
                {
                    using (data.Open(childIdx))
                    {
                        String typeName = scope String();
                        data.GetString(typeName, "Type");

                        Type aType = null;
						ThrowUnimplemented();
                        //aType = Type.GetType(typeName);

                        CompositionItemDef childItemDef = mChildItemDefs[childIdx];                        
                        childItemDef.mObjectType = aType;

                        int32 followIdx = data.GetInt("Follow", -1);
                        if (followIdx != -1)
                            childItemDef.mFollowingItemDef = mChildItemDefs[followIdx];

                        childItemDef.Deserialize(data);
                    }
                }
            }

            DeserializeTimeline(data, mTimelineAnchor, "Anchor",
                scope String[] { "X", "Y" },
                scope ComponentReadAction<CompositionPos>[] { scope => CompositionPos.ReadX, scope => CompositionPos.ReadY });

            DeserializeTimeline(data, mTimelinePosition, "Position",
                scope String[] { "X", "Y" },
                scope ComponentReadAction<CompositionPos>[] { scope => CompositionPos.ReadX, scope => CompositionPos.ReadY });

            DeserializeTimeline(data, mTimelineScale, "Scale",
                scope String[] { "X", "YAspect" },
                scope ComponentReadAction<CompositionScale>[] { scope => CompositionScale.ReadX, scope => CompositionScale.ReadY });

            DeserializeTimeline(data, mTimelineRot, "Rotation",
                scope String[] { "Angle" },
                null);*/            
        }

        protected void SerializeTimeline<T>(StructuredData data, TimelineData<T> timelineData, String name,
            String[] componentNames, Delegate[] writeActions, T theDefault) where T : IEquatable<T>
        {                        
            /*if ((timelineData.mEntries != null) && (timelineData.mEntries.Count > 0))
            {
                using (data.CreateObject(name))
                {
                    using (data.CreateArray("Times"))
                        foreach (var entry in timelineData.mEntries)
                            data.Add((int32)entry.mFrame);

                    for (int32 componentIdx = 0; componentIdx < componentNames.Count; componentIdx++)
                    {
                        using (data.CreateArray(componentNames[componentIdx]))
                        {
                            if (writeActions != null)
                            {
                                foreach (var entry in timelineData.mEntries)
                                    writeActions[componentIdx](data, entry.mValue);
                            }
                            else
                            {
                                foreach (var entry in timelineData.mEntries)
                                    data.DoAdd(entry.mValue);
                            }
                        }
                    }
                }
            }
            else
            {
                if (!timelineData.mConstantValue.Equals(theDefault))
                {
                    using (data.CreateArray(name))
                    {                        
                        for (int32 componentIdx = 0; componentIdx < componentNames.Count; componentIdx++)
                        {
                            if (writeActions != null)
                                writeActions[componentIdx](data, timelineData.mConstantValue);
                            else
                                data.DoAdd(timelineData.mConstantValue);
                        }                        
                    }
                }
            }*/
        }

        // Serialization always occurs from a CompositionItemInst.Serialize so there's so this isn't a direct
        //  parallel to Deserialize -- children aren't iterated through, for example
        public void PartialSerialize(StructuredData data)
        {
            if (mBoundResId != Guid.Empty)
			{
				var resIdStr = scope String();
				mBoundResId.ToString(resIdStr);
                data.Add("ResId", resIdStr);
			}

            /*SerializeTimeline(data, mTimelineAnchor, "Anchor",
                scope String[] { "X", "Y" },
                scope Action<StructuredData, CompositionPos>[] { scope => CompositionPos.WriteX, scope => CompositionPos.WriteY },
                CompositionPos(0, 0));

            SerializeTimeline(data, mTimelinePosition, "Position",
                scope String[] { "X", "Y" },
                scope Action<StructuredData, CompositionPos>[] { scope => CompositionPos.WriteX, scope => CompositionPos.WriteY },
                CompositionPos(0, 0));

            SerializeTimeline(data, mTimelineScale, "Scale",
                scope String[] { "X", "YAspect" },
                scope Action<StructuredData, CompositionScale>[] { scope => CompositionScale.WriteX, scope => CompositionScale.WriteY },
                CompositionScale(1, 1));

            SerializeTimeline(data, mTimelineRot, "Rotation",
                scope String[] { "Angle" },
                null,
                0.0f);*/
        }
        
        public void RebuildChildIndices()
        {
            if (mChildItemDefs != null)
            {
                for (int32 childItemDefIdx = 0; childItemDefIdx < mChildItemDefs.Count; childItemDefIdx++)
                {
                    var childItemDef = mChildItemDefs[childItemDefIdx];
                    childItemDef.mIdxInParent = childItemDefIdx;
                }

                /*foreach (CompositionItemDef childItemDef in mChildItemDefs)
                {
                }*/
            }
        }
    }    

    public class CompositionDef
    {
        public CompositionItemDef mRootItemDef;        

        public float mFrameRate;
        public int32 mGridX = 8;
        public int32 mGridY = 8;
        public bool mGridEnabled = true;        

        public uint32 mBkgColor;
        public StructuredData mData;        

        public void Serialize(StructuredData data)
        {            
            data.Add("FrameRate", mFrameRate);
            data.Add("BkgColor", (int32) mBkgColor);
        }

        public void Deserialize(StructuredData data)
        {
            /*mData = data.Current;            
            mFrameRate = data.GetFloat("FrameRate");
            mBkgColor = (uint32)data.GetInt("BkgColor");            
            mRootItemDef = new CompositionItemDef();                
            mRootItemDef.Deserialize(data);*/            
        }
    }
    
    public class CompositionItemInst
    {   
        public enum HitCode
        {
            None,
            Inside,            
            CornerTopLeft,
            CornerTopRight,
            CornerBotLeft,
            CornerBotRight            
        };

        public Composition mComposition;
        public List<CompositionItemInst> mChildItemInsts; 
        public CompositionItemDef mItemDef;        
        public CompositionItemInst mParentItemInst;
        public Object mRuntimeObject;
        public CompositionItemInst mFollowItemInst;

        public virtual CompositionItemInst FindItemInstAt(float x, float y, out HitCode hitCode, out float distance)
        {
            CompositionItemInst found = null;
            hitCode = HitCode.Inside;
            distance = 1000000;

            if (mChildItemInsts != null)
            {
                for (CompositionItemInst itemInst in mChildItemInsts)
                {
                    HitCode curHitCode;
                    float curDistance;
                    CompositionItemInst curFound = itemInst.FindItemInstAt(x, y, out curHitCode, out curDistance);
                    if (curFound != null)
                    {
                        if (curDistance <= distance)
                        {
                            distance = curDistance;
                            hitCode = curHitCode;
                            found = curFound;
                        }
                    }
                }
            }

            return found;
        }

        public CompositionItemInst FindItemInstForRuntimeObject(Object runtimeObject)
        {
            if (mRuntimeObject == runtimeObject)
                return this;

            if (mChildItemInsts != null)
            {
                for (CompositionItemInst itemInst in mChildItemInsts)
                {
                    CompositionItemInst found = itemInst.FindItemInstForRuntimeObject(runtimeObject);
                    if (found != null)
                        return found;
                }
            }

            return null;
        }

        public virtual void Serialize(StructuredData data)
        {
            data.Add("Name", mItemDef.mName);

            if (mChildItemInsts != null)
            {
                using (data.CreateArray("Children"))
                {
                    for (var childInst in mChildItemInsts)
                    {
                        using (data.CreateObject())
                        {
							var typeName = scope String();
							childInst.mRuntimeObject.GetType().GetFullName(typeName);
                            data.Add("Type", typeName);

                            if (childInst.mItemDef.mFollowingItemDef != null)
                            {
                                int followingIdx = mItemDef.mChildItemDefs.IndexOf(childInst.mItemDef.mFollowingItemDef);
                                data.Add("Follow", followingIdx);
                            }

                            childInst.mItemDef.PartialSerialize(data);
                            childInst.Serialize(data);
                        }
                    }
                }
            }
        }

        public virtual void Init(Object boundResource)
        {            
            if (mItemDef.mChildItemDefs != null)
            {
                mChildItemInsts = new List<CompositionItemInst>(mItemDef.mChildItemDefs.Count);

                for (int32 childDefIdx = 0; childDefIdx < mItemDef.mChildItemDefs.Count; childDefIdx++)
                {
                    var childDef = mItemDef.mChildItemDefs[childDefIdx];
                    Object childObject = Utils.DefaultConstruct(childDef.mObjectType);

                    CompositionItemInst childItemInst = null;

                    Widget childWidget = childObject as Widget;
                    if (childWidget != null)
                    {
                        CompositionWidgetInst selfWidgetInst = (CompositionWidgetInst)this;

                        selfWidgetInst.mWidget.AddWidget(childWidget);
                        childWidget.mIdStr = mItemDef.mName;

                        CompositionWidgetInst childWidgetInst = new CompositionWidgetInst();
                        childItemInst = childWidgetInst;

                        childWidgetInst.mWidget = childWidget;
                        childWidgetInst.mRuntimeObject = childWidget;
                        childWidgetInst.mComposition = mComposition;
                    }
                    else
                    {
                        //TODO: Create bone stuff
                    }
                    
                    childItemInst.mItemDef = childDef;
                    childItemInst.mParentItemInst = this;
                    mChildItemInsts.Add(childItemInst);
                }

                for (int32 childDefIdx = 0; childDefIdx < mItemDef.mChildItemDefs.Count; childDefIdx++)                
                {
                    var childDef = mItemDef.mChildItemDefs[childDefIdx];
                    CompositionItemInst childItemInst = mChildItemInsts[childDefIdx];

                    if (childDef.mFollowingItemDef != null)
                        childItemInst.mFollowItemInst = mChildItemInsts[childDef.mFollowingItemDef.mIdxInParent];

                    Object aBoundResource = null;
                    if (childDef.mBoundResId != Guid.Empty)
                        aBoundResource = BFApp.sApp.mResourceManager.GetResourceById(childDef.mBoundResId, true);

                    childItemInst.Init(aBoundResource);
                }
            }
        }

        public virtual void RemoveChild(CompositionItemInst inst)
        {
            mItemDef.mChildItemDefs.Remove(inst.mItemDef);            

            mChildItemInsts.Remove(inst);
            inst.mParentItemInst = null;
        }

        public virtual void AddChildAtIndex(int idx, CompositionItemInst inst)
        {
            if (mItemDef.mChildItemDefs == null)
                mItemDef.mChildItemDefs = new List<CompositionItemDef>();
            mItemDef.mChildItemDefs.Insert(idx, inst.mItemDef);

            mChildItemInsts.Insert(idx, inst);
            inst.mParentItemInst = this;
        }

        public virtual void InsertChild(CompositionItemInst item, CompositionItemInst insertBefore)
        {
            int aPos = (insertBefore != null) ? mChildItemInsts.IndexOf(insertBefore) : mChildItemInsts.Count;
            AddChildAtIndex(aPos, item);
        }

        public virtual void AddChild(CompositionItemInst item, CompositionItemInst addAfter = null)
        {
            int aPos = (addAfter != null) ? (mChildItemInsts.IndexOf(addAfter) + 1) : mChildItemInsts.Count;
            AddChildAtIndex(aPos, item);
        }

        public virtual void DesignDraw(Graphics g)
        {
            if (mChildItemInsts != null)
            {
                for (var childInst in mChildItemInsts)
                    childInst.DesignDraw(g);
            }
        }

        public virtual void DesignInit(float x, float y)
        {
        }

        public virtual void DesignSetCreatePosition(float x, float y, bool isFinal)
        {
        }

        public virtual void GetCurTransformComponents(out CompositionPos anchorPos, out CompositionPos position, out CompositionScale scale, out float rotation)
        {
            float time = mComposition.mCurFrame;            
            anchorPos = mItemDef.mTimelineAnchor.GetValue(time);
            position = mItemDef.mTimelinePosition.GetValue(time);
            scale = mItemDef.mTimelineScale.GetValue(time);
            rotation = mItemDef.mTimelineRot.GetValue(time);
            if (mFollowItemInst == null)
                return;

            if (mFollowItemInst != null)
            {                                
                CompositionPos followAnchorPos;
                CompositionPos followPos;
                CompositionScale followScale;                
                float followRotation;
                mFollowItemInst.GetCurTransformComponents(out followAnchorPos, out followPos, out followScale, out followRotation);

                CompositionPos prevPos = position;

                prevPos.mX -= followAnchorPos.mX;
                prevPos.mY -= followAnchorPos.mY;
                
                prevPos.mX *= followScale.mScaleX;
                prevPos.mY *= followScale.mScaleX * followScale.mScaleYAspect;

                position.mX = (prevPos.mX * (float)Math.Cos(followRotation) - prevPos.mY * (float)Math.Sin(followRotation));
                position.mY = (prevPos.mX * (float)Math.Sin(followRotation) + prevPos.mY * (float)Math.Cos(followRotation));
                
                position.mX += followPos.mX;
                position.mY += followPos.mY;
                rotation += followRotation;

                scale.mScaleX *= followScale.mScaleX;
                scale.mScaleYAspect *= followScale.mScaleYAspect;
            }
        }

        public virtual void GetCurTransformMatrix(ref Matrix matrix, out bool isSimpleTranslate)
        {
            float time = mComposition.mCurFrame;
            CompositionPos anAnchorPos = mItemDef.mTimelineAnchor.GetValue(time);
            CompositionPos aPosition = mItemDef.mTimelinePosition.GetValue(time);
            CompositionScale aScale = mItemDef.mTimelineScale.GetValue(time);
            float aRotation = mItemDef.mTimelineRot.GetValue(time);

            if (mFollowItemInst != null)
            {                
                mFollowItemInst.GetCurTransformMatrix(ref matrix, out isSimpleTranslate);
                isSimpleTranslate = false;

                /*if ((aRotation == 0) && (aScale.mScaleX == 1.0f) && (aScale.mScaleYAspect == 1.0f))
                {
                    //m.tx = aPosition.mX - anAnchorPos.mX;
                    //m.ty = aPosition.mY - anAnchorPos.mY;
                    matrix.SetMultiplied(aPosition.mX - anAnchorPos.mX, aPosition.mX - anAnchorPos.mY, matrix);
                }
                else*/
                {
                    Matrix m = Matrix.IdentityMatrix;
                    m.Translate(-anAnchorPos.mX, -anAnchorPos.mY);
                    m.Scale(aScale.mScaleX, aScale.mScaleX * aScale.mScaleYAspect);
                    m.Rotate(aRotation);
                    m.Translate(aPosition.mX, aPosition.mY);

                    Matrix mat = m;
                    mat.Multiply(matrix);

                    matrix.SetMultiplied(m, matrix);
                }
            }
            else
            {
                matrix.Identity();
                if ((aRotation == 0) && (aScale.mScaleX == 1.0f) && (aScale.mScaleYAspect == 1.0f))
                {
                    isSimpleTranslate = true;
                    matrix.tx = aPosition.mX - anAnchorPos.mX;
                    matrix.ty = aPosition.mY - anAnchorPos.mY;                    
                }
                else
                {
                    isSimpleTranslate = false;
                    matrix.Translate(-anAnchorPos.mX, -anAnchorPos.mY);
                    matrix.Scale(aScale.mScaleX, aScale.mScaleX * aScale.mScaleYAspect);
                    matrix.Rotate(aRotation);
                    matrix.Translate(aPosition.mX, aPosition.mY);                    
                }
            }
        }

        public virtual void DesignTimelineEdited()
        {
        }        
    }

    public class CompositionBoneJoint : CompositionItemInst
    {        
        public float mX;
        public float mY;

        public override CompositionItemInst FindItemInstAt(float x, float y, out HitCode hitCode, out float distance)
        {
            hitCode = HitCode.Inside;
            distance = Math.Max(0, Utils.Distance(x - mX, y - mY) - 5);
            return this;
        }
    }

    public class CompositionBoneInst : CompositionItemInst
    {
        public CompositionBoneJoint mStartJoint;
        public CompositionBoneJoint mEndJoint;
        public float mWidth;

        public static Image sBoneTex;

        CompositionBoneJoint CreateJoint()
        {
            CompositionBoneJoint joint = new CompositionBoneJoint();
            joint.mParentItemInst = this;
            if (mChildItemInsts == null)
                mChildItemInsts = new List<CompositionItemInst>();
            mChildItemInsts.Add(joint);
            //joint.mComposition = this;
            
            //mParentItemInst.mChildItemInsts.Add(joint);
            return joint;
        }

        public override void DesignInit(float x, float y)
        {
            /*mX = mComposition.mWidth / 2;
            mY = mComposition.mHeight / 2;

            mAngle = (float)Math.Atan2(y - mY, x - mX);
            mLength = Utils.Distance(y - mY, x - mX);*/

            /*HitCode aHitCode;
            float aDistance;
            mComposition.mRootItemInst.FindItemInstAt(x, y, out aHitCode, out aDistance);*/

            if (mStartJoint == null)
            {
                mStartJoint = CreateJoint();
                mStartJoint.mX = x;
                mStartJoint.mY = y;
            }
            mEndJoint = CreateJoint();
            mEndJoint.mX = x;
            mEndJoint.mY = y;

            mWidth = 5;
        }

        public override void DesignSetCreatePosition(float x, float y, bool isFinal)
        {
            mEndJoint.mX = x;
            mEndJoint.mY = y;
        }

        public override void DesignDraw(Graphics g)
        {
            base.DesignDraw(g);

            float startX = mStartJoint.mX;
            float startY = mStartJoint.mY;

            float endX = mEndJoint.mX;
            float endY = mEndJoint.mY;
            
            Matrix m = Matrix.IdentityMatrix;

            float angle = (float)Math.Atan2(endY - startY, endX - startX);
            float length = Utils.Distance(endY - startY, endX - startX);

            m.Rotate(angle - Math.PI_f / 2);
            m.Translate(startX, startY);

            using (g.PushMatrix(m))
            {
                for (int32 row = 0; row < 2; row++)
                {
                    for (int32 col = 0; col < 2; col++)
                    {
                        float yStart = 0;

                        float thicknessW = 1.0f;
                        float thicknessH = 4;

                        float aWidth = mWidth;
                        float aHeight = length;

                        float splitPct = Math.Min(0.25f, 16.0f / length);

                        if (col == 1)
                        {
                            thicknessW = -thicknessW;
                            aWidth = -aWidth;
                        }

                        if (row == 0)
                        {
                            aHeight *= splitPct;
                        }
                        else
                        {
                            yStart += aHeight;
                            aHeight = -aHeight * (1.0f - splitPct);
                            thicknessH = -thicknessH;
                        }

                        g.PolyStart(sBoneTex, 9);
                        
                        g.PolyVertex(0, 0, yStart - thicknessH, 0.125f, 0);
                        g.PolyVertex(1, -aWidth - thicknessW, yStart + aHeight, 0.125f, 0);
                        g.PolyVertex(2, 0, yStart + thicknessH, 0.625f, 0);

                        g.PolyVertexCopy(3, 1);
                        g.PolyVertexCopy(4, 2);
                        g.PolyVertex(5, -aWidth + thicknessW, yStart + aHeight, 0.625f, 0);

                        g.PolyVertexCopy(6, 4);
                        g.PolyVertexCopy(7, 5);
                        g.PolyVertex(8, 0, yStart + aHeight, 0.625f, 0);
                    }
                }
            }

			ThrowUnimplemented();
            //g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.UIBoneJoint), startX - 9, startY - 9);
            //g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.UIBoneJoint), endX - 9, endY - 9);

            /*using (g.PushColor(Color.Red))
                g.FillRect(endX, endY, 2, 2);*/
        }
    }
    
    public class CompositionWidgetInst : CompositionItemInst
    {
        public Widget mWidget;

        public override void Init(Object boundResource)
        {
            base.Init(boundResource);
            mWidget.Deserialize(mItemDef.mData);

            if (mWidget is ImageWidget)
            {
                ImageWidget imageWidget = (ImageWidget)mWidget;
                imageWidget.mImage = (Image)boundResource;
            }
        }

        public override void RemoveChild(CompositionItemInst inst)
        {
            base.RemoveChild(inst);
            if (inst is CompositionWidgetInst)
            {
                CompositionWidgetInst childWidgetInst = (CompositionWidgetInst)inst;
                mWidget.RemoveWidget(childWidgetInst.mWidget);
            }
        }

        public override void AddChildAtIndex(int idx, CompositionItemInst inst)
        {
            if (inst is CompositionWidgetInst)
            {
                CompositionWidgetInst childWidgetInst = (CompositionWidgetInst)inst;
                int32 insertIdx = 0;

                for (int32 checkIdx = 0; checkIdx < idx; checkIdx++)
                {
                    if (mChildItemInsts[checkIdx] is CompositionWidgetInst)
                        insertIdx = checkIdx + 1;
                }

                mWidget.AddWidgetAtIndex(insertIdx, childWidgetInst.mWidget);

            }

            base.AddChildAtIndex(idx, inst);
        }

        public override CompositionItemInst FindItemInstAt(float x, float y, out HitCode hitCode, out float distance)
        {
            /*float transX;
            float transY;
            mWidget.SelfToOtherTranslate(mComposition, 0, 0, out transX, out transY);

            float dX = transX - x;
            if (dX < 0)
            {
                dX = x - (transX + mWidget.mWidth);
                if (dX < 0)
                    dX = 0;
            }

            float dY = transY - y;
            if (dY < 0)
            {
                dY = y - (transY + mWidget.mHeight);
                if (dX < 0)
                    dX = 0;
            }*/

            float transX;
            float transY;
            mComposition.SelfToRootTranslate(x, y, out transX, out transY);
            mWidget.RootToSelfTranslate(transX, transY, out transX, out transY);            
            
            float dX = -transX;
            if (dX < 0)
            {
                dX = transX - mWidget.mWidth;
                if (dX < 0)
                    dX = 0;
            }

            float dY = -transY;
            if (dY < 0)
            {
                dY = transY - mWidget.mHeight;
                if (dY < 0)
                    dY = 0;
            }
            
            distance = Math.Max(dX, dY);
            hitCode = HitCode.Inside;

            HitCode childHitCode;
            float childDistance;
            CompositionItemInst childInst = base.FindItemInstAt(x, y, out childHitCode, out childDistance);
            if (childDistance <= distance)
            {
                hitCode = childHitCode;
                distance = childDistance;
                return childInst;
            }            

            return this;
        }

        public override void Serialize(StructuredData data)
        {
            mWidget.Serialize(data);
            base.Serialize(data);
        }
        
        public override void DesignTimelineEdited()
        {            
            bool isSimpleTranslate = false;
            if (mWidget.mTransformData != null)
            {
                var m = mWidget.Transform;
                GetCurTransformMatrix(ref m, out isSimpleTranslate);
                mWidget.Transform = m;
            }
            else
            {
                // Check the matrix and see if we need a matrix or not
                Matrix m = Matrix();
                GetCurTransformMatrix(ref m, out isSimpleTranslate);
                if (isSimpleTranslate)
                {
                    mWidget.mX = m.tx;
                    mWidget.mY = m.ty;
                }
                else
                {
                    mWidget.Transform = m;
                }
            }
            
        }
    }

    public class Composition : Widget
    {
        public CompositionDef mDef;        
        public CompositionWidgetInst mRootItemInst;
        public float mCurFrame;

        [DesignEditable(DisplayType="Color")]
        public uint32 BkgColor { get { return mDef.mBkgColor; } set { mDef.mBkgColor = value; } }

        public void CreateNew(float width, float height, float frameRate = 60)
        {
            mDef = new CompositionDef();
            mDef.mFrameRate = frameRate;

            CompositionItemDef itemDef = new CompositionItemDef();            
            mDef.mRootItemDef = itemDef;
            mDef.mRootItemDef.mChildItemDefs = new List<CompositionItemDef>();

            CompositionWidgetInst itemInst = new CompositionWidgetInst();
            itemInst.mItemDef = itemDef;
            itemInst.mWidget = this;
            itemInst.mRuntimeObject = this;
            itemInst.mComposition = this;
            itemInst.mChildItemInsts = new List<CompositionItemInst>();
            mRootItemInst = itemInst;

            mWidth = width;
            mHeight = height;
            
            //itemInst.Init(false);
        }

        public void CreateFrom(CompositionDef def)
        {
            mDef = def;

            mRootItemInst = new CompositionWidgetInst();
            mRootItemInst.mItemDef = mDef.mRootItemDef;
            mRootItemInst.mWidget = this;
            mRootItemInst.mRuntimeObject = this;
            mRootItemInst.mComposition = this;
            mRootItemInst.Init(null);            

            Deserialize(mDef.mData);
            UpdateAllItemDataPositions(mRootItemInst);
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            if (mDef.mBkgColor != 0)
            {
                using (g.PushColor(mDef.mBkgColor))
                    g.FillRect(0, 0, mWidth, mHeight);
            }
        }

        public virtual void DesignDraw(Graphics g)
        {
            mRootItemInst.DesignDraw(g);
        }

        void UpdateAllItemDataPositions(CompositionItemInst compositionItem)
        {
            if (compositionItem.mChildItemInsts != null)
            {
                for (var childItem in compositionItem.mChildItemInsts)
                    childItem.DesignTimelineEdited();
            }
        }

        public void UpdatePositions()
        {
            UpdateAllItemDataPositions(mRootItemInst);
        }
    }
}
