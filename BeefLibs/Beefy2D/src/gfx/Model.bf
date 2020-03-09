using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.utils;
using Beefy.geom;

#if STUDIO_CLIENT
using Beefy.ipc;
#endif

namespace Beefy.gfx
{
    public class ModelDef
    {
        public struct JointTranslation
        {
            public Quaternion mQuat;
            public Vector3 mScale;
            public Vector3 mTrans;
        }

        //[StructLayout(LayoutKind.Sequential)]
        public struct VertexDef
        {
            [VertexMember(VertexElementUsage.Position3D)]
            public Vector3 mPosition;
            [VertexMember(VertexElementUsage.Color)]
            public uint32 mColor;
            [VertexMember(VertexElementUsage.TextureCoordinate)]
            public TexCoords mTexCoords;
            [VertexMember(VertexElementUsage.Normal)]
            public Vector3 mNormal;
            [VertexMember(VertexElementUsage.TextureCoordinate, 1)]
            public TexCoords mBumpTexCoords;
            [VertexMember(VertexElementUsage.Tangent)]
            public Vector3 mTangent;

            public static VertexDefinition sVertexDefinition ~ delete _;

			public static void Init()
			{
				sVertexDefinition = new VertexDefinition(typeof(VertexDef));
			}
        }
    }

    #if !STUDIO_CLIENT
    extension ModelDef
    {        
        public class Animation
        {            
            public void* mNativeModelDefAnimation;
            public int32 mFrameCount;
            public String mName;
            public int32 mAnimIdx;

            [StdCall, CLink]
            extern static void ModelDefAnimation_GetJointTranslation(void* nativeAnimation, int32 jointIdx, float frame, out JointTranslation jointTranslation);

            [StdCall, CLink]
            extern static int32 ModelDefAnimation_GetFrameCount(void* nativeAnimation);

            [StdCall, CLink]
            extern static char8* ModelDefAnimation_GetName(void* nativeAnimation);

            [StdCall, CLink]
            extern static void ModelDefAnimation_Clip(void* nativeAnimation, int32 startFrame, int32 numFrames);

            public this(void* nativeModelDefAnimation)
            {
                mNativeModelDefAnimation = nativeModelDefAnimation;
                mFrameCount = ModelDefAnimation_GetFrameCount(mNativeModelDefAnimation);
                mName = new String(ModelDefAnimation_GetName(mNativeModelDefAnimation));
            }

            public void GetJointTranslation(int32 jointIdx, float frame, out JointTranslation jointTranslation)
            {
                ModelDefAnimation_GetJointTranslation(mNativeModelDefAnimation, jointIdx, frame, out jointTranslation);
            }

            public void Clip(int32 startFrame, int32 numFrames)
            {
                ModelDefAnimation_Clip(mNativeModelDefAnimation, startFrame, numFrames);
                mFrameCount = ModelDefAnimation_GetFrameCount(mNativeModelDefAnimation);
            }
        }        

        public void* mNativeModelDef;        
        public float mFrameRate;
        public int32 mJointCount;
        public Animation[] mAnims;
        public Dictionary<String, Animation> mAnimMap = new Dictionary<String, Animation>();

        [StdCall, CLink]
        extern static void* Res_OpenFBX(String fileName, void* nativeVertexDef);

        [StdCall, CLink]
        extern static void* ModelDef_CreateModelInstance(void* nativeModel);

        [StdCall, CLink]
        extern static float ModelDef_GetFrameRate(void* nativeModel);

        [StdCall, CLink]
        extern static int32 ModelDef_GetJointCount(void* nativeModel);

        [StdCall, CLink]
        extern static int32 ModelDef_GetAnimCount(void* nativeModel);

        [StdCall, CLink]
        extern static void* ModelDef_GetAnimation(void* nativeModel, int32 animIdx);

        this(void* nativeModelDef)
        {
            mNativeModelDef = nativeModelDef;

            mFrameRate = ModelDef_GetFrameRate(mNativeModelDef);
            mJointCount = ModelDef_GetJointCount(mNativeModelDef);
            int32 animCount = ModelDef_GetAnimCount(mNativeModelDef);
            mAnims = new Animation[animCount];

            for (int32 animIdx = 0; animIdx < animCount; animIdx++)
            {
                var anim = new Animation(ModelDef_GetAnimation(mNativeModelDef, animIdx));
                anim.mAnimIdx = animIdx;                
                mAnims[animIdx] = anim;
                mAnimMap[anim.mName] = anim;
            }
        }

        public static ModelDef LoadModel(String fileName)
        {
            void* nativeModelDef = Res_OpenFBX(fileName, VertexDef.sVertexDefinition.mNativeVertexDefinition);
            if (nativeModelDef == null)
                return null;
            return new ModelDef(nativeModelDef);            
        }

        public ModelInstance CreateInstance()
        {
            void* nativeModelInstance = ModelDef_CreateModelInstance(mNativeModelDef);
            if (nativeModelInstance == null)
                return null;
            var modelInstance = new ModelInstance(nativeModelInstance, this);            
            return modelInstance;
        }

        public Animation GetAnimation(String name)
        {
            return mAnimMap[name];
        }
    }

    public class ModelInstance : RenderCmd
    {
        [StdCall, CLink]
        extern static void ModelInstance_SetJointTranslation(void* nativeModelInstance, int32 jointIdx, ref ModelDef.JointTranslation jointTranslation);

        [StdCall, CLink]
        extern static void ModelInstance_SetMeshVisibility(void* nativeModelInstance, int32 jointIdx, int32 visibility);

        public ModelDef mModelDef;
        public ModelDef.Animation mAnim;
        public float mFrame;
        public float mAnimSpeed = 1.0f;
        public bool mLoop;

        public this(void* nativeModelInstance, ModelDef modelDef)
        {
            mNativeRenderCmd = nativeModelInstance;
            mModelDef = modelDef;
        }

        public void RehupAnimState()
        {
            for (int32 jointIdx = 0; jointIdx < mModelDef.mJointCount; jointIdx++)
            {
                ModelDef.JointTranslation jointTranslation;
                mAnim.GetJointTranslation(jointIdx, mFrame, out jointTranslation);
                SetJointTranslation(jointIdx, ref jointTranslation);
            }
        }

        public void Update()
        {
            if (mAnim == null)
                return;

            mFrame += mModelDef.mFrameRate * BFApp.sApp.UpdateDelta * mAnimSpeed;

            /*if ((mFrame >= 35.0f) || (mFrame < 1.0f))
                mFrame = 34.0f;*/

            if (mAnim.mFrameCount > 1)
            {
                float endFrameNum = mAnim.mFrameCount - 1.0f;
                while (mFrame >= endFrameNum)
                {
                    if (mLoop)
                        mFrame -= endFrameNum;
                    else
                        mFrame = endFrameNum - 0.00001f;
                }
                while (mFrame < 0)
                    mFrame += endFrameNum;
            }

            RehupAnimState();
        }

        public void Play(ModelDef.Animation anim, bool loop = false)
        {
            mLoop = loop;
            mAnim = anim;
            mFrame = 0;
            RehupAnimState();            
        }

        public void Play(String name, bool loop = false)
        {            
            Play(mModelDef.GetAnimation(name), loop);
        }

        public void Play(bool loop = false)
        {
            Play(mModelDef.mAnims[0], loop);
        }

        public void SetJointTranslation(int32 jointIdx, ref ModelDef.JointTranslation jointTranslation)
        {
            ModelInstance_SetJointTranslation(mNativeRenderCmd, jointIdx, ref jointTranslation);
        }

        public void SetMeshVisibility(int32 meshIdx, bool visible)
        {
            ModelInstance_SetMeshVisibility(mNativeRenderCmd, meshIdx, visible ? 1 : 0);
        }
    }
#else
    extension ModelDef
    {        
        public class Animation
        {
            public void* mNativeModelDefAnimation;
            public int mFrameCount;
            public string mName;
            public int mAnimIdx;
            public IPCProxy<IStudioModelDefAnimation> mStudioModelDefAnimation;

            internal Animation(IPCProxy<IStudioModelDefAnimation> studioModelDefAnimation)
            {
                mStudioModelDefAnimation = studioModelDefAnimation;
                mFrameCount = mStudioModelDefAnimation.Proxy.GetFrameCount();
                mName = mStudioModelDefAnimation.Proxy.GetName();
            }
            
            public void Clip(int startFrame, int numFrames)
            {
                //ModelDefAnimation_Clip(mNativeModelDefAnimation, startFrame, numFrames);
                //mFrameCount = ModelDefAnimation_GetFrameCount(mNativeModelDefAnimation);
                mStudioModelDefAnimation.Proxy.Clip(startFrame, numFrames);
                mFrameCount = mStudioModelDefAnimation.Proxy.GetFrameCount();
            }
        }

        public IPCProxy<IStudioModelDef> mStudioModelDef;        
        public float mFrameRate;
        public int mJointCount;
        public Animation[] mAnims;
        public Dictionary<string, Animation> mAnimMap = new Dictionary<string, Animation>();

        ModelDef(IPCProxy<IStudioModelDef> studioModelDef)
        {
            mStudioModelDef = studioModelDef;

            mFrameRate = mStudioModelDef.Proxy.GetFrameRate();// ModelDef_GetFrameRate(mNativeModelDef);
            mJointCount = mStudioModelDef.Proxy.GetJointCount();
            int animCount = mStudioModelDef.Proxy.GetAnimCount();
            mAnims = new Animation[animCount];

            for (int animIdx = 0; animIdx < animCount; animIdx++)
            {
                var objId = mStudioModelDef.Proxy.GetAnimation(animIdx);
                var anim = new Animation(IPCProxy<IStudioModelDefAnimation>.Create(objId));
                anim.mAnimIdx = animIdx;
                mAnims[animIdx] = anim;
                mAnimMap[anim.mName] = anim;
            }
        }

        public static ModelDef LoadModel(string fileName)
        {
            var objId = BFApp.StudioHostProxy.Res_OpenFBX(fileName, VertexDef.sVertexDefinition.mStudioVertexDefinition);
            IPCProxy<IStudioModelDef> studioModelDef = IPCProxy<IStudioModelDef>.Create(objId);
            if (studioModelDef == null)
                return null;
            return new ModelDef(studioModelDef);
        }

        public ModelInstance CreateInstance()
        {
            var objId = mStudioModelDef.Proxy.CreateModelInstance();
            IPCProxy<IStudioModelInstance> studioModelInstance = IPCProxy<IStudioModelInstance>.Create(objId);
            if (studioModelInstance == null)
                return null;
            var modelInstance = new ModelInstance(studioModelInstance, this);
            return modelInstance;
        }

        public Animation GetAnimation(string name)
        {
            return mAnimMap[name];
        }
    }

    public class ModelInstance : RenderCmd
    {        
        public ModelDef mModelDef;
        public ModelDef.Animation mAnim;
        public float mFrame;
        public float mAnimSpeed = 1.0f;
        public bool mLoop;
        public IPCProxy<IStudioModelInstance> mStudioModelInstance;

        internal ModelInstance(IPCProxy<IStudioModelInstance> studioModelInstance, ModelDef modelDef)
        {
            mStudioModelInstance = studioModelInstance;
            var objId = studioModelInstance.Proxy.GetAsRenderCmd();
            mStudioRenderCmd = IPCProxy<IStudioRenderCmd>.Create(objId);
            mModelDef = modelDef;
        }

        void RehupAnimState()
        {
            mStudioModelInstance.Proxy.RehupAnimState(mAnim.mAnimIdx, mFrame);            
        }

        public void Update()
        {
            if (mAnim == null)
                return;

            mFrame += mModelDef.mFrameRate * BFApp.sApp.UpdateDelta * mAnimSpeed;

            /*if ((mFrame >= 35.0f) || (mFrame < 1.0f))
                mFrame = 34.0f;*/

            if (mAnim.mFrameCount > 1)
            {
                float endFrameNum = mAnim.mFrameCount - 1.0f;
                while (mFrame >= endFrameNum)
                {
                    if (mLoop)
                        mFrame -= endFrameNum;
                    else
                        mFrame = endFrameNum - 0.00001f;
                }
                while (mFrame < 0)
                    mFrame += endFrameNum;
            }

            RehupAnimState();
        }

        public void Play(ModelDef.Animation anim, bool loop = false)
        {
            mLoop = loop;
            mAnim = anim;
            mFrame = 0;
            RehupAnimState();
        }

        public void Play(string name, bool loop = false)
        {
            Play(mModelDef.GetAnimation(name), loop);
        }

        public void Play(bool loop = false)
        {
            Play(mModelDef.mAnims[0], loop);
        }
        
        public void SetMeshVisibility(int meshIdx, bool visible)
        {
            mStudioModelInstance.Proxy.SetMeshVisibility(meshIdx, visible);
        }
    }
#endif
}
