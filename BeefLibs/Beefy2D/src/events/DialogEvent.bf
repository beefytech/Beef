using System;
using System.Collections;
using System.Text;
using Beefy.widgets;

namespace Beefy.events
{
    public class DialogEvent : Event
    {
        public bool mCloseDialog;
        public ButtonWidget mButton;
        public String mResult;
    }
}
