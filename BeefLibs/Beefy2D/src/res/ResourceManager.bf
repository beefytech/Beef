using System;
using System.Collections.Generic;
using System.Text;
using System.Reflection;
using Beefy.utils;
using Beefy.gfx;

namespace Beefy.res
{    
    public class ResInfo
    {        
        public String mName ~ delete _;
        public String mFilePath ~ delete _;
        public Guid mResId;

        public Object mLoadedObject;
    }
    
    public class ResImageInfo : ResInfo
    {

    }

    public class ResGroup
    {
        public List<ResInfo> mResInfoList = new List<ResInfo>() ~ DeleteContainerAndItems!(_);
    }

    public class ResourceManager
    {
        /*[StdCall, CLink]
        static extern void Wwise_Shutdown();*/

        public Dictionary<String, ResGroup> mResGroupMap = new Dictionary<String, ResGroup>() ~ delete _;
        public Dictionary<Guid, ResInfo> mIdToResInfoMap = new Dictionary<Guid, ResInfo>() ~ delete _;

        public ~this()
        {
            //Wwise_Shutdown();
        }

        public void ParseConfigData(StructuredData data)
        {
			ThrowUnimplemented();

            /*int fileResVer = data.GetInt("ResVer");
            using (data.Open("Groups"))
            {                
                for (int groupIdx = 0; groupIdx < data.Count; groupIdx++)
                {
                    using (data.Open(groupIdx))
                    {
                        ResGroup resGroup = new ResGroup();
                        mResGroupMap[data.GetString("Name")] = resGroup;

                        using (data.Open("Resources"))
                        {
                            for (int resIdx = 0; resIdx < data.Count; resIdx++)
                            {
                                using (data.Open(resIdx))
                                {
                                    ResInfo resInfo = null;

                                    string aType = data.GetString("Type");

                                    if (aType == "Image")
                                    {
                                        resInfo = new ResImageInfo();
                                    }

                                    resInfo.mName = data.GetString("Name");
                                    resInfo.mResId = Guid.Parse(data.GetString("Id"));
                                    resInfo.mFilePath = data.GetString("Path");
                                    resGroup.mResInfoList.Add(resInfo);
                                }
                            }
                        }
                    }
                }
            }

            Type appType = BFApp.sApp.GetType();
            Type resourcesType = appType.Assembly.GetType(appType.Namespace + ".Res");
			if (resourcesType == null)
				return;
            Type soundBanksTypes = resourcesType.GetNestedType("SoundBanks");
			if (soundBanksTypes != null)
			{			
				using (data.Open("SoundBanks"))
				{
					for (int soundBanksIdx = 0; soundBanksIdx < data.Count; soundBanksIdx++)
					{
						string name = data.Keys[soundBanksIdx];
						uint wwiseId = data.GetUInt(soundBanksIdx);

						SoundBank soundBank = new SoundBank();
						soundBank.mName = name;
						soundBank.mWwiseSoundId = wwiseId;

						FieldInfo fieldInfo = soundBanksTypes.GetField(name);
						fieldInfo.SetValue(null, soundBank);
					}
				}
			}

            Type soundEventsTypes = resourcesType.GetNestedType("SoundEvents");
			if (soundEventsTypes != null)
			{
				using (data.Open("SoundEvents"))
				{
					for (int soundBanksIdx = 0; soundBanksIdx < data.Count; soundBanksIdx++)
					{
						string name = data.Keys[soundBanksIdx];
						uint wwiseId = data.GetUInt(soundBanksIdx);

						SoundEvent soundRes = new SoundEvent();
						soundRes.mWwiseEventId = wwiseId;

						FieldInfo fieldInfo = soundEventsTypes.GetField(name);
						fieldInfo.SetValue(null, soundRes);
					}
				}
			}

            Type soundParametersTypes = resourcesType.GetNestedType("SoundParameters");
			if (soundParametersTypes != null)
			{
				using (data.Open("SoundParameters"))
				{
					for (int soundBanksIdx = 0; soundBanksIdx < data.Count; soundBanksIdx++)
					{
						string name = data.Keys[soundBanksIdx];
						uint wwiseId = data.GetUInt(soundBanksIdx);

						SoundParameter soundParameter = new SoundParameter();
						soundParameter.mWwiseParamId = wwiseId;

						FieldInfo fieldInfo = soundParametersTypes.GetField(name);
						fieldInfo.SetValue(null, soundParameter);
					}
				}
			}*/
        }

        public bool LoadResGroup(String groupName)
        {
			ThrowUnimplemented();

            /*Type appType = BFApp.sApp.GetType();
            Type resourcesType = appType.Assembly.GetType(appType.Namespace + ".Res");
            Type imagesType = resourcesType.GetNestedType("Images");

            ResGroup resGroup = mResGroupMap[groupName];
            foreach (ResInfo resInfo in resGroup.mResInfoList)
            {
                if (resInfo is ResImageInfo)
                {
                    string fileName = resInfo.mFilePath;
                    if (!fileName.Contains(':'))
                        fileName = BFApp.sApp.mInstallDir + resInfo.mFilePath;
                    Image image = Image.LoadFromFile(fileName, false);
                    FieldInfo fieldInfo = imagesType.GetField(resInfo.mName);
                    fieldInfo.SetValue(null, image);
                }
            }

            return true;*/
        }

        public Object GetResourceById(Guid id, bool allowLoad = false)
        {
			ThrowUnimplemented();

            /*ResInfo resInfo = mIdToResInfoMap[id];
            if ((resInfo.mLoadedObject == null) && (allowLoad))
            {
                if (resInfo is ResImageInfo)
                    resInfo.mLoadedObject = Image.LoadFromFile(resInfo.mFilePath, false);
            }            
            return resInfo.mLoadedObject;*/
        }        
    }
}
