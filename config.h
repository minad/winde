#ifndef OUT
#  define OUT(name, port, bit, alias)
#endif
#ifndef IN
#  define IN(name, port, bit, alias)
#endif
#ifndef STATE
#  define STATE(name, attrs)
#endif
#ifndef ACTION
#  define ACTION(name, code)
#endif
#ifndef EVENT
#  define EVENT(name, condition)
#endif
#ifndef TRANS
#  define TRANS(initial, event, final, attrs)
#endif
#ifndef TRANS_ACTION
#  define TRANS_ACTION(initial, event, final, attrs, action)
#endif
#ifndef CMD
#  define CMD(name, fn, args, help)
#endif

#include "config"

#undef OUT
#undef IN
#undef OUT_ALIAS
#undef IN_ALIAS
#undef STATE
#undef ACTION
#undef EVENT
#undef TRANS
#undef TRANS_ACTION
#undef CMD
