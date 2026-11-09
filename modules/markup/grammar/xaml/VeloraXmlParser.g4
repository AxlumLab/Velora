// XAML XML structure (revised from the v3 package grammar).
parser grammar VeloraXmlParser;
options { tokenVocab = VeloraXmlLexer; }

document : misc* element misc* EOF;
misc     : XML_TEXT | XML_COMMENT | XML_PI | XML_DOCTYPE;
element
    : XML_OPEN XML_NAME attribute* (
        XML_EMPTY_CLOSE
      | XML_TAG_CLOSE content* XML_OPEN_END XML_NAME XML_TAG_CLOSE
      )
    ;
attribute : XML_NAME XML_EQ XML_STRING;
content   : element | XML_TEXT | XML_CDATA | XML_COMMENT | XML_PI | XML_DOCTYPE;
