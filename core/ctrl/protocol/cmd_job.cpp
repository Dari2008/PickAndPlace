// CHECK RUN PAUSE RESUME CONTINUE SKIP STOP JOB
#include "commands.h"
#include "../feeder_scan.h"
#include "../job/runner.h"

static bool jobPath(Port &port, Args &args, char *path)
{
    if (args.count() < 2)
    {
        out::error(port.io, PSTR("missing path"));
        return false;
    }
    if (!commands::resolvePath(port, args[1], path))
    {
        out::error(port.io, PSTR("path too long"));
        return false;
    }
    return true;
}

void cmdCheck(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!jobPath(port, args, path))
        return;
    if (runner.active())
    {
        out::error(port.io, PSTR("busy: a job is running"));
        return;
    }
    const uint8_t n = runner.check(path, &port.io);
    if (!n)
    {
        out::rec(port.io, PSTR("summary"));
        runner.printSummary(port.io);
        port.io.println();
        out::ok(port.io);
        return;
    }
    char count[6];
    itoa(n, count, 10);
    out::error2(port.io, PSTR("problems:"), count);
}

void cmdRun(Port &port, Args &args)
{
    char path[PATH_MAX_LEN];
    if (!jobPath(port, args, path))
        return;
    if (feederScan.active())
    {
        out::error(port.io, PSTR("busy: auto detect is running"));
        return;
    }
    runner.start(path, port.io); // prints ok / error
}

static bool needJob(Port &port)
{
    if (runner.active())
        return true;
    out::error(port.io, PSTR("no job running"));
    return false;
}

void cmdPause(Port &port, Args &)
{
    if (!needJob(port))
        return;
    runner.pause();
    out::ok(port.io);
}

void cmdResume(Port &port, Args &)
{
    if (!needJob(port))
        return;
    runner.resume();
    out::ok(port.io);
}

void cmdContinue(Port &port, Args &)
{
    if (!needJob(port))
        return;
    runner.cont();
    out::ok(port.io);
}

void cmdSkip(Port &port, Args &)
{
    if (!needJob(port))
        return;
    runner.skip();
    out::ok(port.io);
}

void cmdStop(Port &port, Args &)
{
    if (!needJob(port))
        return;
    runner.stop();
    out::ok(port.io);
}

void cmdJob(Port &port, Args &)
{
    out::rec(port.io, PSTR("job"));
    runner.printJob(port.io);
    port.io.println();
    out::ok(port.io);
}
