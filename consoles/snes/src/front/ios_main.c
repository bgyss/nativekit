/*
 * ios_main.c: the kit's iOS host runs the game's main as nk_game_main; a
 * game writes NSHVC_MAIN, which on iOS defines nshvc_game_main.
 */
int nshvc_game_main(int argc, char **argv);
int nk_game_main(int argc, char **argv);

int nk_game_main(int argc, char **argv) { return nshvc_game_main(argc, argv); }
