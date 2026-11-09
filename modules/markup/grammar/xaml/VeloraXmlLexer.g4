// XAML XML tokens (revised from the v3 package grammar). Comments, CDATA, processing
// instructions and DOCTYPE are distinct tokens so the semantic pass can keep comments and
// report precise errors; DTDs and external entities are rejected there, never processed.
lexer grammar VeloraXmlLexer;

XML_COMMENT : '<!--' .*? '-->';
XML_CDATA   : '<![CDATA[' .*? ']]>';
XML_DOCTYPE : '<!DOCTYPE' ~[>[]* ('[' .*? ']')? ~[>]* '>';
XML_PI      : '<?' .*? '?>';
XML_OPEN_END: '</' -> pushMode(IN_TAG);
XML_OPEN    : '<'  -> pushMode(IN_TAG);
XML_TEXT    : ~[<]+;

mode IN_TAG;
XML_EMPTY_CLOSE : '/>' -> popMode;
XML_TAG_CLOSE   : '>'  -> popMode;
XML_EQ          : '=';
XML_NAME        : NAME_START NAME_CHAR*;
XML_STRING      : '"' ~["<]* '"' | '\'' ~['<]* '\'';
XML_WS          : [ \t\r\n]+ -> skip;

fragment NAME_START: [a-zA-Z_:] | [À-ÖØ-öø-˿Ͱ-ͽͿ-῿‌-‍⁰-↏Ⰰ-⿯、-퟿豈-﷏ﷰ-�] | [\u{10000}-\u{EFFFF}];
fragment NAME_CHAR : NAME_START | [0-9.\-·̀-ͯ‿-⁀];
