// Jobs list, job chosen, check problems, running job
#include <algorithm>
#include "screens.h"

JobsScreen jobsScreen;
JobConfirmScreen jobConfirmScreen;
JobProblemsScreen jobProblemsScreen;
JobScreen jobScreen;

static std::string joinPath(const std::string &dir, const std::string &name)
{
    return (dir == "/" ? "" : dir) + "/" + name;
}

// ---------------------------------------------------------------- jobs list

enum
{
    J_EMPTY_REFRESH = 2,
    J_ROWS = 10
};

void JobsScreen::open(const std::string &dir)
{
    dir_ = dir;
    entries_.clear();
    list_.reset();
    ui.push(this);
}

void JobsScreen::load()
{
    loading_ = true;
    error_.clear();
    const std::string dir = dir_;
    ui.send("LS -p " + quote(dir_), [this, dir](const Reply &r) {
        if (dir != dir_)
            return; // the folder changed meanwhile
        loading_ = false;
        entries_.clear();
        if (!r.ok)
        {
            error_ = r.error;
            return;
        }
        for (auto &rec : r.records)
            if (rec.type == "dir" || rec.type == "file")
                entries_.push_back({rec.str("name"), rec.type == "dir", rec.integer("size")});
        std::stable_sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
            if (a.dir != b.dir)
                return a.dir;
            return a.name < b.name;
        });
    }, false);
}

bool JobsScreen::back()
{
    if (dir_ == "/")
        return true;
    const size_t slash = dir_.rfind('/');
    dir_ = slash == 0 ? "/" : dir_.substr(0, slash);
    list_.reset();
    load();
    return false;
}

void JobsScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Jobs", true, dir_.size() > 18 ? ("..." + dir_.substr(dir_.size() - 16)).c_str() : dir_.c_str(),
               th::DIM, Icon::Refresh);
    if (!error_.empty() || (entries_.empty() && !loading_))
    {
        const bool noCard = !error_.empty();
        g.rrect(160 - 22, 62, 44, 44, 12, noCard ? th::WARN_BG : th::PANEL);
        g.icon(noCard ? Icon::Sd : Icon::Jobs, 160 - 7, 77, noCard ? th::WARN : th::DIM);
        g.text(noCard ? "Can't read the data folder" : "No .pap files here", 160, 126, Font::Title, th::TEXT, Align::Center);
        g.text(noCard ? error_ : "Upload one on the website, or copy one from the motion board SD (Settings).", 160, 146, Font::Small, th::DIM, Align::Center);
        g.button(J_EMPTY_REFRESH, noCard ? "Retry" : "Refresh", 96, 172, 128, 34, Kind::Primary, Icon::Refresh);
        return;
    }
    if (entries_.empty())
    {
        g.text("Loading ...", 160, 120, Font::Body, th::DIM, Align::Center);
        return;
    }
    std::vector<Row> rows;
    for (auto &e : entries_)
    {
        Row r;
        r.label = e.name;
        if (e.dir)
        {
            r.icon = Icon::Folder;
            r.iconColor = th::PASTE;
            r.sub = e.name == "tmp" ? "received over USB" : "folder";
            r.chev = true;
        }
        else
        {
            r.icon = Icon::File;
            char kb[16];
            snprintf(kb, sizeof(kb), "%.1f KB", e.size / 1024.0f);
            r.value = kb;
        }
        rows.push_back(r);
    }
    list_.draw(g, J_ROWS, rows);
}

void JobsScreen::action(uint16_t id)
{
    if (id == ID_TITLE_ACTION || id == J_EMPTY_REFRESH)
    {
        if (!error_.empty())
            ui.send("SD MOUNT", [this](const Reply &) { load(); }, false);
        else
            load();
        return;
    }
    const int i = id - J_ROWS;
    if (i < 0 || i >= (int)entries_.size())
        return;
    const Entry e = entries_[i];
    if (e.dir)
    {
        dir_ = joinPath(dir_, e.name);
        entries_.clear();
        list_.reset();
        load();
    }
    else
        jobConfirmScreen.open(joinPath(dir_, e.name));
}

// ---------------------------------------------------------------- job chosen

enum
{
    C_RUN = 1,
    C_CHECK,
    C_DELETE,
    C_BACK
};

void JobConfirmScreen::open(const std::string &path)
{
    path_ = path;
    name_ = baseName(path);
    ui.push(this);
    check();
}

void JobConfirmScreen::check()
{
    checking_ = true;
    ok_ = false;
    problems_.clear();
    error_.clear();
    ui.send("CHECK " + quote(path_), [this](const Reply &r) {
        checking_ = false;
        for (auto &rec : r.records)
        {
            if (rec.type == "problem")
                problems_.push_back({(int)rec.integer("line"), rec.str("text")});
            else if (rec.type == "summary")
            {
                parts_ = rec.integer("parts");
                dots_ = rec.integer("dots");
                feeders_ = rec.integer("feeders");
                if (!rec.str("name").empty())
                    name_ = rec.str("name");
            }
        }
        ok_ = r.ok;
        if (!r.ok && problems_.empty())
            error_ = r.error;
    }, false);
}

void JobConfirmScreen::run()
{
    ui.send("RUN " + quote(path_), [this](const Reply &r) {
        if (r.ok)
        {
            if (!ui.contains(&jobScreen))
            {
                ui.goHome();
                ui.push(&jobScreen);
            }
            return;
        }
        problems_.clear();
        for (auto &rec : r.records)
            if (rec.type == "problem")
                problems_.push_back({(int)rec.integer("line"), rec.str("text")});
        if (!problems_.empty())
        {
            ok_ = false;
            ui.push(&jobProblemsScreen);
        }
    });
}

void JobConfirmScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar(baseName(path_).c_str(), true, "jobs");
    g.card(8, 32, 304, 82);
    g.text(name_, 20, 50, Font::Big, th::TEXT);
    if (checking_)
    {
        g.text("checking the file ...", 20, 70, Font::Small, th::DIM);
        g.pill("CHECKING", 20, 86, th::DIM, th::PANEL2);
    }
    else if (ok_)
    {
        char t[64];
        snprintf(t, sizeof(t), "%d parts · %d paste dots · %d feeders", parts_, dots_, feeders_);
        g.text(t, 20, 70, Font::Small, th::DIM);
        const int w = g.pill("CHECKED", 20, 86, th::OK, th::OK_BG);
        g.pill("ALL FEEDERS ON RAIL", 26 + w, 86, th::OK, th::OK_BG);
    }
    else
    {
        g.text(error_.empty() ? "The controller found problems in this file." : error_, 20, 70, Font::Small, th::DIM);
        char t[24];
        snprintf(t, sizeof(t), "%u PROBLEM%s", (unsigned)problems_.size(), problems_.size() == 1 ? "" : "S");
        g.pill(error_.empty() ? t : "ERROR", 20, 86, th::BAD, th::BAD_BG);
    }
    g.button(C_RUN, "Run", 8, 124, 148, 40, ok_ ? Kind::Primary : Kind::Normal, Icon::Play, Font::Big);
    g.button(C_CHECK, problems_.empty() ? "Check" : "Problems", 164, 124, 148, 40, Kind::Normal,
             problems_.empty() ? Icon::Check : Icon::Warn);
    g.button(C_DELETE, "Delete", 8, 172, 148, 40, Kind::Normal, Icon::Trash);
    g.button(C_BACK, "Back", 164, 172, 148, 40, Kind::Normal, Icon::Back);
}

void JobConfirmScreen::action(uint16_t id)
{
    switch (id)
    {
    case C_RUN:
    {
        if (checking_)
            return;
        if (!ok_)
        {
            ui.toast("Fix the problems first", th::BAD);
            return;
        }
        const MachineStatus s = machine.status();
        if (!s.homed || !s.e1Homed)
            dialog.open("Machine not homed", "Home X, Y, Z and E1 now,", "then start " + name_ + "?",
                        {{"Cancel", Kind::Normal, nullptr},
                         {"Home & run", Kind::Primary, [this] { homingScreen.start(false, [this] { run(); }); }}},
                        Icon::Warn, th::WARN, 1);
        else
            run();
        break;
    }
    case C_CHECK:
        if (problems_.empty())
            check();
        else
            ui.push(&jobProblemsScreen);
        break;
    case C_DELETE:
        dialog.open("Delete " + baseName(path_) + "?", "The file is removed from the Pi.", "",
                    {{"No", Kind::Normal, nullptr},
                     {"Delete", Kind::Danger, [this] {
                          ui.send("RM " + quote(path_), [](const Reply &r) {
                              if (!r.ok)
                                  return;
                              ui.pop();
                              ui.toast("Deleted");
                          });
                      }}},
                    Icon::Trash, th::BAD, 0);
        break;
    case C_BACK:
        ui.pop();
        break;
    }
}

// ---------------------------------------------------------------- problems

void JobProblemsScreen::draw(Gfx &g)
{
    const auto &p = jobConfirmScreen.problems_;
    char t[24];
    snprintf(t, sizeof(t), "%u problem%s", (unsigned)p.size(), p.size() == 1 ? "" : "s");
    g.background();
    g.titleBar(("Check: " + baseName(jobConfirmScreen.path_)).c_str(), true, t, th::BAD);
    std::vector<Row> rows;
    for (auto &x : p)
    {
        Row r;
        r.icon = Icon::Warn;
        r.iconColor = th::BAD;
        r.label = x.text;
        r.sub = "line " + std::to_string(x.line);
        rows.push_back(r);
    }
    list_.draw(g, 10, rows, 25, 40, 188);
    g.button(1, "Back", 96, 196, 128, 32, Kind::Normal, Icon::Back);
}

void JobProblemsScreen::action(uint16_t id)
{
    if (id == 1)
        ui.pop();
}

// ---------------------------------------------------------------- running job

enum
{
    JS_PAUSE = 1,
    JS_SKIP,
    JS_STOP,
    JS_OK = 10,
    JS_AGAIN
};

void JobScreen::tick()
{
    // Running / paused / done change the buttons and the layout: full redraw.
    const std::string st = machine.job().state;
    if (st != state_)
    {
        if (!state_.empty())
            ui.redraw();
        state_ = st;
    }
}

uint8_t JobScreen::liveRects(Rect *r) const
{
    if (!machine.job().active())
        return 0;
    r[0] = {200, 0, 120, 24}; // percent in the title bar
    r[1] = {8, 32, 304, 112}; // part, phase, progress, times
    r[2] = {0, 206, 320, 18}; // pump / valve line
    return 3;
}

void JobScreen::draw(Gfx &g)
{
    const JobStatus j = machine.job();
    const MachineStatus s = machine.status();
    g.background();

    if (!j.active())
    {
        const bool done = j.state == "done";
        const bool failed = j.state == "failed";
        g.titleBar(j.name.empty() ? "Job" : j.name.c_str(), true, j.state.c_str(), done ? th::OK : failed ? th::BAD : th::DIM);
        const uint16_t c = done ? th::OK : failed ? th::BAD : th::WARN;
        g.disc(160, 66, 26, done ? th::OK_BG : failed ? th::BAD_BG : th::WARN_BG);
        g.icon(done ? Icon::Check : Icon::Warn, 160 - 7, 59, c);
        g.text(done ? "Job done" : failed ? "Job stopped with an error" : j.state == "stopped" ? "Job stopped" : "No job running",
               160, 110, Font::Big, th::TEXT, Align::Center);
        char t[80];
        if (failed)
            g.text(j.error, 160, 132, Font::Body, th::BAD, Align::Center);
        snprintf(t, sizeof(t), "%d of %d parts placed · %d skipped · %s", j.done, j.total, j.skipped, fmtTime(j.elapsed).c_str());
        g.text(t, 160, failed ? 150 : 132, Font::Small, th::DIM, Align::Center);
        if (done && !j.file.empty())
        {
            g.button(JS_AGAIN, "Run again", 24, 164, 128, 38, Kind::Normal, Icon::Play);
            g.button(JS_OK, "OK", 168, 164, 128, 38, Kind::Primary);
        }
        else
            g.button(JS_OK, "OK", 96, 176, 128, 38, Kind::Primary);
        return;
    }

    char t[64];
    snprintf(t, sizeof(t), "%d%%", j.percent);
    g.titleBar(j.name.c_str(), true, t, th::ACCENT);
    const bool paused = j.state == "paused";
    const bool waiting = j.state == "message";
    const char *phase = "RUNNING";
    const char *what = "functions of the job file";
    uint16_t pc = th::ACCENT, pbg = th::ACCENT_BG;
    if (paused || waiting)
    {
        phase = paused ? "PAUSED" : "WAITING";
        what = paused ? "paused after the current move" : "waiting for Continue";
        pc = th::WARN, pbg = th::WARN_BG;
    }
    else if (j.phase == "paste")
        phase = "PASTE", what = "paste tip · head 1", pc = th::PASTE, pbg = th::PASTE_BG;
    else if (j.phase == "pick")
        phase = "PLACE", what = "picking from the feeder";
    else if (j.phase == "place")
        phase = "PLACE", what = "nozzle · head 2";
    else if (j.phase == "home")
        phase = "HOMING", what = "HOME in the job file";
    else if (j.phase == "wait")
        phase = "WAIT", what = "WAIT in the job file";

    g.card(8, 32, 304, 112);
    const int pw = g.pill(phase, 18, 42, pc, pbg);
    g.text(what, 18 + pw + 8, 50, Font::Small, th::DIM);
    const int rw = g.text(j.part.empty() ? "-" : j.part, 18, 76, Font::Huge, th::TEXT);
    g.text(j.feeder, 18 + rw + 10, 79, Font::Body, th::DIM);
    g.text("Next", 18, 104, Font::Small, th::DIM);
    g.text(j.next.empty() ? "-" : j.next, 48, 104, Font::Body, th::TEXT);
    snprintf(t, sizeof(t), "%d / %d parts", j.done, j.total);
    g.text(t, 302, 104, Font::Body, th::TEXT, Align::Right);
    g.progress(18, 118, 284, 8, j.total ? (float)(j.done + j.skipped) / j.total : 0, paused || waiting ? th::WARN : th::ACCENT);
    g.text(fmtTime(j.elapsed) + " elapsed", 18, 136, Font::Small, th::DIM);
    g.text(j.remaining < 0 ? "time left: after the first part" : "~ " + fmtTime(j.remaining) + " left", 302, 136, Font::Small,
           th::DIM, Align::Right);

    const int bw = (320 - 32) / 3;
    g.button(JS_PAUSE, paused ? "Resume" : "Pause", 8, 154, bw, 40, paused ? Kind::Primary : Kind::Normal, paused ? Icon::Play : Icon::Pause);
    g.button(JS_SKIP, "Skip", 8 + bw + 8, 154, bw, 40, Kind::Normal, Icon::Skip);
    g.button(JS_STOP, "Stop", 8 + 2 * (bw + 8), 154, bw, 40, Kind::Danger, Icon::Stop);
    snprintf(t, sizeof(t), "Pump %d%% · valve %s%s", s.pump, s.valve ? "open" : "closed",
             j.skipped ? (" · " + std::to_string(j.skipped) + " skipped").c_str() : "");
    g.text(t, 160, 214, Font::Small, th::DIM, Align::Center);
}

void JobScreen::action(uint16_t id)
{
    const JobStatus j = machine.job();
    switch (id)
    {
    case JS_PAUSE:
        ui.send(j.state == "paused" ? "RESUME" : "PAUSE");
        break;
    case JS_SKIP:
        dialog.open("Skip " + (j.part.empty() ? std::string("this part") : j.part) + "?", "The part is not placed;",
                    "a picked part is dropped.", {{"No", Kind::Normal, nullptr}, {"Skip", Kind::Primary, [] { ui.send("SKIP"); }}},
                    Icon::Skip, th::ACCENT, 0);
        break;
    case JS_STOP:
        dialog.open("Stop the job?", "The machine parks (run_after),", "then pump and valve switch off.",
                    {{"No", Kind::Normal, nullptr}, {"Stop", Kind::Danger, [] { ui.send("STOP"); }}}, Icon::Stop, th::BAD, 0);
        break;
    case JS_OK:
        ui.goHome();
        break;
    case JS_AGAIN:
        ui.send("RUN " + quote(j.file));
        break;
    }
}
