using System;
using System.Collections;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.theme;
using Beefy.gfx;

namespace IDE.ui
{

	class BuildPropertiesDialog : TargetedPropertiesDialog
	{
		protected class DistinctOptionBuilder
		{
			BuildPropertiesDialog mDialog;

			public this(BuildPropertiesDialog dialog)
			{
				mDialog = dialog;
			}

			class TypeOptionsEntry
			{
				public String mConfigName;
				public String mPlatformName;
				public List<DistinctBuildOptions> mPropTargets = new .() ~ delete _;
			}

			List<String> mTypeOptionsNames = new .() ~ delete _;
			Dictionary<String, TypeOptionsEntry> mTypeOptionsDict = new .() ~ delete _;
			public int mPropCount;

			public void Add(List<DistinctBuildOptions> distinctBuildOptions)
			{
				int propIdx = mPropCount;
				mPropCount++;

				for (int typeOptionIdx < distinctBuildOptions.Count)
				{
					var typeOptions = distinctBuildOptions[typeOptionIdx];
					if (typeOptions.mCreateState == .Deleted)
						continue;

					String* keyPtr;
					TypeOptionsEntry* typeOptionsPtr;
					TypeOptionsEntry typeOptionsEntry;
					if (mTypeOptionsDict.TryAdd(typeOptions.mFilter, out keyPtr, out typeOptionsPtr))
					{
						mTypeOptionsNames.Add(typeOptions.mFilter);
						typeOptionsEntry = new TypeOptionsEntry();
						*typeOptionsPtr = typeOptionsEntry;
					}
					else
					{
						typeOptionsEntry = *typeOptionsPtr;
					}
					typeOptionsEntry.mConfigName = mDialog.[Friend]mConfigNames[propIdx / mDialog.[Friend]mPlatformNames.Count];
					typeOptionsEntry.mPlatformName = mDialog.[Friend]mPlatformNames[propIdx % mDialog.[Friend]mPlatformNames.Count];
					typeOptionsEntry.mPropTargets.Add(typeOptions);
				}
			}

			public void Finish()
			{
				var root = (DarkListViewItem)mDialog.[Friend]mPropPage.mPropertiesListView.GetRoot();

				let configNames = mDialog.[Friend]mConfigNames;
				let platformNames = mDialog.[Friend]mPlatformNames;

				for (var typeOptionsName in mTypeOptionsNames)
				{
					var typeOptionsEntry = mTypeOptionsDict[typeOptionsName];

					var prevPropTargets = mDialog.[Friend]mCurPropertiesTargets;
					mDialog.[Friend]mCurPropertiesTargets = null;
					defer { mDialog.[Friend]mCurPropertiesTargets = prevPropTargets; }
					mDialog.[Friend]mCurPropertiesTargets = scope Object[typeOptionsEntry.mPropTargets.Count];
					for (int i < typeOptionsEntry.mPropTargets.Count)
						mDialog.[Friend]mCurPropertiesTargets[i] = typeOptionsEntry.mPropTargets[i];

					String label = scope .("Distinct Build Options");
					if (typeOptionsEntry.mPropTargets.Count < configNames.Count * platformNames.Count)
					{
						if (typeOptionsEntry.mPropTargets.Count > 1)
							label.Append(" <Multiple>");
						else if ((configNames.Count > 1) && (platformNames.Count > 1))
							label.AppendF(" ({0}/{1})", configNames[0], platformNames[1]);
						else if (configNames.Count > 1)
							label.AppendF(" ({0})", configNames[0]);
						else
							label.AppendF(" ({0})", platformNames[0]);
					}

					let (category, propEntry) = mDialog.AddPropertiesItem(root, label, "mFilter");
					mDialog.SetupDistinctBuildOptions(propEntry);
					mDialog.AddDistinctBuildOptions(category, -1, true);

					delete typeOptionsEntry;
				}
			}
		}

		protected List<Object> mDeferredDeleteList = new .() ~
		{
			for (let obj in _)
			{
				delete obj;
			}
			delete _;
		};

		protected void AddDistinctBuildOptions(DarkListViewItem category, int typeOptionIdx, bool isNew)
		{
			String optionsName = "";

			String typeName = scope String();

			if (!isNew)
			{
				typeName.Clear(); typeName.Append(optionsName, "mFilter");
				let propEntry = SetupPropertiesItem(category, "Filter", typeName);
				SetupDistinctBuildOptions(propEntry);
			}

			typeName.Clear(); typeName.Append(optionsName, "mBfSIMDSetting");
			AddPropertiesItem(category, "SIMD Instructions", typeName);

			typeName.Clear(); typeName.Append(optionsName, "mBfOptimizationLevel");
			AddPropertiesItem(category, "Optimization Level", typeName);

			typeName.Clear(); typeName.Append(optionsName, "mEmitDebugInfo");
			AddPropertiesItem(category, "Debug Info", typeName);

			typeName.Clear(); typeName.Append(optionsName, "mRuntimeChecks");
			AddPropertiesItem(category, "Runtime Checks", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mEmitDynamicCastCheck");
			AddPropertiesItem(category, "Dynamic Cast Check", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mEmitObjectAccessCheck");
			AddPropertiesItem(category, "Object Access Check", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mAllocStackTraceDepth");
			AddPropertiesItem(category, "Alloc Stack Trace Depth", typeName);

			let (reflectItem, ?) = AddPropertiesItem(category, "Reflect");

			typeName.Clear(); typeName.Append(optionsName, "mReflectAlwaysInclude");
			AddPropertiesItem(reflectItem, "Always Include", typeName);

			typeName.Clear(); typeName.Append(optionsName, "mReflectStaticFields");
			AddPropertiesItem(reflectItem, "Static Fields", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mReflectNonStaticFields");
			AddPropertiesItem(reflectItem, "Non-Static Fields", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mReflectStaticMethods");
			AddPropertiesItem(reflectItem, "Static Methods", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mReflectNonStaticMethods");
			AddPropertiesItem(reflectItem, "Non-Static Methods", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mReflectConstructors");
			AddPropertiesItem(reflectItem, "Constructors", typeName,
				scope String[] ( "No", "Yes" ));

			typeName.Clear(); typeName.Append(optionsName, "mReflectMethodFilter");
			AddPropertiesItem(reflectItem, "Method Filter", typeName);

			category.Open(true, true);
		}

		protected virtual Object[] PhysAddNewDistinctBuildOptions()
		{
			return null;
		}

		protected void AddNewDistinctBuildOptions()
		{
			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();

			var (category, propEntry) = AddPropertiesItem(root, "Distinct Build Options");
			var subItem = (DarkListViewItem)category.CreateSubItem(1);
			subItem.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, 0xFFC0C0C0);
			subItem.Label = "<Add New>...";
			subItem.mOnMouseDown.Add(new (evt) =>
		        {
					if (category.GetChildCount() != 0)
					{
						return;
					}

					subItem.Label = "";

					let typeOptionsTargets = PhysAddNewDistinctBuildOptions();
					defer delete typeOptionsTargets;

					var prevTargets = mCurPropertiesTargets;
					defer { mCurPropertiesTargets = prevTargets; }
					mCurPropertiesTargets = typeOptionsTargets;

					AddDistinctBuildOptions(category, -1, false);
					AddNewDistinctBuildOptions();
		        });
			category.mIsBold = true;
			category.mTextColor = cHeaderColor;
		}

		protected void DeleteDistinctBuildOptions(DarkListViewItem listViewItem)
		{
			mPropPage.mPropEntries.TryGetValue(listViewItem, var propEntries);
			if (propEntries == null)
				return;
			for (var propEntry in propEntries)
			{
				if (@propEntry == 0)
				{
					propEntry.mListViewItem.mParentItem.RemoveChildItem(propEntry.mListViewItem, false);
					mDeferredDeleteList.Add(propEntry.mListViewItem);
				}
				propEntry.mListViewItem = null;
				var typeOptions = (DistinctBuildOptions)propEntry.mTarget;
				typeOptions.mCreateState = .Deleted;
			}
		}

		protected void DeleteDistinctBuildOptions()
		{
			List<PropEntry> typeOptionsEntries = scope .();
			for (var propEntryKV in mPropPage.mPropEntries)
			{
				var propEntry = propEntryKV.value[0];
				if (propEntry.mPropertyName == "mFilter")
				{
					if (propEntry.mListViewItem != null)
						typeOptionsEntries.Add(propEntry);
				}
			}

			for (var propEntry in typeOptionsEntries)
				DeleteDistinctBuildOptions(propEntry.mListViewItem);
		}

		protected void UpdateDistinctBuildOptions(PropEntry propEntry)
		{
			if (propEntry.mListViewItem == null)
				return;

			var typeNames = propEntry.mCurValue.Get<String>();
			var subItem = (DarkListViewItem)propEntry.mListViewItem.GetSubItem(1);
			if ((typeNames.IsEmpty) || (typeNames == propEntry.mNotSetString))
			{
				subItem.Label = propEntry.mNotSetString;
				subItem.mTextColor = 0xFFC0C0C0;
			}
			else
			{
				bool isValid = true;
				for (let typeName in typeNames.Split(';'))
				{
					if ((!typeNames.StartsWith("@")) && (!gApp.mBfResolveCompiler.VerifyTypeName(scope String(typeName), -1)))
						isValid = false;
				}
				subItem.mTextColor = isValid ? 0xFFFFFFFF : 0xFFFF8080;
				propEntry.mColorOverride = subItem.mTextColor;
			}
		}

		protected bool ApplyDistinctBuildOptions(List<DistinctBuildOptions> distinctBuildOptions, bool apply)
		{
			bool appliedChange = false;
			for (let typeOptions in distinctBuildOptions)
			{
				if (typeOptions.mCreateState == .Normal)
					continue;

				if (((apply) && (typeOptions.mCreateState == .New)) ||
					((!apply) && (typeOptions.mCreateState == .Deleted)))
				{
					typeOptions.mCreateState = .Normal;
					continue;
				}

				if (apply)
					appliedChange = true;
				mDeferredDeleteList.Add(typeOptions);
				@typeOptions.Remove();
			}
			return appliedChange;
		}

		protected void SetupDistinctBuildOptions(PropEntry propEntry)
		{
			propEntry.mNotSetString = "<Wildcard>";
			UpdateDistinctBuildOptions(propEntry);
			propEntry.mOnUpdate.Add(new () => { UpdateDistinctBuildOptions(propEntry); return true; });
			propEntry.mIsTypeWildcard = true;

			ImageWidget closeWidget = new ImageWidget();
			closeWidget.mImage = DarkTheme.sDarkTheme.GetImage(.Close);
			closeWidget.mOverImage = DarkTheme.sDarkTheme.GetImage(.CloseOver);
			closeWidget.mOnMouseClick.Add(new (evt) =>
				{
					let valueItem = propEntry.mListViewItem.GetSubItem(1);
					if (!valueItem.Label.IsEmpty)
					{
						let dialog = ThemeFactory.mDefault.CreateDialog("Remove?", "Are you sure you want to remove the selected distinct build options?");
						dialog.AddYesNoButtons(new (evt) =>
							{
								DeleteDistinctBuildOptions(propEntry.mListViewItem);
							}, null, 0, 1);
						dialog.PopupWindow(mWidgetWindow);
					}
					else
					{
						DeleteDistinctBuildOptions(propEntry.mListViewItem);
					}
				});

			let subItem = propEntry.mListViewItem.GetSubItem(1);
			subItem.AddWidget(closeWidget);
			subItem.mOnResized.Add(new (evt) =>
				{
					let width = GetValueEditWidth(subItem);
					closeWidget.Resize(width - GS!(21), GS!(-1), GS!(20), subItem.mHeight + 1);
				});
		}
	}
}
