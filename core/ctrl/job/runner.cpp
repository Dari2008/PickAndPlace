#include "runner.h"
#include <motion.h>
#include "../mosfets.h"
#include "../protocol/out.h"
#include "../settings/general_settings.h"
#include "../settings/storage.h"
#include "../../vision/vision.h"
#include "../../octo/octopus.h"
#include "../nozzles.h"

Runner runner;

enum Section : uint8_t
{
    SEC_NONE,
    SEC_JOB,
    SEC_BOARD,
    SEC_FEEDERS,
    SEC_FUNC,
    SEC_PART,
    SEC_OTHER
};

enum Abort : uint8_t
{
    ABORT_NONE,
    ABORT_SKIP,
    ABORT_STOP,
    ABORT_FAIL
};

static const uint8_t MAX_PROBLEMS_SHOWN = 20;
static char buf_[PAP_LINE_MAX + 1]; // the line being read (shared: never needed twice at once)

static Section sectionOf(const char *kind)
{
    if (!strcasecmp_P(kind, PSTR("job")))
        return SEC_JOB;
    if (!strcasecmp_P(kind, PSTR("board")))
        return SEC_BOARD;
    if (!strcasecmp_P(kind, PSTR("feeders")))
        return SEC_FEEDERS;
    if (!strcasecmp_P(kind, PSTR("func")))
        return SEC_FUNC;
    if (!strcasecmp_P(kind, PSTR("part")))
        return SEC_PART;
    return SEC_OTHER;
}

// ===========================================================================
// Checking
// ===========================================================================

void Runner::problem(uint16_t line, PGM_P text, const char *detail)
{
    problems_++;
    if (!out_ || problems_ > MAX_PROBLEMS_SHOWN)
        return;
    char msg[80];
    strncpy_P(msg, text, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    if (detail && strlen(msg) + strlen(detail) + 2 < sizeof(msg))
    {
        strcat_P(msg, PSTR(": "));
        strcat(msg, detail);
    }
    out::rec(*out_, PSTR("problem"));
    out::num(*out_, PSTR("line"), line);
    out::str(*out_, PSTR("text"), msg);
    out_->println();
}

int8_t Runner::funcIndex(const char *name) const
{
    const uint16_t h = pap::hash(name);
    for (uint8_t i = 0; i < funcCount_; i++)
        if (funcs_[i].hash == h)
            return i;
    return -1;
}

int8_t Runner::feederIndex(const char *name) const
{
    const uint16_t h = pap::hash(name);
    for (uint8_t i = 0; i < feederCount_; i++)
        if (feederHash_[i] == h)
            return i;
    return -1;
}

uint8_t Runner::check(const char *path, Print *out)
{
    out_ = out;
    problems_ = 0;
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    if (!storage.ready())
    {
        problem(0, PSTR("no SD card"));
        return problems_;
    }
    feeders.scan(); // match against the feeder files as they are now
    if (!f_.open(path, O_RDONLY) || f_.isDir())
    {
        f_.close();
        problem(0, PSTR("can't open"), path);
        return problems_;
    }
    if (indexFile())
        checkFile();
    f_.close();
    return problems_;
}

bool Runner::parseJobKey(const char *key, const char *value, uint16_t line)
{
    float v;
    if (!strcasecmp_P(key, PSTR("name")))
    {
        strncpy(name_, value, sizeof(name_) - 1);
        name_[sizeof(name_) - 1] = '\0';
    }
    else if (!strcasecmp_P(key, PSTR("speed")))
    {
        if (!pap::number(value, v) || v <= 0)
            problem(line, PSTR("speed must be a number > 0"));
        else
            jobSpeed_ = v;
    }
    else if (!strcasecmp_P(key, PSTR("paste_amount")))
    {
        if (!pap::number(value, v) || v < 0)
            problem(line, PSTR("paste_amount must be a number >= 0"));
        else
            jobAmount_ = v;
    }
    else if (!strcasecmp_P(key, PSTR("nozzle")))
    {
        const int n = nozzles.find(value);
        if (!n)
            problem(line, nozzles.count() ? PSTR("nozzle is not in the rack (name or slot number)")
                                          : PSTR("nozzle = needs a nozzle changer (nozzle_count)"),
                    value);
        jobNozzle_ = (uint8_t)n;
    }
    return true; // run_before / run_after: second pass (functions known then)
}

bool Runner::parseBoardKey(const char *key, const char *value, uint16_t line)
{
    float v[3];
    if (!strcasecmp_P(key, PSTR("origin")))
    {
        if (!pap::point(value, v, 2, 2))
            problem(line, PSTR("origin must be x, y"));
        else
        {
            origin_[0] = v[0];
            origin_[1] = v[1];
            hasOrigin_ = true;
        }
    }
    else if (!strcasecmp_P(key, PSTR("fiducial")))
    {
        if (!pap::point(value, v, 2, 2))
            problem(line, PSTR("fiducial must be x, y"));
        else if (fidCount_ >= PAP_MAX_FIDUCIALS)
            problem(line, PSTR("at most 3 fiducials"));
        else
        {
            fid_[fidCount_][0] = v[0];
            fid_[fidCount_][1] = v[1];
            fidCount_++;
        }
    }
    else if (!strcasecmp_P(key, PSTR("offset")))
    {
        const uint8_t n = pap::point(value, v, 2, 3);
        if (!n)
            problem(line, PSTR("offset must be x, y or x, y, z"));
        else
        {
            boardOffset_[0] = v[0];
            boardOffset_[1] = v[1];
            boardOffset_[2] = n == 3 ? v[2] : 0.0f;
        }
    }
    else
    {
        float *target = nullptr;
        if (!strcasecmp_P(key, PSTR("thickness")))
            target = &thickness_;
        else if (!strcasecmp_P(key, PSTR("paste_height")))
            target = &pasteH_;
        else if (!strcasecmp_P(key, PSTR("place_height")))
            target = &placeH_;
        else if (!strcasecmp_P(key, PSTR("safe_height")))
            target = &safeH_;
        if (target && !pap::number(value, *target))
            problem(line, PSTR("not a number"), key);
    }
    return true;
}

bool Runner::parseHooks(char *value, uint8_t *list, uint16_t line, bool report)
{
    uint8_t n = 0;
    char *p = value;
    while (p)
    {
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';
        char *name = pap::trim(p);
        if (*name)
        {
            const int8_t i = funcIndex(name);
            if (i < 0)
            {
                if (report)
                    problem(line, PSTR("no function"), name);
            }
            else if (n >= PAP_MAX_HOOKS)
            {
                if (report)
                    problem(line, PSTR("at most 4 functions"));
            }
            else
                list[n++] = i;
        }
        p = comma ? comma + 1 : nullptr;
    }
    for (uint8_t i = n; i < PAP_MAX_HOOKS; i++)
        list[i] = 0xFF;
    return true;
}

bool Runner::parsePartKey(Part &part, const char *key, char *value, uint16_t line, bool report)
{
    float v[2];
    if (!strcasecmp_P(key, PSTR("feeder")))
    {
        const int8_t i = feederIndex(value);
        if (i < 0 && report)
            problem(line, PSTR("feeder is not listed in [feeders]"), value);
        part.feeder = i < 0 ? 0xFF : i;
    }
    else if (!strcasecmp_P(key, PSTR("at")))
    {
        if (!pap::point(value, v, 2, 2))
        {
            if (report)
                problem(line, PSTR("at must be x, y"));
        }
        else
        {
            part.at[0] = v[0];
            part.at[1] = v[1];
            part.hasAt = true;
        }
    }
    else if (!strcasecmp_P(key, PSTR("rotation")))
    {
        if (!pap::number(value, part.rotation) && report)
            problem(line, PSTR("rotation must be a number"));
    }
    else if (!strcasecmp_P(key, PSTR("paste_amount")))
    {
        if ((!pap::number(value, part.amount) || part.amount < 0) && report)
            problem(line, PSTR("paste_amount must be a number >= 0"));
        else
            part.hasAmount = true;
    }
    else if (!strcasecmp_P(key, PSTR("paste")))
    {
        part.dotCount = 0;
        if (!strcasecmp_P(value, PSTR("none")))
            return true;
        char *p = value;
        while (p)
        {
            char *semi = strchr(p, ';');
            if (semi)
                *semi = '\0';
            if (!pap::point(p, v, 2, 2))
            {
                if (report)
                    problem(line, PSTR("paste dot must be x, y"), pap::trim(p));
            }
            else if (part.dotCount >= PAP_MAX_DOTS)
            {
                if (report)
                    problem(line, PSTR("more than 16 paste dots"));
                break;
            }
            else
            {
                part.dots[part.dotCount][0] = v[0];
                part.dots[part.dotCount][1] = v[1];
                part.dotCount++;
            }
            p = semi ? semi + 1 : nullptr;
        }
    }
    else if (!strcasecmp_P(key, PSTR("nozzle")))
    {
        const int n = nozzles.find(value);
        if (!n && report)
            problem(line, nozzles.count() ? PSTR("nozzle is not in the rack (name or slot number)")
                                          : PSTR("nozzle = needs a nozzle changer (nozzle_count)"),
                    value);
        part.nozzle = (uint8_t)n;
    }
    else if (!strcasecmp_P(key, PSTR("run_before")))
        parseHooks(value, part.before, line, report);
    else if (!strcasecmp_P(key, PSTR("run_after")))
        parseHooks(value, part.after, line, report);
    return true;
}

// Pass 1: sections, [feeders], function positions, [job] and [board] values.
bool Runner::indexFile()
{
    hasJob_ = hasBoard_ = hasFeeders_ = hasOrigin_ = false;
    const char *slash = strrchr(path_, '/');
    strncpy(name_, slash ? slash + 1 : path_, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
    jobSpeed_ = 0.0f;
    jobAmount_ = 0.5f;
    jobNozzle_ = 0;
    for (uint8_t i = 0; i < PAP_MAX_HOOKS; i++)
        jobBefore_[i] = jobAfter_[i] = 0xFF;
    origin_[0] = origin_[1] = 0.0f;
    thickness_ = 1.6f;
    pasteH_ = 0.3f;
    placeH_ = 0.0f;
    safeH_ = 5.0f;
    boardOffset_[0] = boardOffset_[1] = boardOffset_[2] = 0.0f;
    fidCount_ = 0;
    fidValid_ = false;
    feederCount_ = funcCount_ = 0;
    total_ = 0;

    Section sec = SEC_NONE;
    uint16_t line = 0;
    bool tooLong;
    f_.seekSet(0);
    while (pap::readLine(f_, buf_, tooLong))
    {
        line++;
        if (tooLong)
            problem(line, PSTR("line longer than 120 characters"));
        char *s = pap::clean(buf_);
        if (!*s)
            continue;
        char *kind, *name;
        if (s[0] == '[')
        {
            if (!pap::header(s, kind, name))
            {
                problem(line, PSTR("section header without ]"));
                sec = SEC_OTHER;
                continue;
            }
            sec = sectionOf(kind);
            bool *seen = sec == SEC_JOB ? &hasJob_ : sec == SEC_BOARD ? &hasBoard_ : sec == SEC_FEEDERS ? &hasFeeders_ : nullptr;
            if (seen)
            {
                if (*seen)
                    problem(line, PSTR("section appears twice"), kind);
                *seen = true;
            }
            else if (sec == SEC_FUNC)
            {
                if (!*name)
                    problem(line, PSTR("a function needs a name"));
                else if (funcIndex(name) >= 0)
                    problem(line, PSTR("function defined twice"), name);
                else if (funcCount_ >= PAP_MAX_FUNCS)
                    problem(line, PSTR("more than 32 functions"));
                else
                {
                    funcs_[funcCount_].hash = pap::hash(name);
                    funcs_[funcCount_].line = line;
                    funcs_[funcCount_].pos = f_.curPosition();
                    funcCount_++;
                }
            }
            else if (sec == SEC_PART)
            {
                total_++;
                if (!*name)
                    problem(line, PSTR("a part needs a reference, e.g. [part R1]"));
            }
            continue;
        }

        char *key, *value;
        switch (sec)
        {
        case SEC_NONE:
            problem(line, PSTR("text outside a section"));
            break;
        case SEC_JOB:
            if (pap::keyValue(s, key, value))
                parseJobKey(key, value, line);
            else
                problem(line, PSTR("expected key = value"));
            break;
        case SEC_BOARD:
            if (pap::keyValue(s, key, value))
                parseBoardKey(key, value, line);
            else
                problem(line, PSTR("expected key = value"));
            break;
        case SEC_FEEDERS:
            if (feederIndex(s) >= 0)
                break; // listed twice: harmless
            if (feederCount_ >= MAX_FEEDERS)
            {
                problem(line, PSTR("more than 24 feeders"));
                break;
            }
            feederNum_[feederCount_] = feeders.match(s);
            if (!feederNum_[feederCount_])
                problem(line, PSTR("feeder is not on the machine"), s);
            feederHash_[feederCount_++] = pap::hash(s);
            break;
        default:
            break;
        }
    }
    if (!hasJob_)
        problem(0, PSTR("missing [job]"));
    if (!hasBoard_)
        problem(0, PSTR("missing [board]"));
    else if (!hasOrigin_)
        problem(0, PSTR("[board] needs origin = x, y"));
    if (!hasFeeders_)
        problem(0, PSTR("missing [feeders]"));
    return true;
}

// Pass 2: [job] hooks, parts and function bodies (all functions are known now).
void Runner::checkFile()
{
    Section sec = SEC_NONE;
    uint16_t line = 0;
    bool inPart = false;
    bool tooLong;
    dots_ = 0;
    f_.seekSet(0);
    while (true)
    {
        const bool more = pap::readLine(f_, buf_, tooLong);
        line++;
        char *s = more ? pap::clean(buf_) : buf_;
        char *kind, *name, *key, *value;
        const bool isHeader = more && s[0] == '[' && pap::header(s, kind, name);
        if ((isHeader || !more) && inPart)
        {
            dots_ += part_.dotCount;
            if (part_.feeder == 0xFF)
                problem(part_.line, PSTR("part has no (listed) feeder"), part_.ref);
            if (!part_.hasAt)
                problem(part_.line, PSTR("part has no at"), part_.ref);
            inPart = false;
        }
        if (!more)
            break;
        if (!*s)
            continue;
        if (isHeader)
        {
            sec = sectionOf(kind);
            if (sec == SEC_PART)
            {
                memset(&part_, 0, sizeof(part_));
                strncpy(part_.ref, name, sizeof(part_.ref) - 1);
                part_.line = line;
                part_.feeder = 0xFF;
                memset(part_.before, 0xFF, sizeof(part_.before));
                memset(part_.after, 0xFF, sizeof(part_.after));
                inPart = true;
            }
            continue;
        }
        if (s[0] == '[')
            continue; // broken header, reported in pass 1
        switch (sec)
        {
        case SEC_JOB:
            if (pap::keyValue(s, key, value))
            {
                if (!strcasecmp_P(key, PSTR("run_before")))
                    parseHooks(value, jobBefore_, line, true);
                else if (!strcasecmp_P(key, PSTR("run_after")))
                    parseHooks(value, jobAfter_, line, true);
            }
            break;
        case SEC_PART:
            if (pap::keyValue(s, key, value))
                parsePartKey(part_, key, value, line, true);
            else
                problem(line, PSTR("expected key = value"));
            break;
        case SEC_FUNC:
            exec(s, nullptr, 0, true, line);
            break;
        default:
            break;
        }
    }
}

// ===========================================================================
// Control
// ===========================================================================

bool Runner::start(const char *path, Print &out)
{
    if (active())
    {
        out::error(out, PSTR("busy: a job is running"));
        return false;
    }
    if (!motion.isHomed() || (octo.value("e1_home", "none") != "none" && !motion.isE1Homed()))
    {
        out::error(out, PSTR("not homed: send HOME first"));
        return false;
    }
    const uint8_t n = check(path, &out);
    if (n)
    {
        char detail[8];
        itoa(n, detail, 10);
        out::error2(out, PSTR("file has problems, not started:"), detail);
        state_ = RUN_IDLE;
        return false;
    }
    if (!f_.open(path, O_RDONLY))
    {
        out::error2(out, PSTR("can't open"), path);
        return false;
    }
    state_ = RUN_STARTING;
    done_ = skipped_ = 0;
    partIndex_ = -1;
    error_[0] = message_[0] = nextRef_[0] = '\0';
    phase_ = nullptr;
    out::ok(out);
    return true;
}

void Runner::pause()
{
    if (state_ == RUN_RUNNING || state_ == RUN_MESSAGE || state_ == RUN_STARTING)
        pauseReq_ = true;
}

void Runner::resume()
{
    pauseReq_ = false;
}

void Runner::cont()
{
    contReq_ = true;
}

void Runner::skip()
{
    if (active() && partIndex_ >= 0 && !stopping_ && abort_ == ABORT_NONE)
        abort_ = ABORT_SKIP;
}

void Runner::stop()
{
    if (state_ == RUN_STARTING)
    {
        f_.close();
        state_ = RUN_STOPPED;
        emit();
        return;
    }
    if (active() && abort_ < ABORT_STOP)
        abort_ = ABORT_STOP;
    pauseReq_ = false;
}

PGM_P Runner::stateName() const
{
    switch (state_)
    {
    case RUN_STARTING:
    case RUN_RUNNING:
        return PSTR("running");
    case RUN_PAUSED:
        return PSTR("paused");
    case RUN_MESSAGE:
        return PSTR("message");
    case RUN_DONE:
        return PSTR("done");
    case RUN_STOPPED:
        return PSTR("stopped");
    case RUN_FAILED:
        return PSTR("failed");
    default:
        return PSTR("idle");
    }
}

uint32_t Runner::elapsedMs() const
{
    if (state_ == RUN_IDLE || state_ == RUN_STARTING)
        return 0;
    const uint32_t end = active() ? millis() : endedAt_;
    uint32_t paused = pausedMs_;
    if (state_ == RUN_PAUSED)
        paused += millis() - pauseStart_;
    return end - startedAt_ - paused;
}

void Runner::printJob(Print &p)
{
    out::strP(p, PSTR("state"), stateName());
    out::str(p, PSTR("file"), path_);
    out::str(p, PSTR("name"), name_);
    out::num(p, PSTR("done"), done_);
    out::num(p, PSTR("total"), total_);
    out::num(p, PSTR("skipped"), skipped_);
    out::num(p, PSTR("percent"), total_ ? (long)(done_ + skipped_) * 100 / total_ : 0);
    const bool inPart = active() && partIndex_ >= 0 && partIndex_ < (int16_t)total_;
    out::str(p, PSTR("part"), inPart ? part_.ref : "");
    const char *feederName = "";
    if (inPart && part_.feeder != 0xFF)
    {
        const int8_t i = feeders.indexOf(feederNum_[part_.feeder]);
        if (i >= 0)
            feederName = feeders.entry(i).name;
    }
    out::str(p, PSTR("feeder"), feederName);
    out::strP(p, PSTR("phase"), active() && phase_ ? phase_ : PSTR(""));
    out::str(p, PSTR("next"), active() ? nextRef_ : "");
    const uint32_t elapsed = elapsedMs();
    out::num(p, PSTR("elapsed"), (long)(elapsed / 1000));
    long remaining = -1;
    const uint16_t finished = done_ + skipped_;
    if (!active())
        remaining = 0;
    else if (finished && lastPartDoneMs_)
        remaining = (long)((float)lastPartDoneMs_ / finished * (total_ - finished) / 1000.0f);
    out::num(p, PSTR("remaining"), remaining);
    out::str(p, PSTR("message"), message_);
    out::str(p, PSTR("error"), error_);
}

void Runner::printSummary(Print &p)
{
    out::str(p, PSTR("name"), name_);
    out::num(p, PSTR("parts"), total_);
    out::num(p, PSTR("dots"), dots_);
    out::num(p, PSTR("feeders"), feederCount_);
    out::num(p, PSTR("funcs"), funcCount_);
}

void Runner::emit()
{
    Print &p = ports.all;
    p.print(F("!job"));
    printJob(p);
    p.println();
}

void Runner::setPhase(PGM_P phase)
{
    if (phase_ == phase)
        return;
    phase_ = phase;
    emit();
}

bool Runner::fail(PGM_P text, uint16_t line)
{
    if (abort_ < ABORT_FAIL)
    {
        abort_ = ABORT_FAIL;
        if (line)
            snprintf(error_, sizeof(error_), "line %u: %s", line, text);
        else
            strncpy_P(error_, text, sizeof(error_) - 1);
        Print &p = ports.all;
        p.print(F("!alarm"));
        out::str(p, PSTR("text"), error_);
        p.println();
    }
    return false;
}

// ===========================================================================
// Running
// ===========================================================================

void Runner::update()
{
    if (state_ == RUN_STARTING)
        runJob();
}

void Runner::runJob()
{
    state_ = RUN_RUNNING;
    startedAt_ = millis();
    pausedMs_ = lastPartDoneMs_ = 0;
    head_ = 2;
    speed_ = jobSpeed_ > 0 ? jobSpeed_ : settings.travelSpeed;
    memcpy(offset_, boardOffset_, sizeof(offset_));
    holding_ = stopping_ = false;
    contReq_ = false;
    abort_ = ABORT_NONE;
    f_.seekSet(0);
    lineNo_ = 0;
    lookAhead();
    phase_ = PSTR("func");
    emit();

    fidValid_ = false;
    fidRot_ = 0;
    if (settings.visionFiducials && fidCount_ >= 2 && !measureFiducials() && settings.visionRequired)
        fail(PSTR("fiducials not found"), 0);

    if (abort_ == ABORT_NONE && runHooks(jobBefore_, nullptr))
    {
        f_.seekSet(0);
        lineNo_ = 0;
        bool tooLong;
        while (abort_ < ABORT_STOP && pap::readLine(f_, buf_, tooLong))
        {
            lineNo_++;
            char *s = pap::clean(buf_);
            char *kind, *name;
            if (s[0] != '[' || !pap::header(s, kind, name) || sectionOf(kind) != SEC_PART)
                continue;
            memset(&part_, 0, sizeof(part_));
            strncpy(part_.ref, name, sizeof(part_.ref) - 1);
            part_.line = lineNo_;
            part_.feeder = 0xFF;
            memset(part_.before, 0xFF, sizeof(part_.before));
            memset(part_.after, 0xFF, sizeof(part_.after));
            readPart(part_);
            partIndex_++;
            lookAhead();
            phase_ = PSTR("func");
            emit();

            if (runPart(part_))
                done_++;
            else if (abort_ == ABORT_SKIP)
            {
                afterAbort();
                skipped_++;
            }
            if (abort_ >= ABORT_STOP)
                break;
            lastPartDoneMs_ = elapsedMs();
            emit();
        }
    }
    partIndex_ = total_; // no current part any more

    const bool failed = abort_ == ABORT_FAIL;
    const bool stopped = abort_ == ABORT_STOP;
    afterAbort();
    if (!failed)
    {
        // Normal end, or a stop: [job] run_after parks the machine.
        stopping_ = stopped;
        abort_ = ABORT_NONE;
        nextRef_[0] = '\0';
        runHooks(jobAfter_, nullptr);
        afterAbort();
    }
    if (failed || stopped || abort_ != ABORT_NONE)
    {
        mosfets.closeValve();
        mosfets.disablePump();
    }
    endedAt_ = millis();
    f_.close();
    state_ = failed || abort_ == ABORT_FAIL ? RUN_FAILED : (stopped || abort_ != ABORT_NONE) ? RUN_STOPPED : RUN_DONE;
    phase_ = nullptr;
    stopping_ = false;
    abort_ = ABORT_NONE;
    emit();
}

// After a skip / stop / failure: the machine stands, a held part is dropped.
void Runner::afterAbort()
{
    if (motion.isBusy())
        motion.stop();
    if (!holding_)
    {
        if (abort_ == ABORT_SKIP)
            abort_ = ABORT_NONE;
        return;
    }
    if (abort_ == ABORT_SKIP)
    {
        abort_ = ABORT_NONE;
        if (settings.rejectX >= 0 && settings.rejectY >= 0)
        {
            head_ = 2;
            if (liftToSafe())
                moveMachine(settings.rejectX, settings.rejectY, motion.getTargetPosition(Z_AXIS), 0) && sync();
        }
    }
    mosfets.closeValve(); // drop it
    holding_ = false;
}

bool Runner::runHooks(const uint8_t *hooks, Part *part)
{
    for (uint8_t i = 0; i < PAP_MAX_HOOKS && hooks[i] != 0xFF; i++)
        if (!callFunc(hooks[i], part, 1))
            return false;
    return abort_ == ABORT_NONE;
}

bool Runner::readPart(Part &part)
{
    bool tooLong;
    while (true)
    {
        const uint32_t pos = f_.curPosition();
        if (!pap::readLine(f_, buf_, tooLong))
            break;
        lineNo_++;
        char *s = pap::clean(buf_);
        if (!*s)
            continue;
        if (s[0] == '[')
        {
            f_.seekSet(pos); // the next section is read by the caller
            lineNo_--;
            break;
        }
        char *key, *value;
        if (pap::keyValue(s, key, value))
            parsePartKey(part, key, value, lineNo_, false);
    }
    return true;
}

void Runner::lookAhead()
{
    const uint32_t pos = f_.curPosition();
    nextRef_[0] = '\0';
    bool tooLong;
    while (pap::readLine(f_, buf_, tooLong))
    {
        char *s = pap::clean(buf_);
        char *kind, *name;
        if (s[0] == '[' && pap::header(s, kind, name) && sectionOf(kind) == SEC_PART)
        {
            strncpy(nextRef_, name, sizeof(nextRef_) - 1);
            nextRef_[sizeof(nextRef_) - 1] = '\0';
            break;
        }
    }
    f_.seekSet(pos);
}

bool Runner::runPart(Part &part)
{
    if (!runHooks(part.before, &part))
        return false;
    if (part.dotCount)
    {
        setPhase(PSTR("paste"));
        head_ = 1;
        const float amount = part.hasAmount ? part.amount : jobAmount_;
        for (uint8_t i = 0; i < part.dotCount; i++)
        {
            float m[3];
            boardToMachine(part.dots[i][0], part.dots[i][1], pasteH_, m);
            if (!travelTo(m[0], m[1], m[2]) || !pasteDot(amount) || !liftToSafe())
                return false;
        }
    }
    const uint8_t wanted = part.nozzle ? part.nozzle : jobNozzle_;
    if (wanted && wanted != nozzles.mounted())
    {
        setPhase(PSTR("nozzle"));
        if (!changeNozzle(wanted))
            return false;
    }
    setPhase(PSTR("pick"));
    if (!pick(part.feeder))
        return false;
    setPhase(PSTR("place"));
    if (!place(part))
        return false;
    setPhase(PSTR("func"));
    return runHooks(part.after, &part);
}

bool Runner::callFunc(uint8_t index, Part *part, uint8_t depth)
{
    if (depth > PAP_CALL_DEPTH + 1)
        return fail(PSTR("CALL nested deeper than 4"), lineNo_);
    const uint32_t pos = f_.curPosition();
    const uint16_t line = lineNo_;
    f_.seekSet(funcs_[index].pos);
    lineNo_ = funcs_[index].line;
    bool ok = true;
    bool tooLong;
    while (ok && pap::readLine(f_, buf_, tooLong))
    {
        lineNo_++;
        char *s = pap::clean(buf_);
        if (!*s)
            continue;
        if (s[0] == '[')
            break;
        ok = exec(s, part, depth, false, lineNo_);
    }
    f_.seekSet(pos);
    lineNo_ = line;
    return ok && abort_ == ABORT_NONE;
}

// ---------------------------------------------------------------------------
// Commands inside [func] (pap-format.md, Commands). dry = only check.
// ---------------------------------------------------------------------------

enum Axis : uint8_t
{
    AX_X,
    AX_Y,
    AX_Z,
    AX_F
};

// "X12.5 Z5" -> v[], mask. False on a letter not in `allowed` or a bad number.
static bool parseAxes(char *s, PGM_P allowed, float *v, uint8_t &mask)
{
    mask = 0;
    while (*s)
    {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        char *end = s;
        while (*end && *end != ' ')
            end++;
        const char save = *end;
        *end = '\0';
        char letter = s[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        const char *at = strchr_P(allowed, letter);
        float value;
        if (!at || !pap::number(s + 1, value))
            return false;
        const uint8_t axis = letter == 'X' ? AX_X : letter == 'Y' ? AX_Y : letter == 'Z' ? AX_Z : AX_F;
        v[axis] = value;
        mask |= 1 << axis;
        *end = save;
        s = end;
    }
    return true;
}

static bool onOff(const char *s, bool &on)
{
    if (!strcasecmp_P(s, PSTR("on")))
        on = true;
    else if (!strcasecmp_P(s, PSTR("off")))
        on = false;
    else
        return false;
    return true;
}

// Report a bad command: as a problem (checking) or as the job's error.
#define REJECT(msg)                                       \
    do                                                    \
    {                                                     \
        if (dry)                                          \
        {                                                 \
            char whole[PAP_LINE_MAX + 1];                 \
            snprintf_P(whole, sizeof(whole), PSTR("%s%s%s"), cmd, *rest ? " " : "", rest); \
            problem(lineNo, PSTR(msg), whole);            \
        }                                                 \
        else                                              \
            fail(PSTR(msg), lineNo);                      \
        return false;                                     \
    } while (0)

bool Runner::exec(char *line, Part *part, uint8_t depth, bool dry, uint16_t lineNo)
{
    char *cmd = line;
    char *rest = line;
    while (*rest && *rest != ' ' && *rest != '\t')
        rest++;
    if (*rest)
        *rest++ = '\0';
    rest = pap::trim(rest);
    const auto is = [cmd](PGM_P name) { return !strcasecmp_P(cmd, name); };
    float v[4];
    uint8_t mask;
    bool on;

    if (is(PSTR("MOVE")))
    {
        bool toPart = !strncasecmp_P(rest, PSTR("PART"), 4) && (rest[4] == '\0' || rest[4] == ' ');
        if (!parseAxes(toPart ? rest + 4 : rest, toPart ? PSTR("ZF") : PSTR("XYZF"), v, mask))
            REJECT("bad MOVE parameters");
        if (!toPart && !(mask & 7))
            REJECT("MOVE needs X, Y, Z or PART");
        if (dry)
            return true;
        if (toPart && !part)
            REJECT("MOVE PART needs a current part");
        float b[3], m[3];
        machineToBoard(b);
        const float x = toPart ? part->at[0] : (mask & 1 << AX_X) ? v[AX_X] : b[0];
        const float y = toPart ? part->at[1] : (mask & 1 << AX_Y) ? v[AX_Y] : b[1];
        const float z = (mask & 1 << AX_Z) ? v[AX_Z] : b[2];
        boardToMachine(x, y, z, m);
        return moveMachine(m[0], m[1], m[2], (mask & 1 << AX_F) ? v[AX_F] : 0);
    }
    if (is(PSTR("MOVE_REL")) || is(PSTR("MOVE_MACHINE")))
    {
        const bool rel = is(PSTR("MOVE_REL"));
        if (!parseAxes(rest, PSTR("XYZF"), v, mask) || !(mask & 7))
            REJECT("needs X.. Y.. Z.. (F..)");
        if (dry)
            return true;
        float m[3];
        for (uint8_t a = 0; a < 3; a++)
        {
            const float cur = motion.getTargetPosition(a);
            m[a] = (mask & 1 << a) ? (rel ? cur + v[a] : v[a]) : cur;
        }
        return moveMachine(m[0], m[1], m[2], (mask & 1 << AX_F) ? v[AX_F] : 0);
    }
    if (is(PSTR("Z")))
    {
        if (!pap::number(rest, v[0]))
            REJECT("Z needs a height");
        if (dry)
            return true;
        float b[3], m[3];
        machineToBoard(b);
        boardToMachine(b[0], b[1], v[0], m);
        return moveMachine(m[0], m[1], m[2], 0);
    }
    if (is(PSTR("OFFSET")) || is(PSTR("OFFSET_REL")))
    {
        const bool rel = is(PSTR("OFFSET_REL"));
        if (!parseAxes(rest, PSTR("XYZ"), v, mask) || (rel && !mask))
            REJECT("needs X.. Y.. Z..");
        if (dry)
            return true;
        if (!mask)
            memcpy(offset_, boardOffset_, sizeof(offset_));
        for (uint8_t a = 0; a < 3; a++)
            if (mask & 1 << a)
                offset_[a] = rel ? offset_[a] + v[a] : v[a];
        return true;
    }
    if (is(PSTR("HOME")))
    {
        if (!parseAxes(rest, PSTR("XYZ"), v, mask) && *rest)
        {
            // "HOME X Y" (letters without numbers) is fine too
            for (char *c = rest; *c; c++)
                if (!strchr_P(PSTR("XYZxyz "), *c))
                    REJECT("HOME takes X, Y and/or Z");
        }
        if (dry)
            return true;
        if (!sync())
            return false;
        setPhase(PSTR("home"));
        if (!motion.home(true)) // seesaw flag (if the Octopus has z_home = flag), then X / Y
            return fail(PSTR("homing failed"), lineNo);
        return true;
    }
    if (is(PSTR("HEAD")))
    {
        if (strcmp_P(rest, PSTR("1")) && strcmp_P(rest, PSTR("2")))
            REJECT("HEAD needs 1 or 2");
        if (!dry)
            head_ = rest[0] - '0';
        return true;
    }
    if (is(PSTR("VACUUM")) || is(PSTR("VACUUM_VALVE")))
    {
        if (!onOff(rest, on))
            REJECT("needs ON or OFF");
        if (dry)
            return true;
        if (!sync())
            return false;
        if (is(PSTR("VACUUM")))
        {
            if (on)
                mosfets.enablePump(mval("pump_power", 100) / 100.0f);
            else
                mosfets.disablePump();
        }
        else if (on)
            mosfets.openValve();
        else
            mosfets.closeValve();
        return true;
    }
    if (is(PSTR("PASTE")))
    {
        float amount = jobAmount_;
        if (*rest && !pap::number(rest, amount))
            REJECT("PASTE takes an amount");
        if (dry)
            return true;
        if (!*rest && part && part->hasAmount)
            amount = part->amount;
        return pasteDot(amount);
    }
    if (is(PSTR("PICK")) || is(PSTR("FEED")))
    {
        const bool isPick = is(PSTR("PICK"));
        int8_t idx = -1;
        if (*rest)
        {
            idx = feederIndex(rest);
            if (idx < 0)
                REJECT("feeder is not listed in [feeders]");
        }
        else if (!isPick)
            REJECT("FEED needs a feeder");
        if (dry)
            return true;
        if (idx < 0)
        {
            if (!part)
                REJECT("PICK without a feeder needs a current part");
            idx = part->feeder;
        }
        return isPick ? pick(idx) : feed(idx);
    }
    if (is(PSTR("PLACE")))
    {
        if (*rest)
            REJECT("PLACE takes no parameters");
        if (dry)
            return true;
        if (!part)
            REJECT("PLACE needs a current part");
        return place(*part);
    }
    if (is(PSTR("ROTATE")))
    {
        if (!pap::number(rest, v[0]))
            REJECT("ROTATE needs degrees");
        if (dry)
            return true;
        return rotateNozzle(v[0]);
    }
    if (is(PSTR("SPEED")) || is(PSTR("WAIT")))
    {
        if (!pap::number(rest, v[0]))
            REJECT("needs a number");
        if (is(PSTR("SPEED")) && v[0] <= 0)
            REJECT("SPEED must be > 0");
        if (is(PSTR("WAIT")) && v[0] < 0)
            REJECT("WAIT takes ms >= 0");
        if (dry)
            return true;
        if (is(PSTR("SPEED")))
            speed_ = v[0];
        else if (is(PSTR("WAIT")))
        {
            if (!sync())
                return false;
            PGM_P before = phase_;
            setPhase(PSTR("wait"));
            if (!waitMs((uint32_t)v[0]))
                return false;
            setPhase(before);
        }
        return true;
    }
    if (is(PSTR("PAUSE")))
    {
        const uint8_t len = strlen(rest);
        if (len < 2 || rest[0] != '"' || rest[len - 1] != '"')
            REJECT("PAUSE needs a \"message\"");
        if (dry)
            return true;
        if (!sync())
            return false;
        rest[len - 1] = '\0';
        strncpy(message_, rest + 1, sizeof(message_) - 1);
        message_[sizeof(message_) - 1] = '\0';
        contReq_ = false;
        state_ = RUN_MESSAGE;
        emit();
        Print &p = ports.all;
        p.print(F("!message"));
        out::str(p, PSTR("text"), message_);
        p.println();
        while (!contReq_ && abort_ == ABORT_NONE)
            service();
        message_[0] = '\0';
        if (state_ == RUN_MESSAGE)
            state_ = RUN_RUNNING;
        emit();
        return abort_ == ABORT_NONE;
    }
    if (is(PSTR("BEEP")))
    {
        float ms = 100;
        if (*rest && !pap::number(rest, ms))
            REJECT("BEEP takes ms");
        if (dry)
            return true;
        if (!sync())
            return false;
        Print &p = ports.all;
        p.print(F("!beep"));
        out::num(p, PSTR("ms"), (long)ms);
        p.println();
        return true;
    }
    if (is(PSTR("CALL")))
    {
        const int8_t i = funcIndex(rest);
        if (!*rest || i < 0)
            REJECT("no such function");
        if (dry)
            return true;
        return callFunc(i, part, depth + 1);
    }
    REJECT("unknown command");
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

bool Runner::yield()
{
    service();
    if (motion.alarm() && abort_ < ABORT_FAIL)
        fail(PSTR("motion alarm"), lineNo_);
    if (abort_ != ABORT_NONE)
    {
        if (motion.isBusy())
            motion.stop(); // decelerate, drop the rest of the queue
        return false;
    }
    if (pauseReq_ && !motion.isBusy())
    {
        const RunState before = state_;
        state_ = RUN_PAUSED;
        pauseStart_ = millis();
        emit();
        while (pauseReq_ && abort_ == ABORT_NONE)
            service();
        pausedMs_ += millis() - pauseStart_;
        state_ = before == RUN_MESSAGE ? RUN_MESSAGE : RUN_RUNNING;
        emit();
    }
    return abort_ == ABORT_NONE;
}

bool Runner::sync()
{
    while (motion.isBusy())
        if (!yield())
            return false;
    return yield();
}

bool Runner::waitMs(uint32_t ms)
{
    const uint32_t start = millis();
    while (millis() - start < ms)
        if (!yield())
            return false;
    return true;
}

bool Runner::moveMachine(float x, float y, float z, float feed)
{
    while (motion.queueSpace() == 0)
        if (!yield())
            return false;
    if (abort_ != ABORT_NONE)
        return false;
    const float cx = motion.getTargetPosition(X_AXIS);
    const float cy = motion.getTargetPosition(Y_AXIS);
    const float cz = motion.getTargetPosition(Z_AXIS);
    const float dz = fabsf(z - cz);
    const float len = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy) + dz * dz);
    if (len < 0.0005f)
        return true;
    float f = feed > 0 ? feed : speed_;
    // Z runs on a lead screw: keep its share of the move at its safe speed
    // unless speed_z sets another limit.
    const float zMax = mval("speed_z", Z_MAX_RATE);
    if (dz > 0.0005f)
        f = fminf(f, zMax * len / dz);
    if (!motion.moveTo(x, y, z, f))
        return fail(PSTR("move rejected"), lineNo_);
    return true;
}

// Seesaw Z: 0 = level (both heads up), + lowers head 1 (paste), - lowers
// head 2 (nozzle); one head goes up as far as the other goes down. A head's
// reach is the travel from level until its tip touches the bed, so its
// height above the bed is reach - Z (head 1) or reach + Z (head 2).
static float headSign(uint8_t head) { return head == 1 ? 1.0f : -1.0f; }

// Head 2 (vacuum) reaches further or less far with the nozzle on it (dz from the load cell touch-off)
static float headReach(uint8_t head)
{
    return settings.headReach[head - 1] + (head == 2 ? nozzles.offsetZ() : 0.0f);
}

float Runner::zForHeight(float h) const
{
    // Higher than its reach: the head can't go above level.
    return headSign(head_) * fmaxf(0.0f, headReach(head_) - h);
}

float Runner::heightAt(float z, uint8_t head) const
{
    return headReach(head) - headSign(head) * z;
}

void Runner::boardToMachine(float bx, float by, float bz, float *m) const
{
    const float *h = settings.head1Offset;
    const bool h1 = head_ == 1;
    float nx = origin_[0] + offset_[0] + bx, ny = origin_[1] + offset_[1] + by;
    if (fidValid_)
    {
        const float rx = nx - fidC_[0], ry = ny - fidC_[1];
        nx = fidA_[0] * rx + fidA_[1] * ry + fidC_[0] + fidT_[0];
        ny = fidA_[2] * rx + fidA_[3] * ry + fidC_[1] + fidT_[1];
    }
    m[0] = nx - (h1 ? h[0] : 0.0f);
    m[1] = ny - (h1 ? h[1] : 0.0f);
    m[2] = zForHeight(thickness_ + offset_[2] + bz);
}

void Runner::machineToBoard(float *b) const
{
    const float *h = settings.head1Offset;
    const bool h1 = head_ == 1;
    float nx = motion.getTargetPosition(X_AXIS) + (h1 ? h[0] : 0.0f);
    float ny = motion.getTargetPosition(Y_AXIS) + (h1 ? h[1] : 0.0f);
    if (fidValid_)
    {
        // inverse of A: nominal = A^-1 (measured - C - T) + C
        const float det = fidA_[0] * fidA_[3] - fidA_[1] * fidA_[2];
        const float rx = nx - fidC_[0] - fidT_[0], ry = ny - fidC_[1] - fidT_[1];
        nx = (fidA_[3] * rx - fidA_[1] * ry) / det + fidC_[0];
        ny = (-fidA_[2] * rx + fidA_[0] * ry) / det + fidC_[1];
    }
    b[0] = nx - origin_[0] - offset_[0];
    b[1] = ny - origin_[1] - offset_[1];
    b[2] = heightAt(motion.getTargetPosition(Z_AXIS), head_) - thickness_ - offset_[2];
}

// Both heads at least safe_height above the board (or level if a head can't
// reach that high): Z stays within [-(reach2 - safe), reach1 - safe].
bool Runner::liftToSafe()
{
    const float safe = thickness_ + offset_[2] + safeH_;
    const float zMax = fmaxf(0.0f, headReach(1) - safe);
    const float zMin = -fmaxf(0.0f, headReach(2) - safe);
    const float z = motion.getTargetPosition(Z_AXIS);
    if (z <= zMax + 0.0005f && z >= zMin - 0.0005f)
        return true;
    return moveMachine(motion.getTargetPosition(X_AXIS), motion.getTargetPosition(Y_AXIS),
                       fminf(zMax, fmaxf(zMin, z)), 0);
}

bool Runner::travelTo(float x, float y, float z)
{
    if (!liftToSafe())
        return false;
    return moveMachine(x, y, motion.getTargetPosition(Z_AXIS), 0) && moveMachine(x, y, z, 0);
}

// Nozzle rotation (A4988 #2, rotation driver): absolute angle, shortest way
// isn't used on purpose - the angle is a plain number, 0 = power-on angle.
bool Runner::rotateNozzle(float degrees)
{
    if (!sync())
        return false;
    if (fabsf(degrees - motion.nozzleAngle()) < 0.01f)
        return true;
    if (!motion.rotateNozzleTo(degrees, mval("rot_speed", 180)))
        return fail(PSTR("move rejected"), lineNo_);
    return true;
}

// Nozzle changer: the plan from ../nozzles.h, run with the job's own (abortable) moves
bool Runner::changeNozzle(int slot)
{
    if (!liftToSafe() || !sync())
        return false;
    for (const NozzleStep &s : nozzles.plan(slot))
    {
        switch (s.kind)
        {
        case NozzleStep::ROTATE0:
            if (!rotateNozzle(0))
                return false;
            break;
        case NozzleStep::MOVE:
            if (!moveMachine(s.x, s.y, s.z, s.speed) || !sync())
                return false;
            break;
        case NozzleStep::WAIT:
            if (!waitMs(s.ms))
                return false;
            break;
        case NozzleStep::MOUNTED:
            if (octo.simulated())
                nozzles.simApply(s);
            nozzles.setMounted(s.slot);
            break;
        case NozzleStep::CALIBRATE:
        {
            std::string err;
            if (!nozzles.calibrate(s.slot, err))
            {
                visionEvent("nozzle", err.c_str());
                return fail(PSTR("nozzle calibration failed (bottom camera)"), lineNo_);
            }
            char d[64];
            snprintf(d, sizeof(d), "slot=%d dx=%.3f dy=%.3f", s.slot, nozzles.slot(s.slot).dx, nozzles.slot(s.slot).dy);
            visionEvent("nozzle", d);
            break;
        }
        case NozzleStep::TOUCHOFF:
        {
            std::string err;
            const bool ok = nozzles.touchOff(s.slot, err);
            motion.invalidateTargets();
            if (!ok)
            {
                visionEvent("nozzle", err.c_str());
                return fail(PSTR("nozzle touch-off failed (load cell)"), lineNo_);
            }
            char d[48];
            snprintf(d, sizeof(d), "slot=%d dz=%.3f", s.slot, nozzles.slot(s.slot).dz);
            visionEvent("nozzle", d);
            break;
        }
        case NozzleStep::EXPECT:
        {
            bool there = false;
            if (!nozzles.slotShows(s.slot, s.present, there))
                return fail(PSTR("nozzle sensors not readable"), lineNo_);
            if (there != s.present)
            {
                char t[64];
                snprintf(t, sizeof(t), "nozzle slot %d: %s", s.slot,
                         s.present ? "no nozzle seen in the slot" : "slot is not empty");
                visionEvent("nozzle", t);
                return fail(s.present ? PSTR("nozzle change: no nozzle in the slot")
                                      : PSTR("nozzle change: slot is not empty"),
                            lineNo_);
            }
            break;
        }
        }
    }
    return abort_ == ABORT_NONE;
}

bool Runner::pasteDot(float amount)
{
    if (!sync())
        return false;
    if (settings.pasteOutput == PASTE_MOTOR)
    {
        // Microlitres on the Octopus paste driver (PASTE): push the dot plus what
        // was pulled back after the last one, then pull back again.
        const float retract = settings.pasteRetract;
        const float push = amount + (pasteRetracted_ ? retract : 0.0f);
        if (!motion.pasteMicrolitres(push))
            return fail(PSTR("move rejected"), lineNo_);
        pasteRetracted_ = false;
        if (retract > 0.0f)
        {
            if (!motion.pasteMicrolitres(-retract))
                return fail(PSTR("move rejected"), lineNo_);
            pasteRetracted_ = true;
        }
        return sync();
    }
    return true; // paste_output none: no dispenser fitted, PASTE lines do nothing
}

bool Runner::pick(uint8_t idx)
{
    if (idx >= feederCount_ || !feederNum_[idx])
        return fail(PSTR("feeder is not on the machine"), lineNo_);
    // Copy what is needed: `feeder` may be reused by commands while we wait.
    if (!feeders.load(feederNum_[idx], feeder))
        return fail(PSTR("can't read the feeder file"), lineNo_);
    if (!feeder.pocketCount)
        return fail(PSTR("feeder has no pockets"), lineNo_);
    const uint8_t pocket = feeder.next;
    const float px = feeder.pockets[pocket - 1].x;
    const float py = feeder.pockets[pocket - 1].y;
    const float feederPos = feeder.feederPos;
    const float advance = FeederStore::advanceDeg(feeder);
    const bool advanceAfter = pocket >= feeder.pocketCount;
    feeder.next = advanceAfter ? 1 : pocket + 1;
    feeders.save(feeder); // remembered even if the job stops now

    head_ = 2;
    float cx = px, cy = py;
    if (settings.visionPockets && vision::available(vision::TOP))
    {
        std::string err;
        float dx, dy;
        if (lookTop(px, py) && vision::findPocket(dx, dy, err))
        {
            cx += dx;
            cy += dy;
        }
        else if (abort_ != ABORT_NONE)
            return false;
        else
            visionEvent("pocket", err.c_str());
        mosfets.topLightIdle();
    }
    cx -= nozzles.offsetX(); // nozzle tip offset (0 / 0 unless set per rack slot)
    cy -= nozzles.offsetY();
    for (uint8_t attempt = 0;; attempt++)
    {
        if (!travelTo(cx, cy, zForHeight(settings.pickHeight)) || !sync())
            return false;
        mosfets.openValve();
        holding_ = true;
        if (!waitMs(settings.pickMs) || !liftToSafe() || !sync())
            return false;
        if (!settings.vacCheck || !vision::vacuumAvailable())
            break;
        if (settings.vacWaitMs && !waitMs(settings.vacWaitMs))
            return false;
        if (vision::partOnNozzle())
            break;
        mosfets.closeValve();
        holding_ = false;
        if (attempt >= settings.vacRetries)
            return skipPart("no part on the nozzle (vacuum)");
        visionEvent("vacuum", "no part on the nozzle, picking again");
    }
    if (advanceAfter)
    {
        // Pockets used up: E1 drives to the feeder, E0 turns it.
        while (motion.queueSpace() < 2)
            if (!yield())
                return false;
#if HAS_E1
        motion.moveE1To(feederPos, mval("e1_speed", 30));
#endif
        motion.rotate(advance, mval("e0_speed", 180));
    }
    return true;
}

bool Runner::feed(uint8_t idx)
{
    if (idx >= feederCount_ || !feederNum_[idx])
        return fail(PSTR("feeder is not on the machine"), lineNo_);
    if (!feeders.load(feederNum_[idx], feeder))
        return fail(PSTR("can't read the feeder file"), lineNo_);
    const float feederPos = feeder.feederPos;
    const float advance = FeederStore::advanceDeg(feeder);
    feeder.next = 1;
    feeders.save(feeder);
    while (motion.queueSpace() < 2)
        if (!yield())
            return false;
#if HAS_E1
    motion.moveE1To(feederPos, mval("e1_speed", 30));
#endif
    motion.rotate(advance, mval("e0_speed", 180));
    return sync();
}

bool Runner::place(const Part &part)
{
    head_ = 2;
    if (!holding_)
        return fail(PSTR("PLACE without a picked part"), lineNo_);
    float rotation = part.rotation + fidRot_;
    float corr[2] = {0, 0};
    bool measured = false; // the bottom camera saw the part: its offset already contains the nozzle's
    if (settings.visionParts && vision::available(vision::BOTTOM))
    {
        // Nozzle (at 0 deg) above the bottom camera: where does the part hang?
        if (!liftToSafe() ||
            !moveMachine(settings.camBottomPos[0], settings.camBottomPos[1], motion.getTargetPosition(Z_AXIS), 0) ||
            !moveMachine(settings.camBottomPos[0], settings.camBottomPos[1], settings.camBottomPos[2], 0) || !sync())
            return false;
        mosfets.bottomLight(true); // part colour (red: pads / leads stand out)
        const bool waited = waitMs(settings.camSettleMs);
        vision::PartResult r;
        const bool seen = waited && vision::measurePart(r);
        mosfets.setLight(false, 0);
        if (!waited)
            return false;
        if (!seen)
        {
            if (settings.visionRequired)
                return skipPart(r.error.empty() ? "part not seen by the bottom camera" : r.error.c_str());
            visionEvent("part", r.error.c_str());
        }
        else
        {
            if (fabsf(r.angle) > settings.visionMaxAngle || hypotf(r.dx, r.dy) > settings.visionMaxOffset)
                return skipPart("part sits too far off on the nozzle");
            // Turn so the part ends at `rotation`; its offset turns with the nozzle.
            const float phi = rotation - r.angle;
            const float turn = r.rotSign * phi * 3.14159265f / 180.0f; // how the offset turns in machine axes
            const float c = cosf(turn), s = sinf(turn);
            corr[0] = c * r.dx - s * r.dy;
            corr[1] = s * r.dx + c * r.dy;
            rotation = phi;
            measured = true;
            char d[96];
            snprintf(d, sizeof(d), "dx=%.3f dy=%.3f angle=%.2f", r.dx, r.dy, r.angle);
            visionEvent("part", d);
        }
        if (!liftToSafe())
            return false;
    }
    // Turn the part (it hangs at the safe height after the pick).
    if (rotation != 0.0f && !rotateNozzle(rotation))
        return false;
    float m[3];
    boardToMachine(part.at[0], part.at[1], placeH_, m);
    if (!measured)
    {
        // Not measured: the part hangs at the nozzle tip, which turns with the nozzle around the axis
        const float t = settings.camBottomRotSign * rotation * 3.14159265f / 180.0f;
        const float ox = nozzles.offsetX(), oy = nozzles.offsetY();
        corr[0] = cosf(t) * ox - sinf(t) * oy;
        corr[1] = sinf(t) * ox + cosf(t) * oy;
    }
    m[0] -= corr[0];
    m[1] -= corr[1];
    if (!travelTo(m[0], m[1], m[2]) || !sync())
        return false;
    mosfets.closeValve();
    holding_ = false;
    if (!waitMs(settings.placeMs) || !liftToSafe())
        return false;
    if (rotation != 0.0f && !rotateNozzle(0)) // back for the next pick
        return false;
    if (settings.visionPlaceCheck && vision::available(vision::TOP))
    {
        float b[3];
        boardToMachine(part.at[0], part.at[1], placeH_, b);
        if (lookTop(b[0], b[1]))
        {
            char path[PATH_MAX_LEN];
            snprintf(path, sizeof(path), "/captures/%s_%s.jpg", name_, part.ref);
            vision::capture(vision::TOP, path);
            visionEvent("capture", path);
        }
        mosfets.topLightIdle();
    }
    return abort_ == ABORT_NONE;
}

// ---------------------------------------------------------------------------
// Vision helpers (Pi)
// ---------------------------------------------------------------------------

void Runner::visionEvent(const char *what, const char *detail)
{
    Print &p = ports.all;
    p.print(F("!vision"));
    out::str(p, PSTR("what"), what);
    out::str(p, PSTR("part"), partIndex_ >= 0 ? part_.ref : "");
    out::str(p, PSTR("text"), detail ? detail : "");
    p.println();
}

bool Runner::skipPart(const char *why)
{
    visionEvent("skip", why);
    if (abort_ == ABORT_NONE)
        abort_ = ABORT_SKIP;
    return false;
}

bool Runner::lookTop(float mx, float my)
{
    head_ = 2;
    const float nx = mx - settings.camTopOffset[0], ny = my - settings.camTopOffset[1];
    if (!liftToSafe() || !moveMachine(nx, ny, motion.getTargetPosition(Z_AXIS), 0) || !sync())
        return false;
    mosfets.setLight(true, settings.lightTop / 100.0f);
    return waitMs(settings.camSettleMs);
}

bool Runner::measureFiducials()
{
    setPhase(PSTR("fiducials"));
    float nominal[PAP_MAX_FIDUCIALS][2], measured[PAP_MAX_FIDUCIALS][2];
    const uint8_t h = head_;
    head_ = 2;
    for (uint8_t i = 0; i < fidCount_; i++)
    {
        float m[3];
        boardToMachine(fid_[i][0], fid_[i][1], 0, m);
        nominal[i][0] = m[0];
        nominal[i][1] = m[1];
        float dx = 0, dy = 0;
        std::string err;
        if (!lookTop(m[0], m[1]) || !vision::findFiducial(dx, dy, err))
        {
            mosfets.topLightIdle();
            head_ = h;
            char d[96];
            snprintf(d, sizeof(d), "fiducial %u: %s", (unsigned)(i + 1), err.empty() ? "aborted" : err.c_str());
            visionEvent("fiducial", d);
            return false;
        }
        measured[i][0] = m[0] + dx;
        measured[i][1] = m[1] + dy;
    }
    mosfets.topLightIdle();
    head_ = h;
    // Similarity from the first two (rotation + offset), scale only with three
    const float vx = nominal[1][0] - nominal[0][0], vy = nominal[1][1] - nominal[0][1];
    const float wx = measured[1][0] - measured[0][0], wy = measured[1][1] - measured[0][1];
    const float rot = atan2f(wy, wx) - atan2f(vy, vx);
    float scale = 1.0f;
    if (fidCount_ >= 3)
        scale = hypotf(wx, wy) / fmaxf(hypotf(vx, vy), 1e-6f);
    const float c = cosf(rot) * scale, s = sinf(rot) * scale;
    fidA_[0] = c;
    fidA_[1] = -s;
    fidA_[2] = s;
    fidA_[3] = c;
    fidC_[0] = nominal[0][0];
    fidC_[1] = nominal[0][1];
    fidT_[0] = measured[0][0] - nominal[0][0];
    fidT_[1] = measured[0][1] - nominal[0][1];
    fidRot_ = rot * 180.0f / 3.14159265f;
    fidValid_ = true;
    char d[96];
    snprintf(d, sizeof(d), "dx=%.3f dy=%.3f rot=%.3f scale=%.5f", fidT_[0], fidT_[1], fidRot_, scale);
    visionEvent("fiducials", d);
    return true;
}
