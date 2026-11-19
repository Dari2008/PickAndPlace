/**
 * runner.h - runs .pap job files from the SD card (pap-format.md)
 * -----------------------------------------------------------------------------
 *   runner.check(path, &out)   problems as "problem line= text=" records
 *   runner.start(path, out)    check, then start (prints "error ..." if not)
 *   runner.update()            from loop(): runs a started job to its end
 *   pause() resume() cont() skip() stop()   from the protocol, any time
 *
 * The file is never loaded as a whole (8 KB RAM). Before a job starts, one
 * pass over the file checks it and keeps only what is needed later: the job
 * and board values, the feeders of [feeders] (matched to feeder files) and
 * where each [func] starts in the file. While running, parts are read one at a
 * time, and functions are run by seeking to their position and back.
 *
 * update() only returns when the job is over. While it waits for the
 * machine it calls service() (main.cpp), so commands keep working: STATUS,
 * JOB, PAUSE, STOP ... and other ports.
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <Arduino.h>
#include <SdFat.h>
#define PAP_MAX_FIDUCIALS 3
#include "pap_text.h"
#include "../protocol/ports.h"
#include "../settings/feeder_store.h"

#define PAP_MAX_FUNCS 32
#define PAP_MAX_HOOKS 4  // functions per run_before / run_after
#define PAP_MAX_DOTS 16
#define PAP_CALL_DEPTH 4

enum RunState : uint8_t
{
    RUN_IDLE,
    RUN_STARTING, // start() accepted, update() not entered yet
    RUN_RUNNING,
    RUN_PAUSED,
    RUN_MESSAGE, // PAUSE "message": waits for CONTINUE
    RUN_DONE,
    RUN_STOPPED,
    RUN_FAILED
};

class Runner
{
public:
    // Check a file; prints problems to `out` (may be nullptr). Returns the
    // number of problems (0 = fine). Leaves the checked values in place.
    uint8_t check(const char *path, Print *out);

    bool start(const char *path, Print &out);
    void update();

    void pause();
    void resume();
    void cont();
    void skip();
    void stop();

    bool active() const { return state_ >= RUN_STARTING && state_ <= RUN_MESSAGE; }
    RunState state() const { return state_; }
    PGM_P stateName() const;
    const char *file() const { return path_; }

    // "job ..." record fields.
    void printJob(Print &p);
    // "summary parts= dots= feeders= funcs=" of the last check (no type word).
    void printSummary(Print &p);

private:
    struct Part
    {
        char ref[8];
        uint16_t line;
        uint8_t feeder; // index into feeders_ (0xFF = none)
        bool hasAt;
        float at[2];
        float rotation;
        bool hasAmount;
        float amount;
        uint8_t dotCount;
        float dots[PAP_MAX_DOTS][2];
        uint8_t before[PAP_MAX_HOOKS], after[PAP_MAX_HOOKS]; // function indices, 0xFF = end
        uint8_t nozzle; // rack slot the part needs, 0 = the [job] nozzle (or keep the current one)
    };

    // ---- checking -----------------------------------------------------------
    void problem(uint16_t line, PGM_P text, const char *detail = nullptr);
    bool indexFile(); // pass 1: sections, feeders, functions, job/board values
    void checkFile(); // pass 2: parts and function bodies
    bool parseJobKey(const char *key, const char *value, uint16_t line);
    bool parseBoardKey(const char *key, const char *value, uint16_t line);
    bool parsePartKey(Part &part, const char *key, char *value, uint16_t line, bool report);
    bool parseHooks(char *value, uint8_t *out, uint16_t line, bool report);
    int8_t funcIndex(const char *name) const;
    int8_t feederIndex(const char *name) const;

    // ---- running ------------------------------------------------------------
    void runJob();
    bool runHooks(const uint8_t *hooks, Part *part);
    bool readPart(Part &part);  // from the current file position (after the header)
    void lookAhead();           // next part's ref for the status
    bool runPart(Part &part);
    bool callFunc(uint8_t index, Part *part, uint8_t depth);
    bool exec(char *line, Part *part, uint8_t depth, bool dry, uint16_t lineNo);
    void afterAbort();

    // motion and timing (all return false when the job is aborted)
    bool yield();
    bool sync();
    bool waitMs(uint32_t ms);
    bool moveMachine(float x, float y, float z, float feed);
    bool travelTo(float x, float y, float z);
    bool liftToSafe();
    float zForHeight(float h) const;            // seesaw Z: active head h mm above the bed
    float heightAt(float z, uint8_t head) const; // that head's height above the bed at Z
    void boardToMachine(float bx, float by, float bz, float *m) const;
    void machineToBoard(float *b) const;
    bool rotateNozzle(float degrees); // absolute nozzle angle (queued)
    bool changeNozzle(int slot);      // nozzle changer: put back the mounted one, pick up `slot` (../nozzles.h)
    bool pick(uint8_t feederIdx);
    bool place(const Part &part);
    bool pasteDot(float amount);
    bool feed(uint8_t feederIdx);
    bool fail(PGM_P text, uint16_t line);
    // ---- vision (Pi) ----------------------------------------------------------
    bool lookTop(float mx, float my);  // top camera above machine point (mx, my), settled, light on
    bool measureFiducials();           // job start: board offset / rotation (/ scale) from [board] fiducial = x, y
    bool skipPart(const char *why);    // bad pick: the part is dropped / rejected, the job goes on
    void visionEvent(const char *what, const char *detail);
    void setPhase(PGM_P phase);
    void emit();
    uint32_t elapsedMs() const;

    // ---- checked values -----------------------------------------------------
    char path_[PATH_MAX_LEN] = "";
    Print *out_ = nullptr;
    uint8_t problems_ = 0;
    bool hasJob_, hasBoard_, hasFeeders_, hasOrigin_;
    char name_[24];
    float jobSpeed_;   // 0 = not given
    float jobAmount_;
    uint8_t jobNozzle_; // [job] nozzle =, 0 = none
    uint8_t jobBefore_[PAP_MAX_HOOKS], jobAfter_[PAP_MAX_HOOKS];
    float origin_[2], thickness_, pasteH_, placeH_, safeH_, boardOffset_[3];
    uint8_t feederCount_;
    uint16_t feederHash_[MAX_FEEDERS];
    uint8_t feederNum_[MAX_FEEDERS];
    uint8_t funcCount_;
    struct Func
    {
        uint16_t hash;
        uint16_t line;
        uint32_t pos; // first byte after the header line
    } funcs_[PAP_MAX_FUNCS];
    uint16_t total_;
    uint8_t fidCount_ = 0;
    float fid_[PAP_MAX_FIDUCIALS][2];   // board coordinates of the fiducials
    bool fidValid_ = false;             // measured: positions are corrected
    float fidA_[4] = {1, 0, 0, 1};      // measured = A * (nominal - C) + C + T
    float fidC_[2] = {0, 0}, fidT_[2] = {0, 0};
    float fidRot_ = 0;                  // deg, added to every part rotation
    uint16_t dots_; // paste dots (from "paste =") in the checked file

    // ---- running state ------------------------------------------------------
    volatile RunState state_ = RUN_IDLE;
    File f_;
    uint16_t lineNo_;
    Part part_;
    char nextRef_[8];
    int16_t partIndex_;
    uint16_t done_, skipped_;
    PGM_P phase_ = nullptr;
    uint8_t head_;
    float speed_;
    float offset_[3];
    bool holding_;
    bool pasteRetracted_ = false; // pulled back after the last dot (kept between jobs: it is physical)
    bool pauseReq_, contReq_;
    uint8_t abort_; // ABORT_*
    bool stopping_; // running [job] run_after after a stop
    char message_[40];
    char error_[48];
    uint32_t startedAt_, pausedMs_, pauseStart_, endedAt_, lastPartDoneMs_;
};

extern Runner runner;

// main.cpp: keeps the ports going while the runner waits.
void service();
