// The microSD card in the Octopus slot (OCTOSD): browse it, copy a file into the Pi's /jobs folder
#include "screens.h"

OctoSdScreen octoSdScreen;

enum
{
    OS_ROWS = 10
};

void OctoSdScreen::open(const std::string &dir)
{
    dir_ = dir.empty() ? "/" : dir;
    entries_.clear();
    error_.clear();
    loading_ = true;
    list_.reset();
    ui.send("OCTOSD LS " + quote(dir_), [this](const Reply &r) {
        loading_ = false;
        if (!r.ok)
        {
            error_ = r.error.empty() ? "no answer" : r.error;
            return;
        }
        std::vector<Entry> dirs, files;
        for (auto &rec : r.records)
        {
            if (rec.type != "file")
                continue;
            Entry e;
            e.name = rec.str("name"), e.size = rec.integer("size"), e.dir = rec.flag("dir");
            if (e.name.empty() || e.name[0] == '.' || e.name == "System Volume Information")
                continue;
            (e.dir ? dirs : files).push_back(e);
        }
        auto byName = [](const Entry &a, const Entry &b) { return a.name < b.name; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        entries_ = dirs;
        entries_.insert(entries_.end(), files.begin(), files.end());
    });
}

bool OctoSdScreen::back()
{
    if (dir_ == "/")
        return true;
    const size_t slash = dir_.find_last_of('/');
    open(slash == 0 ? "/" : dir_.substr(0, slash));
    return false;
}

static std::string sizeText(long b)
{
    char t[24];
    if (b < 1024)
        snprintf(t, sizeof(t), "%ld B", b);
    else
        snprintf(t, sizeof(t), "%.1f KB", b / 1024.0);
    return t;
}

void OctoSdScreen::draw(Gfx &g)
{
    g.background();
    g.titleBar("Motion board SD", true, dir_.c_str());
    std::vector<Row> rows;
    if (loading_ || !error_.empty() || entries_.empty())
    {
        Row r;
        r.icon = error_.empty() ? Icon::Sd : Icon::Warn;
        r.iconColor = error_.empty() ? th::DIM : th::WARN;
        r.label = loading_ ? "Reading the card ..." : !error_.empty() ? error_ : "Empty folder";
        r.sub = error_.empty() ? "" : "card in the Octopus slot? motion standing still?";
        rows.push_back(r);
    }
    for (auto &e : entries_)
    {
        Row r;
        r.icon = e.dir ? Icon::Folder : Icon::File;
        r.label = e.name;
        if (e.dir)
            r.chev = true;
        else
        {
            r.value = sizeText(e.size);
            const bool pap = e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".pap") == 0;
            r.sub = pap ? "job: tap to copy it to the Pi" : "tap to copy it to the Pi";
            r.iconColor = pap ? th::ACCENT : th::DIM;
        }
        rows.push_back(r);
    }
    list_.draw(g, OS_ROWS, rows, 25, 35);
}

void OctoSdScreen::copy(const std::string &path)
{
    ui.toast("Copying ...", th::ACCENT);
    ui.send(
        "OCTOSD COPY " + quote(path),
        [](const Reply &r) {
            if (!r.ok || r.records.empty())
                return;
            const std::string to = r.records[0].str("path");
            dialog.open("Copied to the Pi", to, "It runs from there like every job.",
                        {{"OK", Kind::Normal, nullptr},
                         {"Show jobs", Kind::Primary, [] { ui.post([] { jobsScreen.open("/jobs"); }); }}},
                        Icon::Check, th::OK, 1);
        },
        true, true);
}

void OctoSdScreen::action(uint16_t id)
{
    const int i = id - OS_ROWS;
    if (i < 0 || i >= (int)entries_.size() || loading_)
        return;
    const Entry e = entries_[i];
    const std::string path = (dir_ == "/" ? "" : dir_) + "/" + e.name;
    if (e.dir)
    {
        open(path);
        return;
    }
    dialog.open("Copy to the Pi?", e.name, "into /jobs on the Pi (" + sizeText(e.size) + ")",
                {{"Cancel", Kind::Normal, nullptr}, {"Copy", Kind::Primary, [this, path] { copy(path); }}}, Icon::Sd, th::ACCENT, 1);
}
