#include "feeder_store.h"
#include "general_settings.h"
#include "storage.h"

FeederStore feeders;
Feeder feeder;

static const char DIR_PATH[] = "/feeders";

void FeederStore::path(uint8_t num, char *buf)
{
    sprintf_P(buf, PSTR("/feeders/feeder%02u.txt"), num);
}

// "feeder07.txt" (any case) -> 7, else 0
static uint8_t numberFromName(const char *name)
{
    if (strncasecmp_P(name, PSTR("feeder"), 6) != 0)
        return 0;
    char *end;
    const long n = strtol(name + 6, &end, 10);
    if (end == name + 6 || strcasecmp_P(end, PSTR(".txt")) != 0 || n < 1 || n > 99)
        return 0;
    return (uint8_t)n;
}

static void copyName(char *dst, const char *src)
{
    strncpy(dst, src, FEEDER_NAME_LEN);
    dst[FEEDER_NAME_LEN] = '\0';
}

static void defaultName(char *dst, uint8_t num)
{
    sprintf_P(dst, PSTR("Feeder %02u"), num);
}

static void onEntryKey(const char *key, const char *value, void *ctx)
{
    FeederEntry &e = *(FeederEntry *)ctx;
    if (!strcmp_P(key, PSTR("name")) && *value)
        copyName(e.name, value);
    else if (!strcmp_P(key, PSTR("part")))
    {
        strncpy(e.part, value, FEEDER_PART_LEN);
        e.part[FEEDER_PART_LEN] = '\0';
    }
}

void FeederStore::setEntry(uint8_t num, const char *name, const char *part)
{
    const int8_t i = indexOf(num);
    if (i < 0)
        return;
    copyName(list_[i].name, name);
    strncpy(list_[i].part, part, FEEDER_PART_LEN);
    list_[i].part[FEEDER_PART_LEN] = '\0';
}

int8_t FeederStore::indexOf(uint8_t num) const
{
    for (uint8_t i = 0; i < count_; i++)
        if (list_[i].num == num)
            return i;
    return -1;
}

uint8_t FeederStore::match(const char *text) const
{
    for (uint8_t i = 0; i < count_; i++)
        if (list_[i].part[0] && !strcasecmp(list_[i].part, text))
            return list_[i].num;
    for (uint8_t i = 0; i < count_; i++)
        if (!strcasecmp(list_[i].name, text))
            return list_[i].num;
    return 0;
}

bool FeederStore::scan()
{
    count_ = 0;
    if (!storage.ready())
        return false;
    SdFat &sd = storage.fs();
    if (!sd.exists(DIR_PATH) && !sd.mkdir(DIR_PATH))
        return false;

    File dir;
    if (!dir.open(DIR_PATH, O_RDONLY))
        return false;
    File entry;
    char fileName[24];
    while (count_ < MAX_FEEDERS && storage.openNext(dir, entry)) // tolerant listing
    {
        entry.getName(fileName, sizeof(fileName));
        const bool isFile = !entry.isDir();
        entry.close();
        const uint8_t num = isFile ? numberFromName(fileName) : 0;
        if (!num)
            continue;

        // Insert sorted by number.
        uint8_t pos = count_;
        while (pos > 0 && list_[pos - 1].num > num)
        {
            list_[pos] = list_[pos - 1];
            pos--;
        }
        list_[pos].num = num;
        defaultName(list_[pos].name, num);
        list_[pos].part[0] = '\0';
        count_++;
    }
    dir.close();

    // Names come from inside the files.
    char p[24];
    for (uint8_t i = 0; i < count_; i++)
    {
        path(list_[i].num, p);
        storage.readKv(p, onEntryKey, &list_[i]);
    }
    return true;
}

static void onFeederKey(const char *key, const char *value, void *ctx)
{
    Feeder &f = *(Feeder *)ctx;
    if (!strcmp_P(key, PSTR("name")))
    {
        if (*value)
            copyName(f.name, value);
    }
    else if (!strcmp_P(key, PSTR("feeder_pos")))
        f.feederPos = atof(value);
    else if (!strcmp_P(key, PSTR("advance")))
        f.advance = atof(value);
    else if (!strcmp_P(key, PSTR("teeth")))
        f.teeth = constrain(atoi(value), 0, 255);
    else if (!strcmp_P(key, PSTR("next")))
        f.next = constrain(atoi(value), 1, 255);
    else if (!strcmp_P(key, PSTR("part")))
    {
        strncpy(f.part, value, FEEDER_PART_LEN);
        f.part[FEEDER_PART_LEN] = '\0';
    }
    else if (!strcmp_P(key, PSTR("tag")))
    {
        strncpy(f.tag, value, FEEDER_TAG_LEN);
        f.tag[FEEDER_TAG_LEN] = '\0';
    }
    else if (!strcmp_P(key, PSTR("pocket")) && f.pocketCount < MAX_POCKETS)
    {
        char *end;
        const float x = strtod(value, &end);
        while (*end == ' ' || *end == ',' || *end == ';')
            end++;
        f.pockets[f.pocketCount].x = x;
        f.pockets[f.pocketCount].y = atof(end);
        f.pocketCount++;
    }
}

bool FeederStore::load(uint8_t num, Feeder &f)
{
    f.num = num;
    defaultName(f.name, num);
    f.feederPos = 0.0f;
    f.advance = 0.0f;
    f.teeth = 0;
    f.next = 1;
    f.part[0] = f.tag[0] = '\0';
    f.pocketCount = 0;
    char p[24];
    path(num, p);
    if (!storage.readKv(p, onFeederKey, &f))
        return false;
    if (f.next > f.pocketCount && f.pocketCount > 0)
        f.next = 1;
    return true;
}

bool FeederStore::save(const Feeder &f)
{
    char p[24];
    path(f.num, p);
    File file;
    if (!storage.openWrite(file, p))
        return false;
    file.print(F("# Feeder "));
    file.print(f.num);
    file.print(F(" - edit on a PC or on the display\nname = "));
    file.println(f.name);
    file.print(F("feeder_pos = "));
    file.println(f.feederPos, 2);
    file.print(F("teeth = "));
    file.println(f.teeth);
    file.print(F("advance = "));
    file.println(f.advance, 2);
    file.print(F("next = "));
    file.println(f.next);
    if (f.part[0])
    {
        file.print(F("part = "));
        file.println(f.part);
    }
    if (f.tag[0])
    {
        file.print(F("tag = "));
        file.println(f.tag);
    }
    for (uint8_t i = 0; i < f.pocketCount; i++)
    {
        file.print(F("pocket = "));
        file.print(f.pockets[i].x, 2);
        file.print(F(", "));
        file.println(f.pockets[i].y, 2);
    }
    if (!file.close())
        return false;
    setEntry(f.num, f.name, f.part);
    return true;
}

bool FeederStore::remove(uint8_t num)
{
    if (!storage.ready())
        return false;
    char p[24];
    path(num, p);
    if (!storage.fs().remove(p))
        return false;
    for (uint8_t i = 0; i < count_; i++)
        if (list_[i].num == num)
        {
            memmove(&list_[i], &list_[i + 1], (count_ - i - 1) * sizeof(FeederEntry));
            count_--;
            break;
        }
    return true;
}

int16_t FeederStore::add()
{
    if (!scan() || count_ >= MAX_FEEDERS)
        return -1;
    // Lowest free number.
    uint8_t num = 1;
    for (uint8_t i = 0; i < count_ && list_[i].num == num; i++)
        num++;
    if (num > 99)
        return -1;

    Feeder f;
    f.num = num;
    defaultName(f.name, num);
    f.feederPos = 0.0f;
    f.advance = 0.0f;
    f.teeth = 0;
    f.next = 1;
    f.part[0] = f.tag[0] = '\0';
    f.pocketCount = 0;
    if (!save(f))
        return -1;
    scan();
    return num;
}

// ---------------------------------------------------------------------------
// Auto detect helpers
// ---------------------------------------------------------------------------

struct MatchInfo
{
    char tag[FEEDER_TAG_LEN + 1];
    float pos;
};

static void onMatchKey(const char *key, const char *value, void *ctx)
{
    MatchInfo &m = *(MatchInfo *)ctx;
    if (!strcmp_P(key, PSTR("tag")))
    {
        strncpy(m.tag, value, FEEDER_TAG_LEN);
        m.tag[FEEDER_TAG_LEN] = '\0';
    }
    else if (!strcmp_P(key, PSTR("feeder_pos")))
        m.pos = atof(value);
}

static bool readMatchInfo(uint8_t num, MatchInfo &m)
{
    char p[24];
    FeederStore::path(num, p);
    m.tag[0] = '\0';
    m.pos = 0.0f;
    return storage.readKv(p, onMatchKey, &m);
}

uint8_t FeederStore::findByTag(const char *tag)
{
    MatchInfo m;
    for (uint8_t i = 0; i < count_; i++)
        if (readMatchInfo(list_[i].num, m) && !strcasecmp(m.tag, tag))
            return list_[i].num;
    return 0;
}

uint8_t FeederStore::findUntaggedNear(float pos, float tolerance)
{
    MatchInfo m;
    for (uint8_t i = 0; i < count_; i++)
        if (readMatchInfo(list_[i].num, m) && !m.tag[0] && fabsf(m.pos - pos) <= tolerance)
            return list_[i].num;
    return 0;
}

static char *trimSpaces(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = '\0';
    return s;
}

void FeederStore::applyTagText(Feeder &f, char *text)
{
    char *line = text;
    while (line && *line)
    {
        char *end = strpbrk(line, "\n;");
        char *nextLine = nullptr;
        if (end)
        {
            *end = '\0';
            nextLine = end + 1;
        }
        char *eq = strchr(line, '=');
        if (eq)
        {
            *eq = '\0';
            const char *key = trimSpaces(line);
            const char *value = trimSpaces(eq + 1);
            if (!strcmp_P(key, PSTR("name")) && *value)
                copyName(f.name, value);
            else if (!strcmp_P(key, PSTR("part")))
            {
                strncpy(f.part, value, FEEDER_PART_LEN);
                f.part[FEEDER_PART_LEN] = '\0';
            }
            else if (!strcmp_P(key, PSTR("advance")))
                f.advance = atof(value), f.teeth = 0;
            else if (!strcmp_P(key, PSTR("teeth")))
                f.teeth = constrain(atoi(value), 0, 255);
        }
        line = nextLine;
    }
}

float FeederStore::advanceDeg(const Feeder &f)
{
    if (!f.teeth)
        return f.advance;
    const float wheel = settings.feederWheelTeeth ? settings.feederWheelTeeth : 20;
    return f.teeth * 360.0f / wheel;
}

void FeederStore::qrId(const char *text, char *id)
{
    id[0] = '\0';
    char buf[200];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *save = nullptr;
    for (char *tok = strtok_r(buf, ";\n", &save); tok; tok = strtok_r(nullptr, ";\n", &save))
    {
        char *t = trimSpaces(tok);
        char *eq = strchr(t, '=');
        if (!eq && !id[0])
        {
            strncpy(id, t, FEEDER_TAG_LEN);
            id[FEEDER_TAG_LEN] = '\0';
        }
        else if (eq)
        {
            *eq = '\0';
            if (!strcasecmp(trimSpaces(t), "id"))
            {
                strncpy(id, trimSpaces(eq + 1), FEEDER_TAG_LEN);
                id[FEEDER_TAG_LEN] = '\0';
                return;
            }
        }
    }
    if (!id[0]) // only keys: the whole text identifies the feeder
    {
        strncpy(id, text, FEEDER_TAG_LEN);
        id[FEEDER_TAG_LEN] = '\0';
    }
}

void FeederStore::applyQr(Feeder &f, const char *text)
{
    qrId(text, f.tag);
    char buf[200];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    applyTagText(f, buf);
}
