namespace System.Diagnostics.Contracts
{
    static class Contract
    {
        public enum ContractFailureKind
        {
            Precondition,
	        //[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]
            Postcondition,
	        //[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]
            PostconditionOnException,
            Invariant,
            Assert,
            Assume,
        }
        
        static extern void ReportFailure(ContractFailureKind failureKind, char8* userMessage, int32 userMessageLen, char8* conditionText, int32 conditionTextLen);

        /// <summary>
        /// This method is used internally to trigger a failure indicating to the "programmer" that he is using the interface incorrectly.
        /// It is NEVER used to indicate failure of actual contracts at runtime.
        /// </summary>
        static void AssertMustUseRewriter(ContractFailureKind kind, String contractKind)
        {
        }
        
        public static void Assume(bool condition, StringView userMessage)
        {
            if (!condition)
            {
                ReportFailure(ContractFailureKind.Assume, userMessage.Ptr, (int32)userMessage.Length, null, 0);
            }
        }
        
        public static void Assert(bool condition)
        {
            if (!condition)
                ReportFailure(ContractFailureKind.Assert, null, 0, null, 0);
        }
        
        public static void Assert(bool condition, String userMessage)
        {
            if (!condition)
                ReportFailure(ContractFailureKind.Assert, userMessage.Ptr, (int32)userMessage.Length, null, 0);
        }
        
        public static void Requires(bool condition)
        {
			if (!condition)
				ReportFailure(ContractFailureKind.Assert, null, 0, null, 0);
            //AssertMustUseRewriter(ContractFailureKind.Precondition, "Requires");
        }
        
        public static void Requires(bool condition, StringView userMessage)
        {
            AssertMustUseRewriter(ContractFailureKind.Precondition, "Requires");
        }
        
        public static void EndContractBlock() { }
    }
}
