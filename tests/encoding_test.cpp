// UTF-8/UTF-16 conversion tests. Checks also execute in Release builds.
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <velora/text/Encoding.hpp>

using namespace velora;

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void checkConversion(std::string_view utf8, std::u16string_view utf16)
{
    check(utf8ToUtf16(utf8) == utf16, "UTF-8 converts to the expected UTF-16 code units");
    check(utf16ToUtf8(utf16) == utf8, "UTF-16 converts to the expected UTF-8 bytes");
    check(utf8ToUtf16Safe(utf8) == utf16, "safe UTF-8 conversion preserves valid input");
    check(utf16ToUtf8Safe(utf16) == utf8, "safe UTF-16 conversion preserves valid input");
}

void testBasicConversion()
{
    checkConversion("", u"");
    checkConversion("Hello, World!", u"Hello, World!");
    checkConversion("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C", u"\u4F60\u597D\u4E16\u754C");
    checkConversion("\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF",
                    u"\u3053\u3093\u306B\u3061\u306F");
    checkConversion("\xF0\x9F\x98\x80\xF0\x9F\x8E\x89\xF0\x9F\x9A\x80",
                    u"\U0001F600\U0001F389\U0001F680");
    checkConversion("Hello \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x8C\x8D",
                    u"Hello \u4E16\u754C \U0001F30D");
}

void testBoundariesAndEmbeddedNul()
{
    const std::string boundaries =
        "\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF"
        "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
    const std::u16string expected{0x007F, 0x0080, 0x07FF, 0x0800, 0xD7FF, 0xE000,
                                  0xFFFF, 0xD800, 0xDC00, 0xDBFF, 0xDFFF};
    checkConversion(boundaries, expected);
    const std::string withNul("A\0\xF0\x9F\x98\x80\0Z", 8);
    const std::u16string withNul16{u'A', u'\0', 0xD83D, 0xDE00, u'\0', u'Z'};
    checkConversion(withNul, withNul16);
}

void testInvalidUtf8()
{
    const std::array<std::string_view, 18> invalid{
        "\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xC2\x41",
        "\xE0\x80\x80", "\xE2\x82", "\xE2\x28\xA1", "\xED\xA0\x80", "\xED\xBF\xBF",
        "\xF0\x80\x80\x80", "\xF0\x9F\x98", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80",
        "\xFF", std::string_view("\xE2\0\xA1", 3), "A\xF0\x9F"};
    for (const auto input : invalid)
    {
        bool threw = false;
        try
        {
            utf8ToUtf16(input);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        check(threw, "malformed UTF-8 is rejected");
        check(utf8ToUtf16Safe(input).empty(), "safe conversion returns no partial malformed UTF-8");
    }
}

void testInvalidUtf16()
{
    const std::array<std::u16string, 10> invalid{
        std::u16string{0xD800}, std::u16string{0xDBFF}, std::u16string{0xDC00},
        std::u16string{0xDFFF}, std::u16string{0xD800, u'A'}, std::u16string{0xD800, 0xD800},
        std::u16string{0xDC00, 0xD800}, std::u16string{u'A', 0xDBFF},
        std::u16string{0xD800, 0}, std::u16string{u'A', 0xDC00}};
    for (const auto& input : invalid)
    {
        bool threw = false;
        try
        {
            utf16ToUtf8(input);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        check(threw, "unpaired or misordered UTF-16 surrogates are rejected");
        check(utf16ToUtf8Safe(input).empty(), "safe conversion returns no partial malformed UTF-16");
    }
}

void testLongStrings()
{
    std::string input;
    std::u16string expected;
    for (int i = 0; i < 10000; ++i)
    {
        input += "\xE6\xB5\x8B\xE8\xAF\x95";
        expected += u"\u6D4B\u8BD5";
    }
    checkConversion(input, expected);
}
} // namespace

int main()
{
    testBasicConversion();
    testBoundariesAndEmbeddedNul();
    testInvalidUtf8();
    testInvalidUtf16();
    testLongStrings();
    if (!failures)
        std::cout << "Encoding tests passed\n";
    return failures ? 1 : 0;
}
