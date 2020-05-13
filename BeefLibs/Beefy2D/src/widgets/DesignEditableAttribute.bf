using System;
using System.Collections;
using System.Text;

namespace Beefy.widgets
{
    public struct DesignEditableAttribute : Attribute
    {
        public String DisplayType { get; set mut; }
        public String Group { get; set mut; }
        public String DisplayName { get; set mut; }
        public String SortName { get; set mut; }
        public bool DefaultEditString { get; set mut; }
    }
}
