// Restricted C++ declaration grammar for reflection. It structures reflected
// declarations (rkclass/rkstruct/rkenum and their rkfield/rkfunc/rkctor members)
// and skips everything else as balanced token groups. Semantic interpretation of
// declarators (names, types, constructors) happens in the C++ visitor.
parser grammar VeloraReflectParser;

options { tokenVocab = VeloraReflectLexer; }

@parser::members {
// A member-level macro invocation such as R_OBJECT(Type) written without a trailing ';'.
bool isMemberMacro() {
    auto* id = _input->LT(1);
    if (id->getType() != ID || _input->LT(2)->getType() != LPAREN) return false;
    bool letter = false;
    for (char c : id->getText()) {
        if (c >= 'a' && c <= 'z') return false;
        letter = letter || (c >= 'A' && c <= 'Z');
    }
    if (!letter) return false;
    int depth = 0;
    for (size_t k = 2;; ++k) {
        const size_t type = _input->LT(static_cast<ssize_t>(k))->getType();
        if (type == antlr4::Token::EOF) return false;
        if (type == LPAREN) ++depth;
        else if (type == RPAREN && --depth == 0) return _input->LT(static_cast<ssize_t>(k + 1))->getType() != SEMI;
    }
}
}

translationUnit: declaration* EOF;

declaration
    : namespaceDecl
    | templateDecl
    | attributeDecl
    | reflectedType SEMI?
    | {isMemberMacro()}? macroInvocation
    | otherDecl
    | SEMI
    ;

namespaceDecl: INLINE? NAMESPACE qualifiedName? LBRACE declaration* RBRACE;
qualifiedName: ID (SCOPE INLINE? ID)*;

templateDecl: TEMPLATE angleGroup (reflectedType SEMI? | otherDecl | templateDecl);
attributeDecl: RKATTR parenGroup SEMI?;
macroInvocation: ID parenGroup;
otherDecl: otherFirst otherToken* (SEMI | braceGroup SEMI?);
// 'inline namespace' also matches otherDecl; namespaceDecl wins as the earlier alternative.
otherFirst: ~(SEMI | LBRACE | RBRACE | NAMESPACE | TEMPLATE | RKATTR | RKCLASS | RKSTRUCT | RKENUM);
otherToken: parenGroup | ~(SEMI | LBRACE | RBRACE | LPAREN | RPAREN | RKCLASS | RKSTRUCT | RKENUM);

reflectedType: classDecl | enumDecl;

classDecl: (RKCLASS | RKSTRUCT) classHead (COLON baseSpecifier (COMMA baseSpecifier)*)? LBRACE member* RBRACE;
classHead: (ID | parenGroup)+;
baseSpecifier: (PUBLIC | PROTECTED | PRIVATE | ID | SCOPE | angleGroup)+;

enumDecl: RKENUM (CLASS | STRUCT)? ID (COLON enumBase)? LBRACE enumerators? RBRACE;
enumBase: (ID | SCOPE)+;
enumerators: enumerator (COMMA enumerator)* COMMA?;
enumerator: attributeDecl? ID (ASSIGN expression)?;
expression: (parenGroup | ~(COMMA | RBRACE | LBRACE | LPAREN | RPAREN | SEMI))+;

member
    : accessLabel
    | propertyDecl
    | eventDecl
    | attributeDecl
    | reflectedType SEMI?
    | fieldDecl
    | functionDecl
    | constructorDecl
    | {isMemberMacro()}? macroInvocation
    | memberDecl
    | SEMI
    ;

accessLabel: (PUBLIC | PRIVATE | PROTECTED) COLON;

// rkproperty(Name, Type, get=fn, set=fn, default=value, Flag...) and rkevent(Name, Signature, add=fn).
propertyDecl: RKPROPERTY LPAREN metaItem (COMMA metaItem)* RPAREN SEMI?;
eventDecl: RKEVENT LPAREN metaItem (COMMA metaItem)* RPAREN SEMI?;

fieldDecl: STATIC* RKFIELD fieldMeta? declarator (COMMA declarator)* SEMI;
fieldMeta: LPAREN metaItem? (COMMA metaItem?)* RPAREN;
metaItem: (parenGroup | ~(COMMA | LPAREN | RPAREN))+;
declarator: declToken+ (ASSIGN initializer | braceGroup)?;
initializer: (braceGroup | parenGroup | ~(SEMI | LBRACE | RBRACE | COMMA | LPAREN | RPAREN))+;

functionDecl: STATIC* RKFUNC parenGroup? signature parameters functionTail;
constructorDecl: RKCTOR (ID | INLINE)+ parameters functionTail;
signature: declToken+;
parameters: LPAREN (parameter (COMMA parameter)*)? RPAREN;
parameter: paramToken+ (ASSIGN defaultValue)?;
paramToken: ID | SCOPE | STAR | AMP | ANDAND | CLASS | STRUCT | ELLIPSIS | angleGroup | bracketGroup;
defaultValue: (parenGroup | braceGroup | angleGroup | ~(COMMA | LPAREN | RPAREN | LBRACE | RBRACE | LT | GT))+;
functionTail: tailToken* (SEMI | braceGroup SEMI? | ASSIGN tailToken* SEMI);
tailToken: parenGroup | ~(SEMI | LBRACE | RBRACE | ASSIGN | LPAREN | RPAREN);

// Unreflected members, and constructors/implicit struct fields classified by the visitor.
memberDecl: memberFirst memberToken* (SEMI | braceGroup SEMI?);
memberFirst: ~(SEMI | LBRACE | RBRACE | LPAREN | RPAREN | RKFIELD | RKFUNC | RKCTOR | RKATTR | RKPROPERTY | RKEVENT | RKCLASS | RKSTRUCT | RKENUM | PUBLIC | PRIVATE | PROTECTED);
// Parenthesized groups may contain braces (default arguments such as `= {}`).
memberToken: parenGroup | ~(SEMI | LBRACE | RBRACE | LPAREN | RPAREN | RKFIELD | RKFUNC | RKCTOR | RKATTR | RKPROPERTY | RKEVENT | RKCLASS | RKSTRUCT | RKENUM);

declToken: ID | SCOPE | STAR | AMP | ANDAND | STATIC | INLINE | CLASS | STRUCT | NUMBER | COLON | angleGroup | bracketGroup;

parenGroup: LPAREN (parenGroup | ~(LPAREN | RPAREN))* RPAREN;
braceGroup: LBRACE (braceGroup | ~(LBRACE | RBRACE))* RBRACE;
bracketGroup: LBRACKET (bracketGroup | ~(LBRACKET | RBRACKET))* RBRACKET;
angleGroup: LT (angleGroup | ~(LT | GT | SEMI | LBRACE | RBRACE))* GT;
