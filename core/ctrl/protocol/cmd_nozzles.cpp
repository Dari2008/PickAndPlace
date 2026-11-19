// NOZZLES NOZZLE - the nozzle changer (../nozzles.h)
//
//   NOZZLES                                   nozzle num= name= dx= dy= x= y= present= mounted=  (one per slot)
//                                             + rack count= mounted= sensors=   (sensors = 0: not readable)
//   NOZZLE <n> [name=<text>] [dx=<mm>] [dy=<mm>]   show / change one slot
//   NOZZLE CHANGE <n|name|0>                  put the nozzle on the head back, pick up slot n (0 = only put back)
//   NOZZLE MOUNTED <n|0>                      tell which nozzle is on the head (no movement)
//   NOZZLE LEARN START                        learn which sensor bit is which slot: trigger the slots in order
//   NOZZLE LEARN                              poll it (a few times a second)  learn active= next= count= raw= step= bits=
//                                             step: wait, assigned, finished, two; finished stores nozzle_bit_n
//   NOZZLE LEARN CANCEL                       stop, nothing changes
//   NOZZLE TOUCH                              load cell touch-off of the nozzle on the head: stores its dz
//                                             (no nozzle / no changer: sets head2_reach)  touch slot= z= reach= dz=
#include "commands.h"
#include "../job/runner.h"
#include "../nozzles.h"
#include "../settings/general_settings.h"

static NozzleLearn learn;

static void printLearn(Print &p, unsigned raw, const char *step)
{
    out::rec(p, PSTR("learn"));
    out::num(p, PSTR("active"), learn.active());
    out::num(p, PSTR("next"), learn.next());
    out::num(p, PSTR("count"), learn.slots());
    out::num(p, PSTR("raw"), raw);
    out::str(p, PSTR("step"), step);
    std::string bits;
    for (int n = 1; n < learn.next(); n++)
        bits += (n > 1 ? "," : "") + std::to_string(learn.bit(n));
    out::str(p, PSTR("bits"), bits.c_str());
    p.println();
    out::ok(p);
}

static void cmdLearn(Port &port, Args &args)
{
    Print &p = port.io;
    if (args.is(2, PSTR("CANCEL")))
    {
        learn.cancel();
        out::ok(p);
        return;
    }
    unsigned raw = 0;
    if (!nozzles.readRaw(raw))
    {
        out::error(p, PSTR("nozzle sensors not readable (tool changer board silent or Octopus offline)"));
        return;
    }
    if (args.is(2, PSTR("START")))
    {
        if (commands::refuseIfBusy(port))
            return;
        if (!nozzles.count())
        {
            out::error(p, PSTR("no nozzle changer (nozzle_count = 0)"));
            return;
        }
        learn.start(raw, nozzles.count());
        printLearn(p, raw, "wait");
        return;
    }
    if (!learn.active())
    {
        out::error(p, PSTR("not learning (NOZZLE LEARN START)"));
        return;
    }
    const NozzleLearn::Step step = learn.feed(raw);
    if (step == NozzleLearn::FINISHED)
    {
        for (int n = 1; n <= learn.slots(); n++)
            settings.nozzleBit[n - 1] = (uint8_t)learn.bit(n);
        if (!settings.save())
        {
            out::error(p, PSTR("can't save the settings"));
            return;
        }
    }
    printLearn(p, raw, step == NozzleLearn::FINISHED ? "finished" : step == NozzleLearn::ASSIGNED ? "assigned"
                       : step == NozzleLearn::TWO_AT_ONCE                                        ? "two"
                                                                                                 : "wait");
}

static void printSlot(Print &p, int n, unsigned mask, bool sensors)
{
    float x = 0, y = 0;
    nozzles.slotXY(n, x, y);
    const NozzleSlot &s = nozzles.slot(n);
    out::rec(p, PSTR("nozzle"));
    out::num(p, PSTR("num"), n);
    out::str(p, PSTR("name"), s.name.c_str());
    out::flt(p, PSTR("dx"), s.dx, 3);
    out::flt(p, PSTR("dy"), s.dy, 3);
    out::flt(p, PSTR("dz"), s.dz, 3);
    out::flt(p, PSTR("x"), x, 2);
    out::flt(p, PSTR("y"), y, 2);
    out::num(p, PSTR("present"), sensors ? (int)((mask >> (n - 1)) & 1) : -1);
    out::num(p, PSTR("mounted"), nozzles.mounted() == n);
    p.println();
}

void cmdNozzles(Port &port, Args &)
{
    Print &p = port.io;
    unsigned mask = 0;
    const bool sensors = nozzles.count() > 0 && nozzles.readPresent(mask);
    for (int n = 1; n <= nozzles.count(); n++)
        printSlot(p, n, mask, sensors);
    out::rec(p, PSTR("rack"));
    out::num(p, PSTR("count"), nozzles.count());
    out::num(p, PSTR("mounted"), nozzles.mounted());
    out::num(p, PSTR("sensors"), sensors);
    p.println();
    out::ok(p);
}

void cmdNozzle(Port &port, Args &args)
{
    Print &p = port.io;
    if (args.count() < 2)
    {
        out::error(p, PSTR("usage: NOZZLE <n> [name= dx= dy= dz=] | CHANGE <n|name|0> | MOUNTED <n|0> | TOUCH"));
        return;
    }
    if (args.is(1, PSTR("LEARN")))
    {
        cmdLearn(port, args);
        return;
    }
    if (args.is(1, PSTR("TOUCH")))
    {
        if (commands::refuseIfBusy(port))
            return;
        if (!motion.isHomed())
        {
            out::error(p, PSTR("home first"));
            return;
        }
        const int n = nozzles.count() ? nozzles.mounted() : 0;
        float reach = 0, z = 0;
        std::string err;
        if (!nozzles.touchOff(n, err, &reach, &z))
        {
            out::error(p, err.c_str());
            return;
        }
        out::rec(p, PSTR("touch"));
        out::num(p, PSTR("slot"), n);
        out::flt(p, PSTR("z"), z, 3);
        out::flt(p, PSTR("reach"), reach, 3);
        out::flt(p, PSTR("dz"), n ? nozzles.slot(n).dz : 0.0f, 3);
        p.println();
        out::ok(p);
        return;
    }
    if (args.is(1, PSTR("CHANGE")) || args.is(1, PSTR("MOUNTED")))
    {
        const bool change = args.is(1, PSTR("CHANGE"));
        if (args.count() < 3)
        {
            out::error(p, PSTR("which nozzle (slot number, name or 0)?"));
            return;
        }
        const int n = nozzles.find(args[2]);
        if (!n && strcmp(args[2], "0") && strcasecmp(args[2], "none"))
        {
            out::error2(p, PSTR("no such nozzle"), args[2]);
            return;
        }
        if (!change)
        {
            nozzles.setMounted(n);
            out::ok(p);
            return;
        }
        if (commands::refuseIfBusy(port))
            return;
        if (!nozzles.count())
        {
            out::error(p, PSTR("no nozzle changer (nozzle_count = 0)"));
            return;
        }
        if (!motion.isHomed())
        {
            out::error(p, PSTR("home first"));
            return;
        }
        std::string err;
        if (!nozzles.change(n, err))
        {
            out::error(p, err.c_str());
            return;
        }
        out::rec(p, PSTR("rack"));
        out::num(p, PSTR("mounted"), nozzles.mounted());
        p.println();
        out::ok(p);
        return;
    }
    float v;
    if (!Args::number(args[1], v) || v < 1 || v > nozzles.count())
    {
        out::error(p, PSTR("slot number 1 .. nozzle_count"));
        return;
    }
    const int n = (int)v;
    NozzleSlot &s = nozzles.slot(n);
    bool changed = false;
    for (uint8_t i = 2; i < args.count(); i++)
    {
        const char *a = args[i];
        const char *eq = strchr(a, '=');
        if (!eq)
            continue;
        const std::string key(a, eq - a), val(eq + 1);
        if (key == "name")
        {
            if (val.size() > 15 || val.find_first_of(" \"") != std::string::npos)
            {
                out::error(p, PSTR("name: up to 15 characters, no spaces"));
                return;
            }
            s.name = val;
        }
        else if (key == "dx")
            s.dx = strtof(val.c_str(), nullptr);
        else if (key == "dy")
            s.dy = strtof(val.c_str(), nullptr);
        else if (key == "dz")
            s.dz = strtof(val.c_str(), nullptr);
        else
        {
            out::error2(p, PSTR("unknown key"), key.c_str());
            return;
        }
        changed = true;
    }
    if (changed && !nozzles.save())
    {
        out::error(p, PSTR("can't save nozzles.txt"));
        return;
    }
    unsigned mask = 0;
    const bool sensors = nozzles.readPresent(mask);
    printSlot(p, n, mask, sensors);
    out::ok(p);
}
