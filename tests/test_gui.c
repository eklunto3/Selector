/* Host tests for the portable GUI modules (inflate, aminet, game_list).
 * Usage: test_gui <work_dir> <INDEX.gz> <INDEX uncompressed reference> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/inflate.h"
#include "gui/aminet.h"
#include "gui/game_list.h"
#include "gui/modland.h"
#include <sys/stat.h>

static void mkdir_p(const char *d) { mkdir(d, 0755); }

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int files_equal(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int ca, cb, eq = fa && fb;
    while (eq) {
        ca = getc(fa);
        cb = getc(fb);
        if (ca != cb) eq = 0;
        if (ca == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return eq;
}

int main(int argc, char **argv)
{
    char out[512], path[512];
    long total, n, i;
    gl_list list = { 0, 0, 0, 0 };
    gl_entry **shown;
    int found_aaa = 0;

    if (argc < 4) { printf("usage: test_gui <work> <INDEX.gz> <INDEX>\n"); return 2; }

    CHECK(aminet_parse_size("671K") == 671UL * 1024);
    CHECK(aminet_parse_size("0K") == 0);
    CHECK(aminet_parse_size("9.5M") == 9UL * 1048576 + 1048576 / 2);
    CHECK(inflate_crc32(0, (const unsigned char *)"123456789", 9) == 0xCBF43926UL);
    {
        char b[16];
        CHECK(strcmp(aminet_age_text(0, b), "this week") == 0);
        CHECK(strcmp(aminet_age_text(3, b), "3 weeks") == 0);
        CHECK(strcmp(aminet_age_text(52, b), "12 months") == 0);
        CHECK(strcmp(aminet_age_text(300, b), "5 years") == 0);
        CHECK(strcmp(aminet_age_text(999, b), "19+ years") == 0);
    }
    {
        /* RECENT has the INDEX line format. */
        FILE *f;
        gl_list r = { 0, 0, 0, 0 };
        sprintf(path, "%s/RECENT", argv[1]);
        f = fopen(path, "w");
        fputs("|\n| Aminet index, created on 19-Oct-2026\n|\n\n"
              "amisync.lha                    comm/net   473K   0 Syncthing-compatible file sync daemon\n"
              "openlara.lha                   game/shoot  27M   0 Tomb Raider - OpenLara\n", f);
        fclose(f);
        sprintf(out, "%s/recent.txt", argv[1]);
        CHECK(aminet_convert_list(path, out) == 2);
        CHECK(gl_load(&r, out, GL_FMT_AMINET) && r.count == 2);
        CHECK(r.count == 2 && strcmp(r.items[1].dir, "game/shoot") == 0 &&
              r.items[1].size == 27UL * 1048576 && r.items[1].age == 0 &&
              strcmp(r.items[1].desc, "Tomb Raider - OpenLara") == 0);
        gl_free(&r);
    }

    sprintf(out, "%s/INDEX", argv[1]);
    CHECK(inflate_gunzip_file(argv[2], out) == IF_OK);
    CHECK(files_equal(out, argv[3]));
    CHECK(inflate_gunzip_file(argv[3], out) == IF_ERR_FORMAT);   /* not gzip */

    total = aminet_split_index(argv[3], argv[1]);
    printf("aminet packages: %ld\n", total);
    CHECK(total > 80000);

    sprintf(path, "%s/biz.txt", argv[1]);
    CHECK(gl_load(&list, path, GL_FMT_AMINET));
    printf("biz: %ld\n", list.count);
    CHECK(list.count > 500);
    for (i = 0; i < list.count; i++)
        if (strcmp(list.items[i].name, "AAA_MegaBook.lha") == 0) {
            found_aaa = 1;
            CHECK(strcmp(list.items[i].dir, "biz/dbase") == 0);
            CHECK(list.items[i].size == 279UL * 1024);
            CHECK(strncmp(list.items[i].desc, "ULTIMATE Amiga Address Book", 27) == 0);
            CHECK(list.items[i].age == 999);
        }
    CHECK(found_aaa);

    shown = (gl_entry **)malloc((size_t)list.count * sizeof(gl_entry *));
    n = gl_filter(&list, "address", GL_LETTER_ALL, shown);   /* matches descriptions */
    printf("biz 'address': %ld\n", n);
    CHECK(n > 5);
    n = gl_filter(&list, "", 'A', shown);
    for (i = 0; i < n; i++) CHECK(shown[i]->name[0] == 'A' || shown[i]->name[0] == 'a');
    free(shown);
    gl_free(&list);

    /* Modland */
    {
        char enc[200], local[40];
        CHECK(modland_url_encode("Ace Tracker/505/blocks.am", enc, sizeof(enc)));
        CHECK(strcmp(enc, "Ace%20Tracker/505/blocks.am") == 0);
        CHECK(modland_url_encode("A & B/x#y.mod", enc, sizeof(enc)) && strcmp(enc, "A%20%26%20B/x%23y.mod") == 0);
        modland_local_name("short.mod", local);
        CHECK(strcmp(local, "short.mod") == 0);
        modland_local_name("a very long module name that does not fit.mod", local);
        CHECK(strlen(local) == 30 && strcmp(local + 26, ".mod") == 0);
        modland_local_name("my song.mod", local);
        CHECK(strcmp(local, "my_song.mod") == 0);
        modland_local_name("bad:name.mod", local);
        CHECK(strcmp(local, "bad_name.mod") == 0);
        CHECK(modland_category_count() == 20);
        CHECK(modland_category_split(0) && !modland_category_split(4));
    }
    if (argc > 4) {
        char dir[512];
        gl_list m = { 0, 0, 0, 0 };
        long kept;
        sprintf(dir, "%s/modland", argv[1]);
        mkdir_p(dir);
        kept = modland_split_index(argv[4], dir);
        printf("modland kept: %ld\n", kept);
        CHECK(kept > 90000);
        modland_list_path(dir, 4, 0, path);              /* AHX */
        CHECK(gl_load(&m, path, GL_FMT_AMINET));
        printf("AHX: %ld\n", m.count);
        CHECK(m.count == 1390);
        CHECK(m.count > 0 && strncmp(m.items[0].desc, "AHX/", 4) == 0);
        gl_free(&m);
        modland_list_path(dir, 0, 'A', path);            /* Protracker, authors A */
        CHECK(gl_load(&m, path, GL_FMT_AMINET));
        printf("Protracker A: %ld\n", m.count);
        CHECK(m.count > 1000);
        for (i = 0; i < m.count && i < 500; i++)
            CHECK(m.items[i].dir[0] == 'A' || m.items[i].dir[0] == 'a');
        gl_free(&m);
        {
            int trunc;
            long hits;
            snprintf(path, sizeof(path), "%.400s/search.txt", dir);
            hits = modland_search(dir, "Purple Motion sundance", path, 2000, &trunc);
            printf("search 'Purple Motion sundance': %ld\n", hits);
            CHECK(hits == 1 && !trunc);
            CHECK(gl_load(&m, path, GL_FMT_AMINET) && m.count == 1 &&
                  strcmp(m.items[0].desc, "Protracker/Purple Motion/sundance.mod") == 0);
            gl_free(&m);
            hits = modland_search(dir, "purple motion", path, 2000, &trunc);
            printf("search 'purple motion': %ld\n", hits);
            CHECK(hits >= 6);
            hits = modland_search(dir, "space debris", path, 2000, &trunc);
            printf("search space debris: %ld\n", hits);
            CHECK(gl_load(&m, path, GL_FMT_AMINET));
            for (i = 0, n = 0; i < m.count; i++)
                if (strcmp(m.items[i].desc, "Protracker/Captain/space_debris.mod") == 0) n = 1;
            CHECK(n == 1);
            gl_free(&m);
            hits = modland_search(dir, "a", path, 50, &trunc);
            CHECK(hits == 50 && trunc);
        }
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
