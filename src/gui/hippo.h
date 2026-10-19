#ifndef GUI_HIPPO_H
#define GUI_HIPPO_H

/* Plays a module in HippoPlayer through its ARexx port "HIPPOPLAYER",
 * with the commands used by the Aminet PlayMod script:
 * GET NFIL, ADD <file>, CHOOSE <n>, PLAY.
 *
 * If HippoPlayer is not running and player_path is set, it is started
 * (asynchronously) and the port is waited for up to about 15 seconds.
 * file must be a full path without spaces. Returns 0 on failure and
 * puts the reason into msg. */
int hippo_play(const char *file, const char *player_path, char *msg, int msg_len);

#endif
