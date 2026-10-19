/* Host tests for the portable GUI modules (inflate, aminet, game_list).
 * Usage: test_gui <work_dir> <INDEX.gz> <INDEX uncompressed reference> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/inflate.h"
#include "gui/aminet.h"
#include "gui/game_list.h"

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
    gl_list list = { 0, 0, 0 };
    gl_entry **shown;
    int found_aaa = 0;

    if (argc < 4) { printf("usage: test_gui <work> <INDEX.gz> <INDEX>\n"); return 2; }

    CHECK(aminet_parse_size("671K") == 671UL * 1024);
    CHECK(aminet_parse_size("0K") == 0);
    CHECK(aminet_parse_size("9.5M") == 9UL * 1048576 + 1048576 / 2);
    CHECK(inflate_crc32(0, (const unsigned char *)"123456789", 9) == 0xCBF43926UL);

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

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
