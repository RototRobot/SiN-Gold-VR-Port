/* sinvr.log, written beside ref_vr.dll. */
#ifndef VRLOG_H
#define VRLOG_H

/* Opens the log beside the given module. The first open in a process starts a
   fresh file; later ones (the engine reloads the renderer on vid_restart) append. */
void vrlog_open(void *module);
void vrlog_close(void);

/* One timestamped line. */
void vrlog(const char *fmt, ...);

#endif /* VRLOG_H */
