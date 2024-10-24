#include <internal/platform.h>
#include <internal/unixcon.h>
#include <internal/linuxcon.h>
#include <internal/win32con.h>
#include <internal/ncurdisp.h>
#include <internal/ansiwrit.h>
#include <internal/ncursinp.h>
#include <internal/sighandl.h>
#include <internal/conctl.h>
#include <internal/termio.h>
#include <internal/getenv.h>

namespace tvision
{

// These methods are defined in a separate transaction unit so that the
// Platform can be referenced by the application without having to link all the
// console strategies.

ConsoleStrategy &Platform::createConsole() noexcept
{
#ifdef _WIN32
    return Win32ConsoleStrategy::create();
#else
    auto &con = ConsoleCtl::getInstance();
    InputState &inputState = *new InputState;
    NcursesDisplay &display = *new NcursesDisplay(con);
#ifdef __linux__
    if (con.isLinuxConsole())
        return LinuxConsoleStrategy::create(con, displayBuf, inputState, display, *new NcursesInput(con, display, inputState, false));
#endif // __linux__
    return UnixConsoleStrategy::create(con, displayBuf, inputState, display, *new NcursesInput(con, display, inputState, true));
#endif // _WIN32
}

void Platform::setUpConsole(ConsoleStrategy *&c) noexcept
{
    if (c == &dummyConsole)
    {
        c = &createConsole();
        SignalHandler::enable(signalCallback);
        for (auto *source : c->sources)
            if (source)
                waiter.addSource(*source);
    }
}

void Platform::checkConsole() noexcept
{
    console.lock([&] (ConsoleStrategy *&c) {
        if (!c->isAlive())
        {
            // The console likely crashed (Windows).
            restoreConsole(c);
            setUpConsole(c);
        }
    });
}

bool Platform::getEvent(TEvent &ev) noexcept
{
    if ( waiter.getEvent(ev)
         && (ev.what != evCommand || ev.message.command != cmScreenChanged) )
        return true;
    if (screenChanged())
    {
        ev.what = evCommand;
        ev.message.command = cmScreenChanged;
        return true;
    }
    return false;
}

void Platform::waitForEvents(int ms) noexcept
{
    checkConsole();

    int waitTimeoutMs = ms;
    // When the DisplayBuffer has pending changes, ensure we wake up so that
    // they can be flushed in time.
    int flushTimeoutMs = displayBuf.timeUntilPendingFlushMs();
    if (ms < 0)
        waitTimeoutMs = flushTimeoutMs;
    else if (flushTimeoutMs >= 0)
        waitTimeoutMs = min(ms, flushTimeoutMs);

    waiter.waitForEvents(waitTimeoutMs);
}

void Platform::signalCallback(bool enter) noexcept
{
    if (instance && !instance->console.lockedByCurrentThread())
    {
        // FIXME: these are not signal safe!
        if (enter)
            instance->restoreConsole();
        else
            instance->setUpConsole();
    }
}

} // namespace tvision
