using System;
using System.Collections;
using System.Text;
using System.Reflection;
using Beefy.gfx;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.events;
using Beefy;
using Beefy.utils;
//using System.Windows.Forms;

namespace IDE.ui
{    
    public class PropertiesPanel : Panel
    {
		public override String SerializationType
		{
			get { return "PropertiesPanel"; }
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "PropertiesPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }
    }
}
