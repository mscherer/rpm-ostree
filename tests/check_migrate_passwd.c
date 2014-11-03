#include <check.h>

/* workaround for: implicit declaration of function ‘putgrent’ */
#define __USE_GNU 1

#include "../src/rpmostree-postprocess.h"
#include "../src/rpmostree-postprocess.c"

/*
** Invokes a helper script that does all the setup and checking work for us.
*/
static char *
migrate_helper(char *action,          /* setup or check */
               char *file,            /* passwd or group */
               int testnumber,        /* 0 .. N */
               char *tmpdir,          /* path to tmp dir */
               GError **error) {
    char i_buf[10];                   /* for string form of _i */
    gchar *argv[] = { "./tests/migrate-helper",
                      action, file, i_buf, tmpdir,
                      NULL };
    int estatus;
    char *my_stdout = NULL;
    char *my_stderr = NULL;

    sprintf(i_buf, "%d", testnumber);

    /* Run the helper. This should never fail */
    if (!g_spawn_sync (NULL, (char**)argv, NULL,
                         0, NULL, NULL,
                         &my_stdout, &my_stderr, &estatus, error)) {
        /* error running the command, e.g. script not found. */
        /* FIXME: we should be more helpful than this */
        g_free(my_stdout);
        g_free(my_stderr);
        return g_strdup("ERROR running helper script");
    }

    /*
    ** Helper should never exit with error status. If it does, it's
    ** a bug here. Try to emit a helpful diagnostic.
    */
    if (!g_spawn_check_exit_status(estatus, error)) {
        g_prefix_error (error, "Executing %s: ", argv[0]);
        fprintf(stderr,"ERROR: bad exit status (%d) from helper script\n", estatus);
        fprintf(stderr, "  command was: %s\n", g_strjoinv(" ",argv));
        if (my_stderr && my_stderr[0]) {
            fprintf(stderr, "  stderr was: %s\n", my_stderr);
        }
        return "ERROR: bad exit status from helper; see logs";
    }

    /* Helper should never emit anything to stderr */
    if (my_stderr && my_stderr[0]) {
        g_free(my_stdout);
        return my_stderr;
    }

    /* Or to stdout, either, but we return whatever we got (hopefully "") */
    g_free(my_stderr);
    return my_stdout;
}

/*
** This is the main test. It calls a setup helper, runs migrate_etc_etc(),
** then calls our helper again to check that everything worked.
*/
START_TEST(test_migrate_passwd)
{
    GFile *rootfs = NULL;
    GCancellable *cancellable = NULL;
    GError       *error = NULL;
    char         *helper_stdout;
    char         *files[] = { "passwd", "group", NULL };
    int i;

    /* Run once for each file (passwd, group) */
    for (i=0; files[i]; i++) {
        gs_free char *tmpd = g_mkdtemp(g_strdup("/var/tmp/rpm-ostree.XXXXXX"));
        helper_stdout = migrate_helper("setup", files[i], _i, tmpd, &error);
        ck_assert_str_eq(helper_stdout,"");

        /* Run our code */
        /*
        ** FIXME: this relies on the fact that migrate_etc() defines
        ** an enum where PASSWD=0 and GROUP=1 ! migrate_etc() should
        ** probably accept a string ('passwd', 'group') instead.
        */
        rootfs = g_file_new_for_path (tmpd);
        migrate_passwd_file_except_root (rootfs, i, cancellable, &error);
        if (error) {
          fprintf(stderr, "***** FIXME! GOT SOME SORT OF ERROR!\n");
        }

        /* migrate complete. We should now have two files: etc, usr/lib */
        helper_stdout = migrate_helper("check", files[i], _i, tmpd, &error);
        ck_assert_str_eq(helper_stdout,"");
    }
}
END_TEST

/*
** Scaffolding. This just finds out how many tests we have, and runs them
*/
static Suite * migrate_passwd_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("MigratePasswd");

    /* Core test case */
    tc_core = tcase_create("Core");

    /* Ask helper for number of tests */
    {
        int num_tests;
        char *num_tests_s = migrate_helper("numtests","passwd",0,"/tmp",NULL);
        sscanf(num_tests_s, "%d", &num_tests);
        /* FIXME: ck_assert isn't the right thing to use here */
        if (! num_tests) {
          fprintf(stderr, "FIXME: error running numtests: expected a number, got '%s'\n", num_tests_s);
        }
        g_free(num_tests_s);
        tcase_add_loop_test (tc_core, test_migrate_passwd, 0, num_tests);
    }

    suite_add_tcase(s, tc_core);

    return s;
}

/*
** Scaffolding. Nothing to see here.
*/
int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = migrate_passwd_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? 0 : 1;
}
