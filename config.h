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
#ifndef TRANSITION
#  define TRANSITION(initial, event, final, attrs, action)
#endif
#ifndef COMMAND
#  define COMMAND(name, fn, args, help)
#endif

#include "config"

#undef OUT
#undef IN
#undef STATE
#undef ACTION
#undef EVENT
#undef TRANSITION
#undef COMMAND
