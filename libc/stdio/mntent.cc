#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <osv/mount.h>
#include <fs/fs.hh>
#include <sstream>
#include <libc/libc.hh>
#include "stdio_impl.h"

class mtab_file final : public special_file {
    public:
        mtab_file() : special_file(FREAD, DTYPE_UNSPEC)
        {
            _mounts = osv::current_mounts();
        }

        int close() { return 0; }

        virtual int read(struct uio *uio, int flags) override;

    private:
        std::vector<osv::mount_desc> _mounts;

    private:
        void get_mntline(osv::mount_desc& m, std::string& line);
};

void mtab_file::get_mntline(osv::mount_desc& m, std::string& line)
{
    std::stringstream fmt;

    fmt << " " << m.special << " " << m.path << " " << m.type << " "
        << (m.options.size() ? m.options : MNTOPT_DEFAULTS) << " 0 0\n";

    line = fmt.str();
}

int mtab_file::read(struct uio *uio, int flags)
{
    auto fp = this;
    std::string  line;
    struct iovec *iov;
    size_t       n;
    char         *p;

    size_t skip = 0;
    if ((flags & FOF_OFFSET) == 0) {
        skip = fp->f_offset;
    } else {
        skip = uio->uio_offset;
    }

    int j = 0;
    int iov_skip = 0;


    size_t bc = 0;
    for (size_t i = 0; i < _mounts.size() && uio->uio_resid > 0; i++) {
        auto& m = _mounts[i];

        get_mntline(m, line);
        if (skip > 0) {
            if (skip >= line.size()) {
                skip -= line.size();
                continue;
            }
        }

        size_t t = line.size() - skip;
        const char *q = line.c_str() + skip;

        for (; t > 0 && uio->uio_resid > 0; ) {
            iov = uio->uio_iov + j;
            n = std::min(iov->iov_len - iov_skip, t);
            p = static_cast<char *> (iov->iov_base) + iov_skip;
            std::copy(q, q+n, p);

            if (n == t) {
                iov_skip += n;
            } else {
                iov_skip = 0;
                j++;
            }

            t              -= n;
            uio->uio_resid -= n;
            q              += n;
            bc             += n;
        }
        skip = 0;
    }

    if ((flags & FOF_OFFSET) == 0) {
        fp->f_offset += bc;
    }
    return 0;
}

FILE *setmntent(const char *name, const char *mode)
{
    if (!strcmp(name, "/proc/mounts") || !strcmp(name, "/etc/mnttab") || !strcmp(name, "/etc/mtab")) {
        if (strcmp("r", mode)) {
            return nullptr;
        }

        fileref f = make_file<mtab_file>();
        struct file * fp = f.get();
        if (fp == NULL) {
            return nullptr;
        }

        fhold(fp);

        int fd;
        int rc = fdalloc(fp, &fd);
        fdrop(fp);
        if (rc) {
            return nullptr;
        }

        return __fdopen(fd, "r");
    }
    return fopen(name, mode);
}

int endmntent(FILE *f)
{
    if (f != OSV_DYNMOUNTS) {
        fclose(f);
    }
    return 1;
}

bool osv_getmntent(char *linebuf, int buflen)
{
    // FIXME: we're using a static here in lieu of the file offset
    static size_t last = 0;
    auto mounts = osv::current_mounts();
    if (last >= mounts.size()) {
        last = 0;
        return false;
    } else {
        auto& m = mounts[last++];

        snprintf(linebuf, buflen, " %s %s %s %s 0 0",
                 m.special.c_str(),
                 m.path.c_str(),
                 m.type.c_str(),
                 m.options.size() ? m.options.c_str() : MNTOPT_DEFAULTS);

        return true;
    }
}

struct mntent *getmntent_r(FILE *f, struct mntent *mnt, char *linebuf, int buflen)
{
    int cnt, n[8];

    if (!f) {
        return nullptr;
    }

    mnt->mnt_freq = 0;
    mnt->mnt_passno = 0;

    do {
        if (f == OSV_DYNMOUNTS) {
            bool ret = osv_getmntent(linebuf, buflen);
            if (!ret) {
                return nullptr;
            }
        } else {
            fgets(linebuf, buflen, f);
            if (feof(f) || ferror(f)) return 0;
            if (!strchr(linebuf, '\n')) {
                fscanf(f, "%*[^\n]%*[\n]");
                return nullptr;
            }
        }
        cnt = sscanf(linebuf, " %n%*s%n %n%*s%n %n%*s%n %n%*s%n %d %d",
            n, n+1, n+2, n+3, n+4, n+5, n+6, n+7,
            &mnt->mnt_freq, &mnt->mnt_passno);
    } while (cnt < 2 || linebuf[n[0]] == '#');

    linebuf[n[1]] = 0;
    linebuf[n[3]] = 0;
    linebuf[n[5]] = 0;
    linebuf[n[7]] = 0;

    mnt->mnt_fsname = linebuf+n[0];
    mnt->mnt_dir = linebuf+n[2];
    mnt->mnt_type = linebuf+n[4];
    mnt->mnt_opts = linebuf+n[6];

    return mnt;
}

struct mntent *getmntent(FILE *f)
{
    static char linebuf[256];
    static struct mntent mnt;
    return getmntent_r(f, &mnt, linebuf, sizeof linebuf);
}

int addmntent(FILE *f, const struct mntent *mnt)
{
    if (f == OSV_DYNMOUNTS) {
        return 1;
    }
    if (fseek(f, 0, SEEK_END)) return 1;
    return fprintf(f, "%s\t%s\t%s\t%s\t%d\t%d\n",
        mnt->mnt_fsname, mnt->mnt_dir, mnt->mnt_type, mnt->mnt_opts,
        mnt->mnt_freq, mnt->mnt_passno) < 0;
}

char *hasmntopt(const struct mntent *mnt, const char *opt)
{
    return strstr(mnt->mnt_opts, opt);
}
