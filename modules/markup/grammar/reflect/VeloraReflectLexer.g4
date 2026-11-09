// Tokens for the restricted C++ declaration subset read by the reflection compiler.
// Preprocessor directives are evaluated before lexing (see antlr_frontend.cpp); any
// directive that survives is skipped here.
lexer grammar VeloraReflectLexer;

// Reflection markers; the rk* and c* spellings are equivalent.
RKCLASS: 'rkclass' | 'cclass';
RKSTRUCT: 'rkstruct' | 'cstruct';
RKENUM: 'rkenum' | 'cenum';
RKFIELD: 'rkfield' | 'cfield';
RKFUNC: 'rkfunc' | 'cfunc';
RKCTOR: 'rkctor' | 'cctor';
RKATTR: 'rkattr';
RKPROPERTY: 'rkproperty';
RKEVENT: 'rkevent';

NAMESPACE: 'namespace';
TEMPLATE: 'template';
CLASS: 'class';
STRUCT: 'struct';
PUBLIC: 'public';
PRIVATE: 'private';
PROTECTED: 'protected';
STATIC: 'static';
INLINE: 'inline';

ID: [a-zA-Z_] [a-zA-Z_0-9]*;
NUMBER: '0' [xX] [0-9a-fA-F]+ [a-zA-Z_]* | [0-9] [0-9.]* [a-zA-Z_]*;
STRING: '"' (~["\\\r\n] | '\\' .)* '"';
CHARACTER: '\'' (~['\\\r\n] | '\\' .)* '\'';

SCOPE: '::';
ARROW: '->';
ANDAND: '&&';
OROR: '||';
ELLIPSIS: '...';
COLON: ':';
SEMI: ';';
COMMA: ',';
LPAREN: '(';
RPAREN: ')';
LBRACE: '{';
RBRACE: '}';
LBRACKET: '[';
RBRACKET: ']';
LT: '<';
GT: '>';
ASSIGN: '=';
STAR: '*';
AMP: '&';
PIPE: '|';
OP: [+\-./!~?%^];

BLOCK_COMMENT: '/*' .*? '*/' -> skip;
LINE_COMMENT: '//' ~[\r\n]* -> skip;
DIRECTIVE: '#' ~[\r\n]* -> skip;
WS: [ \t\r\n\f]+ -> skip;
