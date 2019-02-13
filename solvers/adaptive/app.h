#ifndef APP_H
#define APP_H 1

#include "adaptive.h"

typedef struct app
{
  occa::device device;

  p4est_connectivity_t *conn;
  p4est_t *pxest;
  p4est_ghost_t *ghost;

  // level_t *levels;
} app_t;

app_t *app_new(setupAide &options, MPI_Comm comm);
void app_free(app_t *app);

#endif
