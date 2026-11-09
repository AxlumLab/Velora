// x:Bind expressions (revised from the v3 package grammar): a static member/index path or a
// function call whose arguments are paths or literals. No operators, no arbitrary C++.
grammar VeloraBind;

root      : expression EOF;
expression: call | path;
call      : path LPAREN (argument (COMMA argument)*)? RPAREN;
argument  : literal | path;
path      : NAME (DOT NAME | LBRACK index RBRACK)*;
index     : INT | STRING;
literal   : INT | FLOAT | STRING | TRUE | FALSE | XNULL;

TRUE  : 'true';
FALSE : 'false';
XNULL : 'x:Null';
DOT   : '.';
COMMA : ',';
LPAREN: '(';
RPAREN: ')';
LBRACK: '[';
RBRACK: ']';
FLOAT : [0-9]+ '.' [0-9]+;
INT   : [0-9]+;
NAME  : [a-zA-Z_] [a-zA-Z0-9_]*;
STRING: '\'' ('\\' . | ~['\\])* '\'' | '"' ('\\' . | ~["\\])* '"';
WS    : [ \t\r\n]+ -> skip;
