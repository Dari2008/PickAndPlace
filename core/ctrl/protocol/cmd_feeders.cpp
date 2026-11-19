// FEEDERS FEEDER POCKET SCAN NFC
#include "commands.h"
#include <motion.h>
#include "../feeder_scan.h"
#include "../job/runner.h"
#include "../settings/feeder_store.h"
#include "../settings/general_settings.h"
#include "../settings/storage.h"

static const float POCKET_TRAVEL_SPEED = 50.0f; // mm/s for POCKET GOTO

static void printFeeder(Print &p, const Feeder &f)
{
    out::rec(p, PSTR("feeder"));
    out::num(p, PSTR("num"), f.num);
    out::str(p, PSTR("name"), f.name);
    out::str(p, PSTR("part"), f.part);
    out::flt(p, PSTR("pos"), f.feederPos, 2);
    out::flt(p, PSTR("advance"), FeederStore::advanceDeg(f), 2); // the angle actually turned
    out::num(p, PSTR("teeth"), f.teeth);
    out::num(p, PSTR("next"), f.next);
    out::num(p, PSTR("pockets"), f.pocketCount);
    out::str(p, PSTR("tag"), f.tag);
    p.println();
}

// Feeder number argument `i`, loaded into `feeder`. Prints the error.
static bool loadArg(Port &port, Args &args, uint8_t i)
{
    float n;
    if (args.count() <= i || !Args::number(args[i], n) || n < 1 || n > 99)
    {
        out::error(port.io, PSTR("missing feeder number"));
        return false;
    }
    if (!storage.ready())
    {
        out::error(port.io, PSTR("no SD card"));
        return false;
    }
    if (feeders.indexOf((uint8_t)n) < 0 && !(feeders.scan() && feeders.indexOf((uint8_t)n) >= 0))
    {
        out::error(port.io, PSTR("no such feeder"));
        return false;
    }
    if (!feeders.load((uint8_t)n, feeder))
    {
        out::error(port.io, PSTR("can't read the feeder file"));
        return false;
    }
    return true;
}

static void saveAndAnswer(Port &port)
{
    if (!feeders.save(feeder))
    {
        out::error(port.io, PSTR("can't save to the SD card"));
        return;
    }
    printFeeder(port.io, feeder);
    out::ok(port.io);
}

void cmdFeeders(Port &port, Args &)
{
    if (!storage.ready())
    {
        out::error(port.io, PSTR("no SD card"));
        return;
    }
    if (!runner.active())
        feeders.scan();
    Feeder f;
    for (uint8_t i = 0; i < feeders.count(); i++)
        if (feeders.load(feeders.entry(i).num, f))
            printFeeder(port.io, f);
    out::ok(port.io);
}

// "HERE" -> current X / Y, else x= y= words.
static bool pocketArgs(Args &args, uint8_t from, float &x, float &y)
{
    if (args.is(from, PSTR("HERE")))
    {
        x = motion.getTargetPosition(X_AXIS);
        y = motion.getTargetPosition(Y_AXIS);
        return true;
    }
    bool bad = false;
    const bool hasX = args.kv(PSTR("x"), x, bad);
    const bool hasY = args.kv(PSTR("y"), y, bad);
    return hasX && hasY && !bad;
}

void cmdFeeder(Port &port, Args &args)
{
    Print &p = port.io;
    const bool readOnly = args.count() == 2 && !args.is(1, PSTR("ADD"));
    if (!readOnly && commands::refuseIfBusy(port))
        return;

    if (args.is(1, PSTR("ADD")))
    {
        const int16_t num = feeders.add();
        if (num < 0 || !feeders.load(num, feeder))
        {
            out::error(p, PSTR("can't create a feeder (SD card missing or 24 feeders)"));
            return;
        }
        printFeeder(p, feeder);
        out::ok(p);
        return;
    }
    if (args.is(1, PSTR("DELETE")))
    {
        if (!loadArg(port, args, 2))
            return;
        if (!feeders.remove(feeder.num))
            out::error(p, PSTR("can't delete"));
        else
            out::ok(p);
        return;
    }
    if (args.is(1, PSTR("SET")))
    {
        if (!loadArg(port, args, 2))
            return;
        for (uint8_t i = 3; i < args.count(); i++)
        {
            const char *eq = strchr(args[i], '=');
            if (!eq)
            {
                out::error(p, PSTR("usage: FEEDER SET <n> key=value ..."));
                return;
            }
            const char *v = eq + 1;
            const uint8_t klen = eq - args[i];
            float n;
            const auto key = [&](PGM_P k) { return strlen_P(k) == klen && !strncasecmp_P(args[i], k, klen); };
            if (key(PSTR("name")))
            {
                strncpy(feeder.name, v, FEEDER_NAME_LEN);
                feeder.name[FEEDER_NAME_LEN] = '\0';
            }
            else if (key(PSTR("part")))
            {
                strncpy(feeder.part, v, FEEDER_PART_LEN);
                feeder.part[FEEDER_PART_LEN] = '\0';
            }
            else if (key(PSTR("tag")))
            {
                strncpy(feeder.tag, v, FEEDER_TAG_LEN);
                feeder.tag[FEEDER_TAG_LEN] = '\0';
            }
            else if (!Args::number(v, n))
            {
                out::error2(p, PSTR("not a number:"), args[i]);
                return;
            }
            else if (key(PSTR("pos")))
                feeder.feederPos = n;
            else if (key(PSTR("advance")))
                feeder.advance = n, feeder.teeth = 0; // degrees: no teeth count
            else if (key(PSTR("teeth")))
            {
                if (n < 0 || n > 255)
                {
                    out::error(p, PSTR("teeth: 0 .. 255 (0 = use advance in degrees)"));
                    return;
                }
                feeder.teeth = (uint8_t)n;
            }
            else if (key(PSTR("next")))
            {
                if (n < 1 || n > max(1, (int)feeder.pocketCount))
                {
                    out::error(p, PSTR("next is outside the pockets"));
                    return;
                }
                feeder.next = (uint8_t)n;
            }
            else
            {
                out::error2(p, PSTR("unknown key"), args[i]);
                return;
            }
        }
        saveAndAnswer(port);
        return;
    }
    if (args.is(1, PSTR("QR")))
    {
        // FEEDER QR TEACH <n>: the top camera is centred on feeder n's QR code -> where the codes are
        const bool teach = args.is(2, PSTR("TEACH"));
        if (!loadArg(port, args, teach ? 3 : 2))
            return;
        if (commands::refuseIfBusy(port))
            return;
        if (teach)
        {
            const float qx = motion.getTargetPosition(X_AXIS) + settings.camTopOffset[0];
            const float qy = motion.getTargetPosition(Y_AXIS) + settings.camTopOffset[1];
            char v[24];
            snprintf(v, sizeof(v), "%.3f", qx - settings.feederQrDir[0] * feeder.feederPos);
            settings.setValue(settings.find("feeder_qr_x0"), v);
            snprintf(v, sizeof(v), "%.3f", qy - settings.feederQrDir[1] * feeder.feederPos);
            settings.setValue(settings.find("feeder_qr_y0"), v);
            if (!settings.save())
            {
                out::error(p, PSTR("can't save the settings"));
                return;
            }
            out::rec(p, PSTR("qr"));
            out::flt(p, PSTR("x0"), settings.feederQr0[0], 3);
            out::flt(p, PSTR("y0"), settings.feederQr0[1], 3);
            p.println();
            out::ok(p);
            return;
        }
        std::string text, err;
        if (!FeederScan::readQrAt(feeder.feederPos, text, err))
        {
            out::error(p, err.c_str());
            return;
        }
        FeederStore::applyQr(feeder, text.c_str());
        out::rec(p, PSTR("qr"));
        out::str(p, PSTR("text"), text.c_str());
        p.println();
        saveAndAnswer(port);
        return;
    }
    if (args.is(1, PSTR("RESET")))
    {
        if (!loadArg(port, args, 2))
            return;
        feeder.next = 1;
        saveAndAnswer(port);
        return;
    }
    if (args.is(1, PSTR("ADVANCE")) || args.is(1, PSTR("GOTO")))
    {
        if (!loadArg(port, args, 2))
            return;
#if HAS_E1
        motion.moveE1To(feeder.feederPos, mval("e1_speed", 30));
#endif
        if (args.is(1, PSTR("ADVANCE")))
            motion.rotate(FeederStore::advanceDeg(feeder), mval("e0_speed", 180));
        out::ok(p);
        return;
    }

    // FEEDER <n>: details with pockets
    if (!loadArg(port, args, 1))
        return;
    printFeeder(p, feeder);
    for (uint8_t i = 0; i < feeder.pocketCount; i++)
    {
        out::rec(p, PSTR("pocket"));
        out::num(p, PSTR("i"), i + 1);
        out::flt(p, PSTR("x"), feeder.pockets[i].x, 2);
        out::flt(p, PSTR("y"), feeder.pockets[i].y, 2);
        p.println();
    }
    out::ok(p);
}

// POCKET <n> ADD|SET|DELETE|GOTO ...
void cmdPocket(Port &port, Args &args)
{
    Print &p = port.io;
    if (commands::refuseIfBusy(port) || !loadArg(port, args, 1))
        return;
    float x, y, idx = 0;
    const bool needIndex = !args.is(2, PSTR("ADD"));
    if (needIndex && (!Args::number(args[3], idx) || idx < 1 || idx > feeder.pocketCount))
    {
        out::error(p, PSTR("no such pocket"));
        return;
    }
    const uint8_t i = (uint8_t)idx - 1;

    if (args.is(2, PSTR("ADD")))
    {
        if (feeder.pocketCount >= MAX_POCKETS)
        {
            out::error(p, PSTR("24 pockets at most"));
            return;
        }
        if (!pocketArgs(args, 3, x, y))
        {
            out::error(p, PSTR("usage: POCKET <n> ADD x= y= | HERE"));
            return;
        }
        feeder.pockets[feeder.pocketCount].x = x;
        feeder.pockets[feeder.pocketCount].y = y;
        feeder.pocketCount++;
    }
    else if (args.is(2, PSTR("SET")))
    {
        if (!pocketArgs(args, 4, x, y))
        {
            out::error(p, PSTR("usage: POCKET <n> SET <i> x= y= | HERE"));
            return;
        }
        feeder.pockets[i].x = x;
        feeder.pockets[i].y = y;
    }
    else if (args.is(2, PSTR("DELETE")))
    {
        memmove(&feeder.pockets[i], &feeder.pockets[i + 1], (feeder.pocketCount - i - 1) * sizeof(Pocket));
        feeder.pocketCount--;
        if (feeder.next > feeder.pocketCount)
            feeder.next = 1;
    }
    else if (args.is(2, PSTR("GOTO")))
    {
        // Over the pocket at the current height (lift first if needed).
        const float z = motion.getTargetPosition(Z_AXIS);
        if (!motion.moveTo(feeder.pockets[i].x, feeder.pockets[i].y, z, POCKET_TRAVEL_SPEED))
            out::error(p, PSTR("move rejected (alarm?)"));
        else
            out::ok(p);
        return;
    }
    else
    {
        out::error(p, PSTR("usage: POCKET <n> ADD|SET|DELETE|GOTO"));
        return;
    }
    saveAndAnswer(port);
}

void cmdScan(Port &port, Args &args)
{
    if (args.is(1, PSTR("STOP")))
    {
        feederScan.stop();
        out::ok(port.io);
        return;
    }
    if (commands::refuseIfBusy(port))
        return;
    PGM_P err = feederScan.start();
    if (err)
        out::error(port.io, err);
    else
        out::ok(port.io);
}

void cmdNfc(Port &port, Args &)
{
    out::error(port.io, PSTR("no NFC reader on this machine"));
}
