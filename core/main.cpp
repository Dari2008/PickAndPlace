/**
 * pap-core - the pick & place controller on the Raspberry Pi 5
 * -----------------------------------------------------------------------------
 * Runs the jobs, feeders, settings and files (ported ATmega controller, ctrl/),
 * talks to the BTT Octopus over USB (octo/), reads the cameras, vacuum sensor
 * and joystick (vision/, dev/), drives the 3.5" display (ui/) and serves the
 * text protocol on TCP for the website (api/).
 *
 *   pap-core [--data ~/pap-data] [--octopus /dev/ttyACM0|sim] [--sim] [--api-port 7070]
 *            [--api-bind 127.0.0.1] [--no-display] [--console] [--flash [file.bin]] [--flash-only [file.bin]]
 *   --sim        Octopus, cameras and sensors simulated (development on a PC)
 *   --flash      flash the Octopus first (default ../firmware/pap-motion.bin, octo/flash.h), then run
 *   --flash-only flash the Octopus and exit (0 = flashed; upload.bat -flash)
 *   --console    commands from stdin, answers on stdout (service)
 * -----------------------------------------------------------------------------
 */
#include <Arduino.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include "api/tcp_server.h"
#include "ctrl/feeder_scan.h"
#include "ctrl/nozzles.h"
#include "ctrl/job/runner.h"
#include "ctrl/mosfets.h"
#include "ctrl/protocol/commands.h"
#include "ctrl/protocol/ports.h"
#include "ctrl/settings/feeder_store.h"
#include "ctrl/settings/general_settings.h"
#include "ctrl/settings/machine_store.h"
#include "ctrl/settings/storage.h"
#include "dev/joystick.h"
#include "dev/sensors.h"
#include "octo/flash.h"
#include "octo/octopus.h"
#include "ui/ui_main.h"
#include "vision/camera.h"

void announceReady(); // ctrl/protocol/cmd_general.cpp

static volatile sig_atomic_t g_quit = 0;
static void onSignal(int) { g_quit = 1; }

// After the Octopus (re)connects: reports on, machine values, positions
static void initOctopus()
{
    if (!octo.initPending())
        return;
    octo.initDone();
    if (!octo.call("REPORT 50").ok)
    {
        octo.call("PING"); // try again with the next connect event
        return;
    }
    std::string err;
    const int bad = machineStore::pushAll(err);
    machineStore::fetch();
    motion.syncTargets();
    Print &p = ports.all;
    p.print(F("!octopus connected=1"));
    if (bad)
    {
        p.print(F(" error="));
        p.print(("\"machine.txt: " + err + "\"").c_str());
    }
    p.println();
    std::cerr << "[core] Octopus connected (" << octo.device() << ")" << (bad ? " - " + err : "") << std::endl;
}

// Called by the runner whenever it waits for the machine (ATmega main.cpp kept that name)
void service()
{
    initOctopus();
    ports.poll();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
}

static std::string defaultData()
{
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/pap-data";
}

// The USB device of the Octopus firmware (product "pap-motion"), else /dev/ttyACM0
static std::string findOctopus()
{
    if (FILE *f = popen("ls /dev/serial/by-id/ 2>/dev/null | grep -i pap-motion | head -1", "r"))
    {
        char buf[256] = "";
        const bool got = fgets(buf, sizeof(buf), f) != nullptr;
        pclose(f);
        if (got && buf[0])
        {
            std::string n = buf;
            while (!n.empty() && (n.back() == '\n' || n.back() == '\r'))
                n.pop_back();
            return "/dev/serial/by-id/" + n;
        }
    }
    return "/dev/ttyACM0";
}

int main(int argc, char **argv)
{
    std::string data = defaultData(), octoDev, apiBind = "127.0.0.1";
    int apiPort = 7070;
    bool sim = false, display = true, console = false, flash = false, flashOnly = false;
    std::string flashFile;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << what << " needs a value\n";
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--data")
            data = next("--data");
        else if (a == "--octopus")
            octoDev = next("--octopus");
        else if (a == "--sim")
            sim = true;
        else if (a == "--api-port")
            apiPort = atoi(next("--api-port").c_str());
        else if (a == "--api-bind")
            apiBind = next("--api-bind");
        else if (a == "--no-display")
            display = false;
        else if (a == "--console")
            console = true;
        else if (a == "--flash" || a == "--flash-only")
        {
            flash = true;
            flashOnly = a == "--flash-only";
            if (i + 1 < argc && argv[i + 1][0] != '-')
                flashFile = argv[++i];
        }
        else
        {
            std::cerr << "usage: pap-core [--data DIR] [--octopus DEV|sim] [--sim] [--api-port N] [--api-bind ADDR]"
                         " [--no-display] [--console] [--flash [FILE.bin]] [--flash-only [FILE.bin]]\n";
            return 2;
        }
    }
    if (flash)
    {
        bool ok = false;
        if (sim || octoDev == "sim")
            std::cerr << "[flash] not with a simulated Octopus, skipped" << std::endl;
        else if (!(ok = flashOctopus(octoDev.empty() ? findOctopus() : octoDev,
                                     flashFile.empty() ? defaultFirmwarePath() : flashFile)))
            std::cerr << (flashOnly ? "[flash] FAILED" : "[flash] FAILED - starting anyway") << std::endl;
        if (flashOnly)
            return ok ? 0 : 1;
    }
    if (octoDev.empty())
        octoDev = sim ? "sim" : findOctopus();

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);

    sdfs_set_root(data);
    if (!storage.ready())
    {
        std::cerr << "[core] can't use the data folder " << data << std::endl;
        return 1;
    }
    settings.load();
    machineStore::load();
    feeders.scan();
    nozzles.load();

    octo.start(octoDev);
    // SGTEST live values (!sg) from the board to the display and the website, as one write per
    // line. Only sent while SGTEST blocks the command loop, so nothing else writes to the ports.
    octo.onEvent([](const Record &e) {
        if (e.type != "sg")
            return;
        const std::string line = e.text + "\n";
        ports.all.write((const uint8_t *)line.data(), line.size());
    });
    sensors.start("/dev/i2c-1", sim);
    joystick.begin(17, true); // your stick drives 3V3 when pressed (false: switch to GND)
    cams.setSimulation(sim);

    TcpServer api;
    if (!api.start(apiBind, apiPort))
        std::cerr << "[core] API port " << apiPort << " not available" << std::endl;

    if (display)
        panel::start(sim);

    std::shared_ptr<Channel> con;
    std::thread conIn;
    if (console)
    {
        con = std::make_shared<Channel>("console");
        ports.add(con);
        conIn = std::thread([con] {
            std::string l;
            while (std::getline(std::cin, l))
                con->pushLine(l);
            con->close();
        });
        conIn.detach();
        std::thread([con] {
            std::string l;
            while (!con->closed())
                if (con->popLine(l, 200))
                    std::cout << l << std::endl;
        }).detach();
    }

    std::cerr << "[core] data " << data << ", octopus " << octoDev << ", api " << apiBind << ":" << apiPort
              << (sim ? ", simulation" : "") << std::endl;
    announceReady();

    while (!g_quit)
    {
        service();
        feederScan.update();
        runner.update(); // returns when a started job is over
    }
    std::cerr << "[core] stopping" << std::endl;
    panel::stop();
    api.stop();
    if (octo.connected())
    {
        octo.call("VALVE OFF");
        octo.call("PUMP OFF");
    }
    cams.closeAll();
    sensors.stop();
    octo.stop();
    return 0;
}
