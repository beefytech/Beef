namespace System;

struct Guid : IHashable, IParseable<Guid>
{
	public static readonly Guid Empty = Guid();

	private uint32 mA;
	private uint16 mB;
	private uint16 mC;
	private uint8 mD;
	private uint8 mE;
	private uint8 mF;
	private uint8 mG;
	private uint8 mH;
	private uint8 mI;
	private uint8 mJ;
	private uint8 mK;

	public this()
	{
		this = default;
	}

	public this(uint32 a, uint16 b, uint16 c, uint8 d, uint8 e, uint8 f, uint8 g, uint8 h, uint8 i, uint8 j, uint8 k)
	{
		mA = a;
		mB = b;
		mC = c;
		mD = d;
		mE = e;
		mF = f;
		mG = g;
		mH = h;
		mI = i;
		mJ = j;
		mK = k;
	}

	public int GetHashCode()
	{
		int hash1 = (int)mA;
		int hash2 = ((int)mB << 16) | (int)mC;
		int hash3 = ((int)mD << 24) | ((int)mE << 16) | ((int)mF << 8) | (int)mG;
		int hash4 = ((int)mH << 24) | ((int)mI << 16) | ((int)mJ << 8) | (int)mK;

		return hash1 ^ hash2 ^ hash3 ^ hash4;
	}

	[Commutable]
	public static bool operator ==(Guid val1, Guid val2)
	{
		return
			(val1.mA == val2.mA) &&
			(val1.mB == val2.mB) &&
			(val1.mC == val2.mC) &&
			(val1.mD == val2.mD) &&
			(val1.mE == val2.mE) &&
			(val1.mF == val2.mF) &&
			(val1.mG == val2.mG) &&
			(val1.mH == val2.mH) &&
			(val1.mI == val2.mI) &&
			(val1.mJ == val2.mJ) &&
			(val1.mK == val2.mK);
	}

	public static Guid Create()
	{
		Guid guid = ?;
		Platform.BfpSystem_CreateGUID(&guid);
		return guid;
	}

	public void ToString(String strBuffer, char8 format = 'D')
	{
	    switch(format)
	    {
	    case 'N', 'n':
	        // 32 digits: 00000000000000000000000000000000
	        strBuffer.AppendF("{0,8:x8}{1,4:x4}{2,4:x4}{3,2:x2}{4,2:x2}{5,2:x2}{6,2:x2}{7,2:x2}{8,2:x2}{9,2:x2}{10,2:x2}", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	        
	    case 'D', 'd':
	        // 32 digits separated by hyphens: 00000000-0000-0000-0000-000000000000
	        strBuffer.AppendF("{0,8:x8}-{1,4:x4}-{2,4:x4}-{3,2:x2}{4,2:x2}-{5,2:x2}{6,2:x2}{7,2:x2}{8,2:x2}{9,2:x2}{10,2:x2}", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	        
	    case 'B', 'b':
	        // 32 digits separated by hyphens, enclosed in braces: {00000000-0000-0000-0000-000000000000}
	        strBuffer.AppendF("{{{0,8:x8}-{1,4:x4}-{2,4:x4}-{3,2:x2}{4,2:x2}-{5,2:x2}{6,2:x2}{7,2:x2}{8,2:x2}{9,2:x2}{10,2:x2}}}", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	        
	    case 'P', 'p':
	        // 32 digits separated by hyphens, enclosed in parentheses: (00000000-0000-0000-0000-000000000000)
	        strBuffer.AppendF("({0,8:x8}-{1,4:x4}-{2,4:x4}-{3,2:x2}{4,2:x2}-{5,2:x2}{6,2:x2}{7,2:x2}{8,2:x2}{9,2:x2}{10,2:x2})", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	        
	    case 'X', 'x':
	        // Four hexadecimal values enclosed in braces: {0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
	        strBuffer.AppendF("{{0x{0,8:x8},0x{1,4:x4},0x{2,4:x4},{{0x{3,2:x2},0x{4,2:x2},0x{5,2:x2},0x{6,2:x2},0x{7,2:x2},0x{8,2:x2},0x{9,2:x2},0x{10,2:x2}}}}}", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	        
	    default:
	        // Default to 'D' format
	        strBuffer.AppendF("{0,8:x8}-{1,4:x4}-{2,4:x4}-{3,2:x2}{4,2:x2}-{5,2:x2}{6,2:x2}{7,2:x2}{8,2:x2}{9,2:x2}{10,2:x2}", 
	            mA, mB, mC, mD, mE, mF, mG, mH, mI, mJ, mK);
	    }
	}

	public override void ToString(String strBuffer)
	{
	    ToString(strBuffer, 'D');
	}

	public static Result<Guid> Parse(StringView val)
	{
		var val;
	    if (val.IsEmpty)
	        return .Err;

		String str = scope .(val);
		str.Trim();
	    
	    // Determine format based on string characteristics
	    if (str.StartsWith("{0x") && str.EndsWith("}}"))
	    {
	        // X format: {0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
	        return ParseXFormat(str);
	    }
	    else if (str.StartsWith("{") && str.EndsWith("}"))
	    {
	        // B format: {00000000-0000-0000-0000-000000000000}
	        return ParseDashFormat(str.Substring(1, str.Length - 2));
	    }
	    else if (str.StartsWith("(") && str.EndsWith(")"))
	    {
	        // P format: (00000000-0000-0000-0000-000000000000)
	        return ParseDashFormat(str.Substring(1, str.Length - 2));
	    }
	    else if (str.Contains('-'))
	    {
	        // D format: 00000000-0000-0000-0000-000000000000
	        return ParseDashFormat(str);
	    }
	    else if (str.Length == 32)
	    {
	        // N format: 00000000000000000000000000000000
	        return ParseNFormat(str);
	    }
	    
	    return .Err;
	}

	private static Result<Guid> ParseDashFormat(StringView val)
	{
	    // Expected format: 00000000-0000-0000-0000-000000000000
	    StringView[5] parts = ?;
	    int partCount = 0;
	    
	    for (var part in val.Split('-'))
	    {
	        if (partCount >= 5)
	            return .Err;
	        parts[partCount] = part;
	        partCount++;
	    }
	    
	    if (partCount != 5)
	        return .Err;
	    
	    if (parts[0].Length != 8 || parts[1].Length != 4 || parts[2].Length != 4 || 
	        parts[3].Length != 4 || parts[4].Length != 12)
	        return .Err;
	    
	    // Parse each part
	    if (uint32.Parse(parts[0], .HexNumber) case .Ok(let a) &&
	        uint16.Parse(parts[1], .HexNumber) case .Ok(let b) &&
	        uint16.Parse(parts[2], .HexNumber) case .Ok(let c))
	    {
	        // Parse the 8 bytes from parts[3] and parts[4]
	        String part3and4 = scope String()..Append(parts[3])..Append(parts[4]);
	        
	        if (part3and4.Length != 16)
	            return .Err;
	        
	        uint8[8] bytes = ?;
	        for (int i = 0; i < 8; i++)
	        {
	            StringView byteStr = part3and4.Substring(i * 2, 2);
	            if (uint8.Parse(byteStr, .HexNumber) case .Ok(let byteVal))
	                bytes[i] = byteVal;
	            else
	                return .Err;
	        }
	        
	        return .Ok(Guid(a, b, c, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]));
	    }
	    
	    return .Err;
	}

	private static Result<Guid> ParseNFormat(StringView val)
	{
	    // Expected format: 00000000000000000000000000000000 (32 hex chars)
	    if (val.Length != 32)
	        return .Err;
	    
	    // Parse as: aaaaaaaa bbbb cccc ddee ffgghhiijjkk
	    if (uint32.Parse(val.Substring(0, 8), .HexNumber) case .Ok(let a) &&
	        uint16.Parse(val.Substring(8, 4), .HexNumber) case .Ok(let b) &&
	        uint16.Parse(val.Substring(12, 4), .HexNumber) case .Ok(let c))
	    {
	        uint8[8] bytes = ?;
	        for (int i = 0; i < 8; i++)
	        {
	            StringView byteStr = val.Substring(16 + i * 2, 2);
	            if (uint8.Parse(byteStr, .HexNumber) case .Ok(let byteVal))
	                bytes[i] = byteVal;
	            else
	                return .Err;
	        }
	        
	        return .Ok(Guid(a, b, c, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]));
	    }
	    
	    return .Err;
	}

	private static Result<Guid> ParseXFormat(StringView val)
	{
	    // Expected format: {0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
	    StringView inner = val.Substring(1, val.Length - 2); // Remove outer braces
	    
	    if (!inner.StartsWith("0x"))
	        return .Err;
	    
	    // Find the inner brace section
	    int innerBraceStart = inner.IndexOf('{');
	    if (innerBraceStart == -1)
	        return .Err;
	    
	    int innerBraceEnd = inner.LastIndexOf('}');
	    if (innerBraceEnd == -1 || innerBraceEnd <= innerBraceStart)
	        return .Err;
	    
	    // Parse the first three parts before the inner braces
	    StringView firstPart = inner.Substring(0, innerBraceStart - 1); // Remove trailing comma
	    StringView[3] mainParts = ?;
	    int mainPartCount = 0;
	    
	    for (var part in firstPart.Split(','))
	    {
	        if (mainPartCount >= 3)
	            return .Err;
	        mainParts[mainPartCount] = part;
	        mainPartCount++;
	    }
	    
	    if (mainPartCount != 3)
	        return .Err;
	    
	    // Parse main parts (remove 0x prefix)
	    if (uint32.Parse(mainParts[0].Substring(2), .HexNumber) case .Ok(let a) &&
	        uint16.Parse(mainParts[1].Substring(2), .HexNumber) case .Ok(let b) &&
	        uint16.Parse(mainParts[2].Substring(2), .HexNumber) case .Ok(let c))
	    {
	        // Parse the 8 bytes in the inner braces
	        StringView innerBytes = inner.Substring(innerBraceStart + 1, innerBraceEnd - innerBraceStart - 1);
	        StringView[8] byteParts = ?;
	        int bytePartCount = 0;
	        
	        for (var part in innerBytes.Split(','))
	        {
	            if (bytePartCount >= 8)
	                return .Err;
	            byteParts[bytePartCount] = scope :: String(part)..Trim();
	            bytePartCount++;
	        }
	        
	        if (bytePartCount != 8)
	            return .Err;
	        
	        uint8[8] bytes = ?;
	        for (int i = 0; i < 8; i++)
	        {
	            StringView bytePart = byteParts[i];
	            if (!bytePart.StartsWith("0x"))
	                return .Err;
	                
	            if (uint8.Parse(bytePart.Substring(2), .HexNumber) case .Ok(let byteVal))
	                bytes[i] = byteVal;
	            else
	                return .Err;
	        }
	        
	        return .Ok(Guid(a, b, c, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]));
	    }
	    
	    return .Err;
	}
}

#if TEST
class GUIDTests
{
    // Test GUID for consistent testing
    private static Guid GetTestGUID()
    {
        return Guid(0x550e8400, 0xe29b, 0x41d4, 0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00);
    }

    [Test]
    public static void Constructor()
    {
        var guid = Guid(0x12345678, 0x1234, 0x5678, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);
        
        // Test that we can access the values (indirectly through ToString)
        String str = scope String();
        guid.ToString(str, 'N');
        Test.Assert(str == "1234567812345678123456789abcdef0");
    }

    [Test]
    public static void EmptyGUID()
    {
        var empty = Guid.Empty;
        String str = scope String();
        empty.ToString(str, 'D');
        Test.Assert(str == "00000000-0000-0000-0000-000000000000");
    }

    [Test]
    public static void Equality()
    {
        var guid1 = GetTestGUID();
        var guid2 = GetTestGUID();
        var guid3 = Guid(0x12345678, 0x1234, 0x5678, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);

        Test.Assert(guid1 == guid2);
        Test.Assert(!(guid1 == guid3));
        Test.Assert(!(guid2 == guid3));
    }

    [Test]
    public static void HashCode()
    {
        var guid1 = GetTestGUID();
        var guid2 = GetTestGUID();
        var guid3 = Guid(0x12345678, 0x1234, 0x5678, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);

        // Same GUIDs should have same hash code
        Test.Assert(guid1.GetHashCode() == guid2.GetHashCode());
        
        // Different GUIDs should likely have different hash codes (not guaranteed, but very likely)
        Test.Assert(guid1.GetHashCode() != guid3.GetHashCode());
    }

    [Test]
    public static void ToString_NFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        guid.ToString(str, 'N');
        Test.Assert(str == "550e8400e29b41d4a716446655440000");
        
        str.Clear();
        guid.ToString(str, 'n');
        Test.Assert(str == "550e8400e29b41d4a716446655440000");
    }

    [Test]
    public static void ToString_DFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        guid.ToString(str, 'D');
        Test.Assert(str == "550e8400-e29b-41d4-a716-446655440000");
        
        str.Clear();
        guid.ToString(str, 'd');
        Test.Assert(str == "550e8400-e29b-41d4-a716-446655440000");
        
        // Test default format
        str.Clear();
        guid.ToString(str);
        Test.Assert(str == "550e8400-e29b-41d4-a716-446655440000");
    }

    [Test]
    public static void ToString_BFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        guid.ToString(str, 'B');
        Test.Assert(str == "{550e8400-e29b-41d4-a716-446655440000}");
        
        str.Clear();
        guid.ToString(str, 'b');
        Test.Assert(str == "{550e8400-e29b-41d4-a716-446655440000}");
    }

    [Test]
    public static void ToString_PFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        guid.ToString(str, 'P');
        Test.Assert(str == "(550e8400-e29b-41d4-a716-446655440000)");
        
        str.Clear();
        guid.ToString(str, 'p');
        Test.Assert(str == "(550e8400-e29b-41d4-a716-446655440000)");
    }

    [Test]
    public static void ToString_XFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        guid.ToString(str, 'X');
        Test.Assert(str == "{0x550e8400,0xe29b,0x41d4,{0xa7,0x16,0x44,0x66,0x55,0x44,0x00,0x00}}");
        
        str.Clear();
        guid.ToString(str, 'x');
        Test.Assert(str == "{0x550e8400,0xe29b,0x41d4,{0xa7,0x16,0x44,0x66,0x55,0x44,0x00,0x00}}");
    }

    [Test]
    public static void ToString_InvalidFormat()
    {
        var guid = GetTestGUID();
        String str = scope String();
        
        // Invalid format should default to 'D' format
        guid.ToString(str, 'Z');
        Test.Assert(str == "550e8400-e29b-41d4-a716-446655440000");
    }

    [Test]
    public static void Parse_DFormat()
    {
        String testStr = "550e8400-e29b-41d4-a716-446655440000";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void Parse_NFormat()
    {
        String testStr = "550e8400e29b41d4a716446655440000";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void Parse_BFormat()
    {
        String testStr = "{550e8400-e29b-41d4-a716-446655440000}";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void Parse_PFormat()
    {
        String testStr = "(550e8400-e29b-41d4-a716-446655440000)";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void Parse_XFormat()
    {
		//return GUID(0x550e8400, 0xe29b, 0x41d4, 0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00);
        String testStr = "{0x550e8400,0xe29b,0x41d4,{0xa7,0x16,0x44,0x66,0x55,0x44,0x00,0x00}}";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void Parse_EmptyString()
    {
        if (Guid.Parse("") case .Ok(let parsed))
        {
            Test.Assert(false); // Should have failed
        }
        else
        {
            // Expected to fail
            Test.Assert(true);
        }
    }

    [Test]
    public static void Parse_InvalidFormat()
    {
        String[] invalidStrings = scope .(
            "invalid",
            "123",
            "550e8400-e29b-41d4-a716", // Too short
            "550e8400-e29b-41d4-a716-446655440000-extra", // Too long
            "{550e8400-e29b-41d4-a716-446655440000", // Missing closing brace
            "550e8400-e29b-41d4-a716-446655440000}", // Missing opening brace
            "(550e8400-e29b-41d4-a716-446655440000", // Missing closing paren
            "550e8400-e29b-41d4-a716-446655440000)", // Missing opening paren
            "gggggggg-gggg-gggg-gggg-gggggggggggg", // Invalid hex characters
            "550e8400e29b41d4a716446655440000extra", // N format too long
            "550e8400e29b41d4a71644665544000", // N format too short
        );

        for (var invalidStr in invalidStrings)
        {
            if (Guid.Parse(invalidStr) case .Ok(let parsed))
            {
                Test.Assert(false); // Should have failed
            }
        }
    }

    [Test]
    public static void Parse_WithWhitespace()
    {
        String testStr = "  550e8400-e29b-41d4-a716-446655440000  ";
        
        if (Guid.Parse(testStr) case .Ok(let parsed))
        {
            var expected = GetTestGUID();
            Test.Assert(parsed == expected);
        }
        else
        {
            Test.Assert(false); // Parse should have succeeded
        }
    }

    [Test]
    public static void RoundTripTest_AllFormats()
    {
        var original = GetTestGUID();
        String str = scope String();
        
        // Test each format can round-trip
        char8[] formats = scope .('N', 'D', 'B', 'P', 'X');
        
        for (var format in formats)
        {
            str.Clear();
            original.ToString(str, format);
            
            if (Guid.Parse(str) case .Ok(let parsed))
            {
                Test.Assert(parsed == original);
            }
            else
            {
                Test.Assert(false); // Round-trip should work
            }
        }
    }

    [Test]
    public static void RoundTripTest_RandomGUIDs()
    {
        // Test with multiple different GUIDs
        Guid[] testGuids = scope .(
            Guid.Empty,
            GetTestGUID(),
            Guid(0xFFFFFFFF, 0xFFFF, 0xFFFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF),
            Guid(0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01),
            Guid(0x12345678, 0x9ABC, 0xDEF0, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0)
        );

        String str = scope String();
        
        for (var guid in testGuids)
        {
            str.Clear();
            guid.ToString(str, 'D');
            
            if (Guid.Parse(str) case .Ok(let parsed))
            {
                Test.Assert(parsed == guid);
            }
            else
            {
                Test.Assert(false);
            }
        }
    }

    [Test]
    public static void Create_GeneratesUniqueGUIDs()
    {
        // Generate several GUIDs and ensure they're different
        Guid[] guids = scope Guid[10];
        
        for (int i = 0; i < 10; i++)
        {
            guids[i] = Guid.Create();
        }
        
        // Check that all GUIDs are different from each other
        for (int i = 0; i < 10; i++)
        {
            for (int j = i + 1; j < 10; j++)
            {
                Test.Assert(!(guids[i] == guids[j]));
            }
        }
        
        // Check that none are empty
        for (int i = 0; i < 10; i++)
        {
            Test.Assert(!(guids[i] == Guid.Empty));
        }
    }

    [Test]
    public static void Parse_CaseInsensitive()
    {
        // Test that parsing is case insensitive for hex digits
        String lowerCase = "550e8400-e29b-41d4-a716-446655440000";
        String upperCase = "550E8400-E29B-41D4-A716-446655440000";
        String mixedCase = "550e8400-E29B-41d4-A716-446655440000";
        
        var expected = GetTestGUID();
        
        if (Guid.Parse(lowerCase) case .Ok(let parsed1))
        {
            Test.Assert(parsed1 == expected);
        }
        else
        {
            Test.Assert(false);
        }
        
        if (Guid.Parse(upperCase) case .Ok(let parsed2))
        {
            Test.Assert(parsed2 == expected);
        }
        else
        {
            Test.Assert(false);
        }
        
        if (Guid.Parse(mixedCase) case .Ok(let parsed3))
        {
            Test.Assert(parsed3 == expected);
        }
        else
        {
            Test.Assert(false);
        }
    }

    [Test]
    public static void ToString_ConsistentOutput()
    {
        var guid = GetTestGUID();
        String str1 = scope String();
        String str2 = scope String();
        
        // Multiple calls should produce identical output
        guid.ToString(str1, 'D');
        guid.ToString(str2, 'D');
        
        Test.Assert(str1 == str2);
    }
}
#endif