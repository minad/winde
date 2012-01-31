#define _NOTHING()
#define __SECOND(a, b)     b
#define _CONCAT(a, b)      a ## b
#define _SECOND(tuple)     __SECOND tuple
#define _IS_EMPTY(arg)     _SECOND((_ ## arg()))
#define __IS_EMPTY_HELPER  ,1 _NOTHING
#define _IS_EMPTY_HELPER() ,0
#define IS_EMPTY(arg)      _IS_EMPTY(arg _IS_EMPTY_HELPER)
#define _IF1(a,b)          a
#define _IF0(a,b)          b
#define IF(cond, a, b)     _CONCAT(_IF, cond)(a,b)
#define IF_EMPTY(c, a, b)  IF(IS_EMPTY(c), a, b)
#define REMOVE_PARENS(...) __VA_ARGS__
#define STRING(s)          #s
