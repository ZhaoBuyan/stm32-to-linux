/* Loader regression: repeat reload, then verify failure clears callbacks. */
#include "fbview_loader.c"

int main(int argc, char **argv)
{
    FvAdapter ad;
    FvConfig cfg = {0};
    char err[1024];
    if (argc != 2) return 2;
    strcpy(cfg.dll, "demo_adapter.dll");
    if (Fv_LoadAdapter(argv[1], &cfg, &ad, err, sizeof(err)) != 0) return 3;
    if (Fv_ReloadAdapter(&ad, err, sizeof(err)) != 0) return 4;
    if (Fv_ReloadAdapter(&ad, err, sizeof(err)) != 0) return 5;
    /* Deterministic failure without changing files on disk. */
    strcpy(s_last_folder, "Z:\\fbview_nonexistent_project");
    if (Fv_ReloadAdapter(&ad, err, sizeof(err)) == 0) return 6;
    if (ad.dll || ad.Attach || ad.Render || ad.Update || ad.Key || ad.Title) return 7;
    puts("PASS: repeated reload and failed reload cleanup");
    return 0;
}
