/**
 * commands.h - command table and dispatcher (protocol.md)
 * -----------------------------------------------------------------------------
 * The handlers live in cmd_*.cpp, grouped like protocol.md:
 *   cmd_general.cpp   HELP INFO STATUS REPORT WAIT
 *   cmd_files.cpp     SD PWD CD LS MKDIR RMDIR RM MV CAT UPLOAD
 *   cmd_job.cpp       CHECK RUN PAUSE RESUME CONTINUE SKIP STOP JOB
 *   cmd_machine.cpp   HOME MOVE JOG E1 E0 MOTOR SETPOS POS MOTORS HALT ESTOP
 *                     CLEAR SENSORS PUMP VALVE OUTPUTS
 *   cmd_settings.cpp  GET SET RELOAD DEFAULTS
 *   cmd_feeders.cpp   FEEDERS FEEDER POCKET SCAN NFC
 * -----------------------------------------------------------------------------
 */
#pragma once

#include "args.h"
#include "out.h"
#include "ports.h"
#include <string>

typedef void (*CommandHandler)(Port &port, Args &args);

namespace commands
{
    void execute(Port &port, char *line);

    // "!status ..." (+ "!job ..." while a job runs) for REPORT.
    void report(Print &p);

    // The "status ..." record fields (without the type word).
    void printStatus(Print &p);

    // Machine busy with a job or auto detect: prints the error and returns true.
    bool refuseIfBusy(Port &port);

    // Called for every incoming line before it is queued: HALT / ESTOP go to the Octopus at
    // once, so they also stop a HOME / SGTEST that blocks the command loop. The line is still
    // run normally afterwards.
    void urgentLine(const std::string &line);

    // The command line being executed, as it came in (for passing it on to the Octopus)
    const std::string &currentLine();
    // Resolve a path argument against the port's working folder into `out`
    // (PATH_MAX_LEN). False if too long.
    bool resolvePath(const Port &port, const char *arg, char *out);
}

// Status helpers: "pump= valve= cam_fan= box_fan= pi_fan= light_top= light_bottom=" fields (cmd_machine.cpp), card mounted (cmd_files.cpp)
void printOutputs(Print &p);
void printExtraStatus(Print &p); // cmd_vision.cpp: vac= cams= nozzle=
bool sdMounted();

// Handlers (cmd_*.cpp)
void cmdHelp(Port &, Args &);
void cmdInfo(Port &, Args &);
void cmdStatus(Port &, Args &);
void cmdReport(Port &, Args &);
void cmdWait(Port &, Args &);
void cmdLog(Port &, Args &);
void cmdRotate(Port &, Args &);
void cmdPaste(Port &, Args &);
float nozzleAngle(); // cmd_machine.cpp (HAS_TOOL)

void cmdSd(Port &, Args &);
void cmdPwd(Port &, Args &);
void cmdCd(Port &, Args &);
void cmdLs(Port &, Args &);
void cmdMkdir(Port &, Args &);
void cmdRmdir(Port &, Args &);
void cmdRm(Port &, Args &);
void cmdMv(Port &, Args &);
void cmdCat(Port &, Args &);
void cmdUpload(Port &, Args &);
// An upload is running on `port`: handle its data / END / ABORT line.
bool uploadActive(const Port &port);
void uploadLine(Port &port, char *line);
// Progress of the running / last upload (display banner); thread-safe
struct UploadInfo
{
    bool active = false, ok = false, fromUi = false;
    std::string path, error;
    size_t done = 0, total = 0;
    uint32_t endedAt = 0;
};
UploadInfo uploadInfo();

void cmdCheck(Port &, Args &);
void cmdRun(Port &, Args &);
void cmdPause(Port &, Args &);
void cmdResume(Port &, Args &);
void cmdContinue(Port &, Args &);
void cmdSkip(Port &, Args &);
void cmdStop(Port &, Args &);
void cmdJob(Port &, Args &);

void cmdHome(Port &, Args &);
void cmdMove(Port &, Args &);
void cmdJog(Port &, Args &);
void cmdJogV(Port &, Args &);
void cmdE1(Port &, Args &);
void cmdE0(Port &, Args &);
void cmdMotor(Port &, Args &);
void cmdSetPos(Port &, Args &);
void cmdPos(Port &, Args &);
void cmdMotors(Port &, Args &);
void cmdHalt(Port &, Args &);
void cmdEstop(Port &, Args &);
void cmdClear(Port &, Args &);
void cmdSensors(Port &, Args &);
void cmdPump(Port &, Args &);
void cmdValve(Port &, Args &);
void cmdOutputs(Port &, Args &);

void cmdGet(Port &, Args &);
void cmdSet(Port &, Args &);
void cmdReload(Port &, Args &);
void cmdDefaults(Port &, Args &);

void cmdLight(Port &, Args &);
void cmdTmc(Port &, Args &);
void cmdOcto(Port &, Args &);
// cmd_vision.cpp: cameras, vision, vacuum sensor, joystick, calibrations
void cmdCam(Port &, Args &);
void cmdVacuum(Port &, Args &);
void cmdJoy(Port &, Args &);
void cmdCal(Port &, Args &);
void cmdScreen(Port &, Args &);
// cmd_nozzles.cpp: nozzle changer
void cmdNozzles(Port &, Args &);
void cmdSettings(Port &, Args &); // cmd_settings.cpp: the settings catalog
void cmdLoadcell(Port &, Args &); // cmd_machine.cpp: load cell on the Octopus
void cmdSgTest(Port &, Args &);   // cmd_machine.cpp: StallGuard measurement (sensorless homing)
void cmdFindLen(Port &, Args &);  // cmd_machine.cpp: travel length of X / Y / the gantry
void cmdOctoSd(Port &, Args &);   // cmd_octosd.cpp: the Octopus microSD card
void cmdNozzle(Port &, Args &);
void cmdFeeders(Port &, Args &);
void cmdFeeder(Port &, Args &);
void cmdPocket(Port &, Args &);
void cmdScan(Port &, Args &);
void cmdNfc(Port &, Args &);
