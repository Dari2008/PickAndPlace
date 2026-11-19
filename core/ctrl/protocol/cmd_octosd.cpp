// OCTOSD - the microSD card in the Octopus slot (read only): list it, copy files to the Pi
//
//   OCTOSD LS [folder]              file name= size= dir=0|1 ... ok
//   OCTOSD COPY <file> [<pi path>]  copies the file into the Pi's data folder (default /jobs/<name>):
//                                   saved path= bytes= ok
// Jobs run from the Pi's folder; the Octopus card is only a place to bring files from.
#include "commands.h"
#include "../../octo/octopus.h"
#include "../settings/storage.h"

#include <cstdio>
#include <fstream>

void cmdOctoSd(Port &port, Args &args)
{
    Print &p = port.io;
    if (args.is(1, PSTR("LS")))
    {
        const std::string dir = args.count() > 2 ? args[2] : "/";
        const OctoReply r = octo.call("SDLS " + dir, 15000);
        if (!r.ok)
        {
            out::error(p, (r.error.empty() ? std::string("motion board offline") : r.error).c_str());
            return;
        }
        for (auto &rec : r.records)
            if (rec.type == "file")
            {
                out::rec(p, PSTR("file"));
                out::str(p, PSTR("name"), rec.str("name").c_str());
                out::num(p, PSTR("size"), rec.integer("size"));
                out::num(p, PSTR("dir"), rec.integer("dir"));
                p.println();
            }
        out::ok(p);
        return;
    }
    if (!args.is(1, PSTR("COPY")) || args.count() < 3)
    {
        out::error(p, PSTR("usage: OCTOSD LS [folder] | COPY <file> [<pi path>]"));
        return;
    }
    if (commands::refuseIfBusy(port))
        return;
    const std::string src = args[2];
    std::string name = src.substr(src.find_last_of('/') == std::string::npos ? 0 : src.find_last_of('/') + 1);
    std::string dest = args.count() > 3 ? std::string(args[3]) : "/jobs/" + name;
    if (dest.empty() || dest[0] != '/' || dest.find("..") != std::string::npos || name.empty())
    {
        out::error(p, PSTR("bad path"));
        return;
    }
    storage.fs().mkdir(dest.substr(0, dest.find_last_of('/')).c_str(), true);
    const std::string real = sdfs_path(dest.c_str()), tmp = real + ".part";
    std::ofstream f(tmp, std::ios::binary);
    if (!f)
    {
        out::error(p, PSTR("can't write to the Pi's data folder"));
        return;
    }
    unsigned long offset = 0, bytes = 0;
    for (int chunk = 0; chunk < 100000; chunk++)
    {
        const OctoReply r = octo.call("SDREAD " + src + " " + std::to_string(offset), 15000);
        if (!r.ok)
        {
            f.close();
            std::remove(tmp.c_str());
            out::error(p, (r.error.empty() ? std::string("motion board offline") : r.error).c_str());
            return;
        }
        bool eof = false;
        for (auto &rec : r.records)
        {
            if (rec.type == "|")
            {
                f << rec.text << "\n";
                bytes += rec.text.size() + 1;
            }
            else if (rec.type == "sdread")
            {
                offset = (unsigned long)rec.integer("offset");
                eof = rec.flag("eof");
            }
        }
        if (eof)
            break;
    }
    f.close();
    if (std::rename(tmp.c_str(), real.c_str()) != 0)
    {
        std::remove(tmp.c_str());
        out::error(p, PSTR("can't save the file"));
        return;
    }
    out::rec(p, PSTR("saved"));
    out::str(p, PSTR("path"), dest.c_str());
    out::num(p, PSTR("bytes"), (long)bytes);
    p.println();
    out::ok(p);
}
