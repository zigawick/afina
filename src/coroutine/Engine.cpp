#include <afina/coroutine/Engine.h>

#include <setjmp.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile char deep_point = 0;
    size_t depth = StackBottom - &deep_point;
    ctx.Stack.second = depth;

    if (ctx.Stack.first)
        delete[] ctx.Stack.first;
    ctx.Stack.first = new char[depth];

    std::copy(&deep_point, &deep_point + depth, ctx.Stack.first);
}

[[noreturn]] void Engine::Restore(context &ctx) {
    volatile char deep_point;

    if (&deep_point > StackBottom - ctx.Stack.second) { //< expand the stack so we can awoid the stack corruption
        Restore(ctx);
    }
    std::copy(ctx.Stack.first, ctx.Stack.first + ctx.Stack.second, StackBottom - ctx.Stack.second);

    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (alive) { // run another coroutine, if any
        context *to_call = alive;
        return sched(to_call);
    }
}

void Engine::sched(void *routine_) {
    context *to_call = static_cast<context *>(routine_);
    if (cur_routine) {
        Store(*cur_routine);
        if (setjmp(cur_routine->Environment)) {
            return;
        }
    }
    cur_routine = to_call;
    Restore(*to_call);
}

} // namespace Coroutine
} // namespace Afina
