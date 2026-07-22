using System;
using System.IO;
using System.Globalization;
using Beefy;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.utils;
using Beefy.events;

namespace IDE.ui
{
    /// Provides binary content backed by a file on disk
    public class BinaryFileContentProvider : IBinaryDataContentProvider
    {
        FileStream mFileStream ~ delete _;
        bool mReadOnly;
        public int mFileSize;

        public bool IsReadOnly { get { return mReadOnly; } }
        public int DataSize { get { return mFileSize; } }

        public Result<void> Open(StringView filePath)
        {
            mFileStream = new FileStream();
            if (mFileStream.Open(filePath, .ReadWrite, .Read) case .Err)
            {
                // Fall back to a read-only view
                mReadOnly = true;
                if (mFileStream.Open(filePath, .Read, .ReadWrite) case .Err)
                {
                    DeleteAndNullify!(mFileStream);
                    return .Err;
                }
            }
            mFileSize = (int)mFileStream.Length;
            return .Ok;
        }

        public void Close()
        {
            DeleteAndNullify!(mFileStream);
        }

        public uint8[] ReadBinaryData(int offset, int32 size)
        {
            uint8[] result = new uint8[size]; // Zero-initialized; anything past EOF stays zero
            if ((mFileStream != null) && (offset < mFileSize) && (size > 0))
            {
                int readLen = Math.Min((int)size, mFileSize - offset);
                if (readLen > 0)
                {
                    mFileStream.Seek(offset).IgnoreError();
                    mFileStream.TryRead(.(result.CArray(), readLen)).IgnoreError();
                }
            }
            return result;
        }

        public void WriteBinaryData(uint8[] data, int offset)
        {
            if ((mReadOnly) || (mFileStream == null))
                return;

            int writeLen = data.Count;
            if (offset + writeLen > mFileSize)
                writeLen = mFileSize - offset; // Never grow the file
            if (writeLen <= 0)
                return;

            mFileStream.Seek(offset).IgnoreError();
            mFileStream.TryWrite(.(data.CArray(), writeLen)).IgnoreError();
        }
    }

    /// Simple dialog for jumping to an integer byte offset (decimal, or hex with a '0x' prefix)
    class GoToBinaryOffsetDialog : DarkDialog
    {
        BinaryDataPanel mPanel;
        EditWidget mEditWidget;

        public this() : base("Go To Offset", "Offset", null)
        {
        }

        public void Init(BinaryDataPanel panel)
        {
            mPanel = panel;
            mDefaultButton = AddButton("OK", new (evt) => Submit());
            mEscButton = AddButton("Cancel", new (evt) => {});
            mEditWidget = AddEdit("");
        }

        void Submit()
        {
            var text = scope String();
            mDialogEditWidget.GetText(text);
            text.Trim();

            int offset = 0;
            bool parsed = false;
            if (text.StartsWith("0x", .OrdinalIgnoreCase))
            {
                if (int64.Parse(scope String(text, 2), .HexNumber) case .Ok(let val))
                {
                    offset = (int)val;
                    parsed = true;
                }
            }
            else if (int64.Parse(text) case .Ok(let val))
            {
                offset = (int)val;
                parsed = true;
            }

            if (!parsed)
            {
                IDEApp.Beep(.Error);
                return;
            }

            mPanel.GotoOffset(offset);
        }
    }

    /// Panel for viewing and editing the raw contents of a binary file
    public class BinaryDataPanel : ContentPanel
    {
        public BinaryDataWidget mBinaryDataWidget;
        public BinaryFileContentProvider mProvider ~ delete _;
        // mFilePath is declared in ContentPanel

        public this()
        {
        }

        public bool Show(StringView filePath)
        {
            BinaryFileContentProvider provider = new BinaryFileContentProvider();
            if (provider.Open(filePath) case .Err)
            {
                delete provider;
                return false;
            }

            mProvider = provider;
            mFilePath = new String(filePath);
            IDEUtils.FixFilePath(mFilePath);

            mBinaryDataWidget = new BinaryDataWidget(mProvider);
            mBinaryDataWidget.SetFileMode(mProvider.mFileSize);
            mBinaryDataWidget.mIsFillWidget = true;
            AddWidget(mBinaryDataWidget);
            mBinaryDataWidget.UpdateScrollbar();

            return true;
        }

        public void ShowGotoOffset()
        {
            GoToBinaryOffsetDialog dialog = new GoToBinaryOffsetDialog();
            dialog.Init(this);
            dialog.PopupWindow(mWidgetWindow);
        }

        public void GotoOffset(int offset)
        {
            if (mBinaryDataWidget == null)
                return;

            int useOffset = offset;
            if (mProvider.mFileSize > 0)
                useOffset = Math.Min(Math.Max(offset, 0), mProvider.mFileSize - 1);
            else
                useOffset = 0;

            mBinaryDataWidget.SelectRange(useOffset, 1);
            mBinaryDataWidget.SetFocus();
        }

        public override bool FileNameMatches(String fileName)
        {
            if (mFilePath == null)
                return false;
            return Path.Equals(fileName, mFilePath);
        }

        public override bool HasUnsavedChanges()
        {
            return (mBinaryDataWidget != null) && (mBinaryDataWidget.HasUnsavedChanges());
        }

        /// Commit pending edits to the file
        public bool Save()
        {
            if (mBinaryDataWidget != null)
                mBinaryDataWidget.Save();
            return true;
        }

        /// Discard pending edits without saving
        public void RevertChanges()
        {
            if (mBinaryDataWidget != null)
                mBinaryDataWidget.DiscardChanges();
        }

        public override void SetFocus()
        {
            base.SetFocus();
            if (mBinaryDataWidget != null)
                mBinaryDataWidget.SetFocus();
        }

        public override void Dispose()
        {
            if (mProvider != null)
                mProvider.Close();
            base.Dispose();
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            if (mBinaryDataWidget != null)
                mBinaryDataWidget.Resize(0, 0, mWidth, mHeight);
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);
            if (mFilePath == null)
                return;

            data.Add("Type", "BinaryDataPanel");
            String relPath = scope String();
            gApp.mWorkspace.GetWorkspaceRelativePath(mFilePath, relPath);
            data.Add("FilePath", relPath);
        }

        public override bool Deserialize(StructuredData data)
        {
            if (!base.Deserialize(data))
                return false;

            String relFilePath = scope String();
            data.GetString("FilePath", relFilePath);
            String absFilePath = scope String();
            gApp.mWorkspace.GetWorkspaceAbsPath(relFilePath, absFilePath);

            return Show(absFilePath);
        }
    }
}
