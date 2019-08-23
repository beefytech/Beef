using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.widgets
{
    public struct DesignEditableAttribute : Attribute
    {
        public String DisplayType { get; set; }
        public String Group { get; set; }
        public String DisplayName { get; set; }
        public String SortName { get; set; }
        public bool DefaultEditString { get; set; }
    }
}
