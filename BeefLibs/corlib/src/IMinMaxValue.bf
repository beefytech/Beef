namespace System
{
    interface IMinMaxValue<T>
    {
        public static T MinValue { get; }
        public static T MaxValue { get; }
    }
}
