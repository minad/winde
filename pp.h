#define EMPTY()
#define _CAT(a, b)         a ## b
#define CAT(a, b)          _CAT(a, b)
#define __SECOND(a, b)     b
#define _SECOND(tuple)     __SECOND tuple
#define _IS_EMPTY(arg)     _SECOND((_ ## arg()))
#define __IS_EMPTY_HELPER  ,1 EMPTY
#define _IS_EMPTY_HELPER() ,0
#define IS_EMPTY(arg)      _IS_EMPTY(arg _IS_EMPTY_HELPER)
#define _IF1(a,b)          a
#define _IF0(a,b)          b
#define IF(cond, a, b)     CAT(_IF, cond)(a,b)
#define IF_EMPTY(c, a, b)  IF(IS_EMPTY(c), a, b)
#define REMOVE_PARENS(...) __VA_ARGS__
#define STRINGIZE(s)       #s
