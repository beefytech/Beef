namespace System
{
	public delegate void EventHandler(Object sender, EventArgs e);
	public delegate void EventHandler<TEventArgs>(Object sender, TEventArgs e); // Removed TEventArgs constraint post-.NET 4
}
