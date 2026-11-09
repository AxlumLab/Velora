// XAML markup extensions (revised from the v3 package grammar). The grammar accepts the
// structural form; the semantic pass restricts extensions and argument names. Values keep
// their source spelling (the builder slices the source between the first and last token).
grammar VeloraMarkup;

root         : extension EOF;
extension    : LBRACE IDENT argumentList? RBRACE;
argumentList : positional (COMMA namedArgument)*
             | namedArgument (COMMA namedArgument)*
             ;
positional   : value;
namedArgument: IDENT EQ value;
value        : extension | scalar;
scalar       : (parenGroup | scalarPart)+;
// Parentheses group x:Bind function arguments so their commas do not split arguments.
parenGroup   : LPAREN (parenGroup | extension | COMMA | EQ | scalarPart)* RPAREN;
scalarPart   : IDENT | INT | STRING | ESCAPED | DOT | OTHER;

LBRACE : '{';
RBRACE : '}';
LPAREN : '(';
RPAREN : ')';
COMMA  : ',';
EQ     : '=';
DOT    : '.';
INT    : [0-9]+;
IDENT  : [a-zA-Z_] [a-zA-Z0-9_:]*;
// Single-quoted strings; a backslash escapes the next character.
STRING : '\'' ('\\' . | ~['\\])* '\'';
ESCAPED: '\\' .;
WS     : [ \t\r\n]+ -> skip;
OTHER  : ~[{}(),=.'\\ \t\r\n];
