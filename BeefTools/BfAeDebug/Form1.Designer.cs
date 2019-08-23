namespace BfAeDebug
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.mBfButton = new System.Windows.Forms.Button();
            this.mVsButton = new System.Windows.Forms.Button();
            this.mCancelButton = new System.Windows.Forms.Button();
            this.mLabel = new System.Windows.Forms.Label();
            this.SuspendLayout();
            // 
            // mBfButton
            // 
            this.mBfButton.Location = new System.Drawing.Point(12, 62);
            this.mBfButton.Name = "mBfButton";
            this.mBfButton.Size = new System.Drawing.Size(160, 32);
            this.mBfButton.TabIndex = 0;
            this.mBfButton.Text = "Attach Beef Debugger (d)";
            this.mBfButton.UseVisualStyleBackColor = true;
            this.mBfButton.Click += new System.EventHandler(this.button1_Click);
            // 
            // mVsButton
            // 
            this.mVsButton.Location = new System.Drawing.Point(178, 62);
            this.mVsButton.Name = "mVsButton";
            this.mVsButton.Size = new System.Drawing.Size(160, 32);
            this.mVsButton.TabIndex = 1;
            this.mVsButton.Text = "Attach Visual Studio";
            this.mVsButton.UseVisualStyleBackColor = true;
            this.mVsButton.Click += new System.EventHandler(this.mVsButton_Click);
            // 
            // mCancelButton
            // 
            this.mCancelButton.Location = new System.Drawing.Point(344, 62);
            this.mCancelButton.Name = "mCancelButton";
            this.mCancelButton.Size = new System.Drawing.Size(160, 32);
            this.mCancelButton.TabIndex = 2;
            this.mCancelButton.Text = "Cancel";
            this.mCancelButton.UseVisualStyleBackColor = true;
            this.mCancelButton.Click += new System.EventHandler(this.mCancelButton_Click);
            // 
            // mLabel
            // 
            this.mLabel.AutoSize = true;
            this.mLabel.Location = new System.Drawing.Point(12, 9);
            this.mLabel.Name = "mLabel";
            this.mLabel.Size = new System.Drawing.Size(87, 13);
            this.mLabel.TabIndex = 3;
            this.mLabel.Text = "Process Crashed";
            this.mLabel.Click += new System.EventHandler(this.label1_Click);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(518, 114);
            this.Controls.Add(this.mLabel);
            this.Controls.Add(this.mCancelButton);
            this.Controls.Add(this.mVsButton);
            this.Controls.Add(this.mBfButton);
            this.Name = "Form1";
            this.Text = "Attach Debugger ...";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button mBfButton;
        private System.Windows.Forms.Button mVsButton;
        private System.Windows.Forms.Button mCancelButton;
        private System.Windows.Forms.Label mLabel;
    }
}

