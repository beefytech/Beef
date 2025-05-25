using System;
using System.Collections;
using System.IO;
using System.Reflection;
using Beefy.utils;

namespace IDE
{
	class CustomBuildProperties
	{
		static Dictionary<String, String> mProperties = new .() ~ DeleteDictionaryAndKeysAndValues!(_);

		static public void GetFileName(String outResult)
		{
			String workspaceDir = scope String();

			if (gApp.mWorkspace.mDir == null)
				Directory.GetCurrentDirectory(workspaceDir);
			else
				workspaceDir = gApp.mWorkspace.mDir;

			outResult.Append(workspaceDir, "/BeefProperties.toml");
		}

		static public void Clear()
		{
			for (var entry in mProperties)
			{
				delete entry.key;
				delete entry.value;
			}

			mProperties.Clear();
		}

		static public void Load(bool clearExistingProperties = true)
		{
			const char8* PROPERTIES_STR = "Properties";

			if (clearExistingProperties)
				Clear();

			String propertiesFileName = scope String();
			GetFileName(propertiesFileName);

			if (!File.Exists(propertiesFileName))
				return;

			StructuredData data = scope StructuredData();
			if (gApp.StructuredLoad(data, propertiesFileName) case .Err(let err))
			{
				gApp.OutputErrorLine("Failed to load properties from: '{0}'", propertiesFileName);
				return;
			}

			if (!data.Contains(PROPERTIES_STR))
				return;

			for (var property in data.Enumerate(PROPERTIES_STR))
			{
				String propertyName = new String();
				property.ToString(propertyName);

				if (Contains(propertyName))
				{
					delete propertyName;
					continue;
				}

				String propertyValue = new String();
				data.GetCurString(propertyValue);

				TryAddProperty(propertyName, propertyValue);
			}
		}

		static public bool TryAddProperty(String propertyName, String propertyValue)
		{
			if (Contains(propertyName))
				return false;

			mProperties.Add(propertyName, propertyValue);
			return true;
		}

		static public bool Contains(String property)
		{
			return mProperties.ContainsKey(property);
		}

		static public String Get(String property)
		{
			if (!Contains(property))
				return null;

			return mProperties[property];
		}
	}
}
